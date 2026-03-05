#include "select_tool.h"
#include "geometry_utils.h"
#include "document/commands/cmd_move_vertex.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

#include <cmath>
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

    const glm::vec2             p{mapX, mapZ};
    const world::WorldMapData&  map = doc.mapData();
    auto&                       sel = doc.selection();

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

            // Begin drag.
            m_dragging      = true;
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
            return;
        }
    }

    // 3. Sector hit test.
    {
        const world::SectorId hit = hitTestSector(map, mapX, mapZ);
        sel.clear();
        if (hit != world::INVALID_SECTOR_ID)
        {
            sel.type = SelectionType::Sector;
            sel.sectors.push_back(hit);
        }
    }
}

// ─── onMouseMove ──────────────────────────────────────────────────────────────

void SelectTool::onMouseMove(EditMapDocument& doc,
                              float mapX, float mapZ)
{
    if (!m_dragging) return;
    if (m_dragSectorId >= doc.mapData().sectors.size()) return;

    auto& sector = doc.mapData().sectors[m_dragSectorId];
    if (m_dragWallIndex >= sector.walls.size()) return;

    const glm::vec2 newPos{mapX, mapZ};
    sector.walls[m_dragWallIndex].p0 = newPos;
    doc.markDirty();  // live 3D viewport update
    m_dragMoved = true;
}

// ─── onMouseUp ────────────────────────────────────────────────────────────────

void SelectTool::onMouseUp(EditMapDocument& doc,
                            float mapX, float mapZ,
                            int   button)
{
    if (button != 0 || !m_dragging) return;
    m_dragging = false;

    if (!m_dragMoved) return;

    if (m_dragSectorId >= doc.mapData().sectors.size()) return;
    auto& sector = doc.mapData().sectors[m_dragSectorId];
    if (m_dragWallIndex >= sector.walls.size()) return;

    const glm::vec2 finalPos = sector.walls[m_dragWallIndex].p0;

    // Push an undoable command.  CmdMoveVertex::execute() re-applies finalPos
    // (for redo), so it is not a true no-op after the live drag.
    doc.pushCommand(std::make_unique<CmdMoveVertex>(
        doc, m_dragSectorId, m_dragWallIndex, m_dragOrigPos, finalPos));

    // Keep vertex selected.
    auto& sel           = doc.selection();
    sel.type            = SelectionType::Vertex;
    sel.vertexSectorId  = m_dragSectorId;
    sel.vertexWallIndex = m_dragWallIndex;

    (void)mapX; (void)mapZ;
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
