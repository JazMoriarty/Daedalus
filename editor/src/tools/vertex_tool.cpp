#include "vertex_tool.h"
#include "document/commands/cmd_move_vertex.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

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
        return;  // No vertex near the click — selection unchanged.

    // Select the vertex.
    auto& sel = doc.selection();
    sel.clear();
    sel.items.push_back({SelectionType::Vertex, bestSector, bestWall});

    // Begin drag.
    m_dragging      = true;
    m_dragSectorId  = bestSector;
    m_dragWallIndex = bestWall;
    m_dragOrigPos   = map.sectors[bestSector].walls[bestWall].p0;
    m_dragMoved     = false;
}

// ─── onMouseMove ──────────────────────────────────────────────────────────────

void VertexTool::onMouseMove(EditMapDocument& doc,
                              float mapX, float mapZ)
{
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
    if (button != 0 || !m_dragging) return;
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

} // namespace daedalus::editor
