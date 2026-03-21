#include "vertex_tool.h"
#include "document/commands/cmd_move_vertex.h"
#include "daedalus/editor/compound_command.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

#include <algorithm>
#include <memory>

namespace daedalus::editor
{

// ─── onMouseDown ──────────────────────────────────────────────────────────────

void VertexTool::onMouseDown(EditMapDocument& doc,
                              float mapX, float mapZ,
                              int   button)
{
    if (button != 0) return;

    const glm::vec2            p{mapX, mapZ};
    const world::WorldMapData& map = doc.mapData();

    float           bestDistSq = k_vertexRadius * k_vertexRadius;
    world::SectorId bestSector = world::INVALID_SECTOR_ID;
    std::size_t     bestWall   = 0;

    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const auto& sector = map.sectors[si];
        for (std::size_t wi = 0; wi < sector.walls.size(); ++wi)
        {
            const glm::vec2 d      = sector.walls[wi].p0 - p;
            const float     distSq = glm::dot(d, d);
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestSector = static_cast<world::SectorId>(si);
                bestWall   = wi;
            }
        }
    }

    if (bestSector == world::INVALID_SECTOR_ID)
    {
        // Click on empty space: clear the selection (same as SelectTool).
        // If the user then drags a rectangle, onRectSelect will populate
        // a new selection; if it is a trivial click the selection stays empty.
        doc.selection().clear();
        return;
    }

    auto& sel = doc.selection();

    // If the clicked vertex is part of an existing multi-vertex selection,
    // preserve the whole group and start a grouped drag rather than
    // replacing the selection with just this one vertex.
    const bool inMultiSel =
        sel.uniformType() == SelectionType::Vertex &&
        sel.items.size() > 1 &&
        sel.isVertexSelected(bestSector, bestWall);

    if (inMultiSel)
    {
        // Capture original positions for all selected vertices.
        m_multiOrig.clear();
        m_multiOrig.reserve(sel.items.size());
        for (const auto& item : sel.items)
        {
            if (item.sectorId < static_cast<world::SectorId>(map.sectors.size()) &&
                item.index < map.sectors[item.sectorId].walls.size())
            {
                m_multiOrig.push_back({
                    item.sectorId, item.index,
                    map.sectors[item.sectorId].walls[item.index].p0
                });
            }
        }
        m_multiDrag          = true;
        m_multiDragClickPos  = {mapX, mapZ};
        m_multiDragFirstMove = true;
        m_multiDragMoved     = false;
    }
    else
    {
        // Single vertex: replace selection and begin single drag.
        sel.clear();
        sel.items.push_back({SelectionType::Vertex, bestSector, bestWall});

        m_dragging      = true;
        m_dragSectorId  = bestSector;
        m_dragWallIndex = bestWall;
        m_dragOrigPos   = map.sectors[bestSector].walls[bestWall].p0;
        m_dragMoved     = false;
    }
}

// ─── onMouseMove ──────────────────────────────────────────────────────────────

void VertexTool::onMouseMove(EditMapDocument& doc,
                              float mapX, float mapZ)
{
    if (m_multiDrag)
    {
        // Align anchor to first grid-snapped position.
        if (m_multiDragFirstMove)
        {
            m_multiDragClickPos  = {mapX, mapZ};
            m_multiDragFirstMove = false;
        }

        const glm::vec2 delta = glm::vec2{mapX, mapZ} - m_multiDragClickPos;
        auto& sectors = doc.mapData().sectors;
        for (const auto& orig : m_multiOrig)
        {
            if (orig.sectorId < static_cast<world::SectorId>(sectors.size()) &&
                orig.wallIndex < sectors[orig.sectorId].walls.size())
            {
                sectors[orig.sectorId].walls[orig.wallIndex].p0 = orig.origPos + delta;
            }
        }
        doc.markDirty();
        m_multiDragMoved = true;
        return;
    }

    if (!m_dragging) return;
    if (m_dragSectorId >= doc.mapData().sectors.size()) return;

    auto& sector = doc.mapData().sectors[m_dragSectorId];
    if (m_dragWallIndex >= sector.walls.size()) return;

    sector.walls[m_dragWallIndex].p0 = {mapX, mapZ};
    doc.markDirty();
    m_dragMoved = true;
}

// ─── onMouseUp ────────────────────────────────────────────────────────────────

void VertexTool::onMouseUp(EditMapDocument& doc,
                            float mapX, float mapZ,
                            int   button)
{
    if (button != 0) return;

    if (m_multiDrag)
    {
        m_multiDrag = false;
        if (m_multiDragMoved)
        {
            auto& sectors = doc.mapData().sectors;
            std::vector<std::unique_ptr<ICommand>> steps;
            for (const auto& orig : m_multiOrig)
            {
                if (orig.sectorId >= static_cast<world::SectorId>(sectors.size())) continue;
                if (orig.wallIndex >= sectors[orig.sectorId].walls.size()) continue;
                const glm::vec2 finalPos = sectors[orig.sectorId].walls[orig.wallIndex].p0;
                if (finalPos == orig.origPos) continue;
                steps.push_back(std::make_unique<CmdMoveVertex>(
                    doc, orig.sectorId, orig.wallIndex, orig.origPos, finalPos));
            }
            if (!steps.empty())
                doc.pushCommand(std::make_unique<CompoundCommand>(
                    "Move Vertices", std::move(steps)));
            // Selection stays as-is (all dragged vertices remain selected).
        }
        (void)mapX; (void)mapZ;
        return;
    }

    if (!m_dragging) return;
    m_dragging = false;

    if (!m_dragMoved) return;
    if (m_dragSectorId >= doc.mapData().sectors.size()) return;

    auto& sector = doc.mapData().sectors[m_dragSectorId];
    if (m_dragWallIndex >= sector.walls.size()) return;

    const glm::vec2 finalPos = sector.walls[m_dragWallIndex].p0;
    doc.pushCommand(std::make_unique<CmdMoveVertex>(
        doc, m_dragSectorId, m_dragWallIndex, m_dragOrigPos, finalPos));

    // Keep vertex selected after the drag.
    auto& sel = doc.selection();
    sel.clear();
    sel.items.push_back({SelectionType::Vertex, m_dragSectorId, m_dragWallIndex});

    (void)mapX; (void)mapZ;
}

// ─── onRectSelect ────────────────────────────────────────────────────────────────────

void VertexTool::onRectSelect(EditMapDocument& doc,
                               glm::vec2        minCorner,
                               glm::vec2        maxCorner)
{
    const world::WorldMapData& map = doc.mapData();
    auto& sel = doc.selection();
    sel.clear();

    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const auto& sector = map.sectors[si];
        for (std::size_t wi = 0; wi < sector.walls.size(); ++wi)
        {
            const glm::vec2& p = sector.walls[wi].p0;
            if (p.x >= minCorner.x && p.x <= maxCorner.x &&
                p.y >= minCorner.y && p.y <= maxCorner.y)
            {
                sel.items.push_back({SelectionType::Vertex,
                                     static_cast<world::SectorId>(si), wi});
            }
        }
    }
}

} // namespace daedalus::editor
