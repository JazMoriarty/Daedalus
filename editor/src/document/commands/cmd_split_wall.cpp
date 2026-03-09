#include "cmd_split_wall.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/world/map_data.h"

#include <format>

namespace daedalus::editor
{

CmdSplitWall::CmdSplitWall(EditMapDocument& doc,
                             world::SectorId  sectorId,
                             std::size_t      wallIndex,
                             glm::vec2        splitPoint)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_wallIndex(wallIndex)
{
    const auto& sectors = doc.mapData().sectors;
    if (sectorId >= static_cast<world::SectorId>(sectors.size())) return;

    const auto& sector = sectors[sectorId];
    if (wallIndex >= sector.walls.size()) return;

    const std::size_t n = sector.walls.size();
    m_origWall = sector.walls[wallIndex];

    // Use provided split point, or fall back to midpoint.
    if (splitPoint.x == k_useMidpoint && splitPoint.y == k_useMidpoint)
    {
        m_splitPoint = (sector.walls[wallIndex].p0 +
                        sector.walls[(wallIndex + 1) % n].p0) * 0.5f;
    }
    else
    {
        m_splitPoint = splitPoint;
    }
}

// ─── execute ──────────────────────────────────────────────────────────────────

void CmdSplitWall::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= static_cast<world::SectorId>(sectors.size())) return;

    auto& sector = sectors[m_sectorId];
    if (m_wallIndex >= sector.walls.size()) return;

    // Clear portal on the original wall (geometry no longer matches partner).
    sector.walls[m_wallIndex].portalSectorId = world::INVALID_SECTOR_ID;

    // Build the new wall: copy all properties from the original (including
    // material IDs and UV data), then override p0 and clear portal.
    world::Wall newWall          = m_origWall;
    newWall.p0                   = m_splitPoint;
    newWall.portalSectorId       = world::INVALID_SECTOR_ID;

    sector.walls.insert(
        sector.walls.begin() + static_cast<std::ptrdiff_t>(m_wallIndex + 1),
        newWall);

    // Select the new midpoint vertex so the user can drag-refine immediately.
    auto& sel           = m_doc.selection();
    sel.clear();
    sel.type            = SelectionType::Vertex;
    sel.vertexSectorId  = m_sectorId;
    sel.vertexWallIndex = m_wallIndex + 1;

    m_doc.markDirty();
    m_doc.log(std::format("Split wall {} of sector {} at ({:.2f}, {:.2f}).",
                           m_wallIndex, m_sectorId, m_splitPoint.x, m_splitPoint.y));
    m_executed = true;
}

// ─── undo ─────────────────────────────────────────────────────────────────────

void CmdSplitWall::undo()
{
    if (!m_executed) return;

    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= static_cast<world::SectorId>(sectors.size())) return;

    auto& sector = sectors[m_sectorId];
    if (m_wallIndex + 1 >= sector.walls.size()) return;

    // Remove the inserted midpoint wall.
    sector.walls.erase(
        sector.walls.begin() + static_cast<std::ptrdiff_t>(m_wallIndex + 1));

    // Restore the original wall (including its portal link).
    sector.walls[m_wallIndex] = m_origWall;

    // Restore wall selection.
    auto& sel        = m_doc.selection();
    sel.clear();
    sel.type         = SelectionType::Wall;
    sel.wallSectorId = m_sectorId;
    sel.wallIndex    = m_wallIndex;

    m_doc.markDirty();
}

} // namespace daedalus::editor
