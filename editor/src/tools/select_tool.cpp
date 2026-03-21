#include "select_tool.h"
#include "geometry_utils.h"
#include "document/commands/cmd_move_vertex.h"
#include "document/commands/cmd_slide_wall.h"
#include "document/commands/cmd_move_sector.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

namespace daedalus::editor
{

// ─── onMouseDown ──────────────────────────────────────────────────────────────

void SelectTool::onMouseDown(EditMapDocument& doc,
                              float mapX, float mapZ,
                              int   button)
{
    if (button != 0) return;

    const glm::vec2            p{mapX, mapZ};
    const world::WorldMapData& map      = doc.mapData();
    auto&                      sel      = doc.selection();
    const bool                 shiftHeld = ImGui::GetIO().KeyShift;

    // 1. Vertex hit test — priority over walls and sectors.
    {
        float           bestDistSq = k_vertexRadius * k_vertexRadius;
        world::SectorId bestSector = world::INVALID_SECTOR_ID;
        std::size_t     bestWall   = 0;

        for (std::size_t si = 0; si < map.sectors.size(); ++si)
        {
            const auto& sector = map.sectors[si];
            for (std::size_t wi = 0; wi < sector.walls.size(); ++wi)
            {
                const glm::vec2 d = sector.walls[wi].p0 - p;
                const float     distSq = glm::dot(d, d);
                if (distSq < bestDistSq)
                {
                    bestDistSq = distSq;
                    bestSector = static_cast<world::SectorId>(si);
                    bestWall   = wi;
                }
            }
        }

        if (bestSector != world::INVALID_SECTOR_ID)
        {
            sel.clear();
            sel.type            = SelectionType::Vertex;
            sel.vertexSectorId  = bestSector;
            sel.vertexWallIndex = bestWall;

            // Begin vertex drag (Shift only disables snapping — drag always starts).
            m_dragTarget    = DragTarget::Vertex;
            m_dragSectorId  = bestSector;
            m_dragWallIndex = bestWall;
            m_dragOrigPos   = map.sectors[bestSector].walls[bestWall].p0;
            m_dragMoved     = false;
            return;
        }
    }

    // 2. Wall hit test.
    {
        float           bestDistSq = k_wallThresholdSq;
        world::SectorId bestSector = world::INVALID_SECTOR_ID;
        std::size_t     bestWall   = 0;

        for (std::size_t si = 0; si < map.sectors.size(); ++si)
        {
            const auto& sector = map.sectors[si];
            const std::size_t n = sector.walls.size();
            for (std::size_t wi = 0; wi < n; ++wi)
            {
                const glm::vec2 a = sector.walls[wi].p0;
                const glm::vec2 b = sector.walls[(wi + 1) % n].p0;
                const float     distSq = geometry::pointToSegmentDistSq(p, a, b);
                if (distSq < bestDistSq)
                {
                    bestDistSq = distSq;
                    bestSector = static_cast<world::SectorId>(si);
                    bestWall   = wi;
                }
            }
        }

        if (bestSector != world::INVALID_SECTOR_ID)
        {
            sel.clear();
            sel.type         = SelectionType::Wall;
            sel.wallSectorId = bestSector;
            sel.wallIndex    = bestWall;

            if (!shiftHeld)
            {
                // Begin wall (slide) drag.
                const auto&       wls = map.sectors[bestSector].walls;
                const std::size_t n   = wls.size();
                m_dragTarget           = DragTarget::Wall;
                m_dragSectorId         = bestSector;
                m_dragWallIndex        = bestWall;
                m_dragOrigPos          = wls[bestWall].p0;
                m_dragOrig2            = wls[(bestWall + 1) % n].p0;
                m_dragOrigWallCurveA   = wls[bestWall].curveControlA;
                m_dragOrigWallCurveB   = wls[bestWall].curveControlB;
                m_dragClickPos         = p;
                m_dragMoved            = false;
                m_dragFirstMove        = true;
            }
            return;
        }
    }

    // 3. Sector hit test.
    {
        const world::SectorId hit = hitTestSector(map, mapX, mapZ);

        if (shiftHeld)
        {
            // Shift+click: toggle sector in/out of multi-selection (no drag).
            if (hit != world::INVALID_SECTOR_ID)
            {
                if (sel.type != SelectionType::Sector)
                {
                    sel.clear();
                    sel.type = SelectionType::Sector;
                }
                const auto it = std::find(sel.sectors.begin(), sel.sectors.end(), hit);
                if (it != sel.sectors.end())
                    sel.sectors.erase(it);
                else
                    sel.sectors.push_back(hit);
            }
            // Shift+click on empty space: preserve selection.
            return;
        }

        // Normal click: replace selection and start sector drag.
        fprintf(stderr, "[sel] DESELECT (empty-space click) mapX=%.2f mapZ=%.2f\n", mapX, mapZ);
        sel.clear();
        if (hit != world::INVALID_SECTOR_ID)
        {
            sel.type = SelectionType::Sector;
            sel.sectors.push_back(hit);

            // Save all wall vertex positions and curve control points for sector drag.
            const auto& walls = map.sectors[hit].walls;
            m_dragOrigAll.clear();
            m_dragOrigAll.reserve(walls.size());
            m_dragOrigCurveA.clear();
            m_dragOrigCurveA.reserve(walls.size());
            m_dragOrigCurveB.clear();
            m_dragOrigCurveB.reserve(walls.size());
            for (const auto& wall : walls)
            {
                m_dragOrigAll.push_back(wall.p0);
                m_dragOrigCurveA.push_back(wall.curveControlA);
                m_dragOrigCurveB.push_back(wall.curveControlB);
            }

            m_dragTarget    = DragTarget::Sector;
            m_dragSectorId  = hit;
            m_dragClickPos  = p;
            m_dragMoved     = false;
            m_dragFirstMove = true;
        }
    }
}

// ─── onMouseMove ──────────────────────────────────────────────────────────────

void SelectTool::onMouseMove(EditMapDocument& doc,
                              float mapX, float mapZ)
{
    if (m_dragTarget == DragTarget::None) return;
    if (m_dragSectorId >= doc.mapData().sectors.size()) return;

    const glm::vec2 mouse{mapX, mapZ};
    auto& sectors = doc.mapData().sectors;

    // Align the drag anchor to the first grid-snapped mouse position so that
    // the delta is always computed in snapped space.  Without this, clicking
    // off-grid would produce a non-zero delta even before the mouse moves,
    // shifting any wall endpoints that lie at off-grid positions (e.g. after
    // a vertex snap).
    if (m_dragFirstMove &&
        (m_dragTarget == DragTarget::Wall || m_dragTarget == DragTarget::Sector))
    {
        m_dragClickPos  = mouse;
        m_dragFirstMove = false;
    }

    switch (m_dragTarget)
    {
    case DragTarget::Vertex:
    {
        auto& walls = sectors[m_dragSectorId].walls;
        if (m_dragWallIndex >= walls.size()) return;
        walls[m_dragWallIndex].p0 = mouse;
        break;
    }
    case DragTarget::Wall:
    {
        auto& walls = sectors[m_dragSectorId].walls;
        if (m_dragWallIndex >= walls.size()) return;
        const std::size_t n     = walls.size();
        const glm::vec2   delta = mouse - m_dragClickPos;
        walls[m_dragWallIndex].p0             = m_dragOrigPos + delta;
        walls[(m_dragWallIndex + 1) % n].p0   = m_dragOrig2   + delta;
        // Move curve control points on the slid wall by the same delta.
        if (m_dragOrigWallCurveA.has_value())
            walls[m_dragWallIndex].curveControlA = *m_dragOrigWallCurveA + delta;
        if (m_dragOrigWallCurveB.has_value())
            walls[m_dragWallIndex].curveControlB = *m_dragOrigWallCurveB + delta;
        break;
    }
    case DragTarget::Sector:
    {
        auto& walls = sectors[m_dragSectorId].walls;
        if (m_dragOrigAll.size() != walls.size()) return;
        const glm::vec2 delta = mouse - m_dragClickPos;
        for (std::size_t i = 0; i < walls.size(); ++i)
        {
            walls[i].p0 = m_dragOrigAll[i] + delta;
            // Move curve control points by the same delta.
            if (i < m_dragOrigCurveA.size() && m_dragOrigCurveA[i].has_value())
                walls[i].curveControlA = *m_dragOrigCurveA[i] + delta;
            if (i < m_dragOrigCurveB.size() && m_dragOrigCurveB[i].has_value())
                walls[i].curveControlB = *m_dragOrigCurveB[i] + delta;
        }
        break;
    }
    default:
        break;
    }

    doc.markDirty();
    m_dragMoved = true;
}

// ─── onMouseUp ────────────────────────────────────────────────────────────────

void SelectTool::onMouseUp(EditMapDocument& doc,
                            float mapX, float mapZ,
                            int   button)
{
    if (button != 0 || m_dragTarget == DragTarget::None) return;

    const DragTarget target = m_dragTarget;
    m_dragTarget = DragTarget::None;

    if (!m_dragMoved) return;
    if (m_dragSectorId >= doc.mapData().sectors.size()) return;

    auto& sectors = doc.mapData().sectors;

    switch (target)
    {
    case DragTarget::Vertex:
    {
        auto& walls = sectors[m_dragSectorId].walls;
        if (m_dragWallIndex >= walls.size()) return;
        const glm::vec2 finalPos = walls[m_dragWallIndex].p0;
        doc.pushCommand(std::make_unique<CmdMoveVertex>(
            doc, m_dragSectorId, m_dragWallIndex, m_dragOrigPos, finalPos));

        // Keep vertex selected.
        auto& sel           = doc.selection();
        sel.type            = SelectionType::Vertex;
        sel.vertexSectorId  = m_dragSectorId;
        sel.vertexWallIndex = m_dragWallIndex;
        break;
    }
    case DragTarget::Wall:
    {
        auto& walls = sectors[m_dragSectorId].walls;
        if (m_dragWallIndex >= walls.size()) return;
        const std::size_t n       = walls.size();
        const glm::vec2   newPos1 = walls[m_dragWallIndex].p0;
        const glm::vec2   newPos2 = walls[(m_dragWallIndex + 1) % n].p0;
        doc.pushCommand(std::make_unique<CmdSlideWall>(
            doc, m_dragSectorId, m_dragWallIndex,
            m_dragOrigPos, newPos1,
            m_dragOrig2,   newPos2));
        break;
    }
    case DragTarget::Sector:
    {
        auto& walls = sectors[m_dragSectorId].walls;
        if (m_dragOrigAll.empty() || walls.empty()) return;
        // Compute actual delta from the first wall (accounts for grid snapping).
        const glm::vec2 delta = walls[0].p0 - m_dragOrigAll[0];
        doc.pushCommand(std::make_unique<CmdMoveSector>(
            doc, m_dragSectorId, delta));
        break;
    }
    default:
        break;
    }

    (void)mapX; (void)mapZ;
}

// ─── onRectSelect ────────────────────────────────────────────────────────────

void SelectTool::onRectSelect(EditMapDocument& doc,
                               glm::vec2        minCorner,
                               glm::vec2        maxCorner)
{
    const world::WorldMapData& map = doc.mapData();
    auto& sel = doc.selection();

    std::vector<world::SectorId> inRect;
    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const auto& sector = map.sectors[si];
        // Include sector if any vertex lies inside the rectangle.
        for (const auto& wall : sector.walls)
        {
            if (wall.p0.x >= minCorner.x && wall.p0.x <= maxCorner.x &&
                wall.p0.y >= minCorner.y && wall.p0.y <= maxCorner.y)
            {
                inRect.push_back(static_cast<world::SectorId>(si));
                break;
            }
        }
    }

    if (inRect.empty())
    {
        sel.clear();
    }
    else
    {
        sel.clear();
        sel.type    = SelectionType::Sector;
        sel.sectors = std::move(inRect);
    }
}

// ─── hitTestSector ────────────────────────────────────────────────────────────

world::SectorId SelectTool::hitTestSector(const world::WorldMapData& map,
                                           float mapX, float mapZ) const noexcept
{
    const glm::vec2 p{mapX, mapZ};

    // Test sectors in reverse order (top-most drawn sector wins).
    for (std::size_t i = map.sectors.size(); i > 0; --i)
    {
        const std::size_t    si     = i - 1;
        const world::Sector& sector = map.sectors[si];
        const std::size_t    n      = sector.walls.size();
        if (n < 3) continue;

        std::vector<glm::vec2> verts;
        verts.reserve(n);
        for (const auto& wall : sector.walls)
            verts.push_back(wall.p0);

        if (geometry::pointInPolygon(p, verts))
            return static_cast<world::SectorId>(si);
    }
    return world::INVALID_SECTOR_ID;
}

} // namespace daedalus::editor
