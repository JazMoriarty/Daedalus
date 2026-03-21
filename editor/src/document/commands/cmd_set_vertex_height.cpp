#include "cmd_set_vertex_height.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetVertexHeight::CmdSetVertexHeight(EditMapDocument&          doc,
                                       world::SectorId           sectorId,
                                       std::size_t               wallIndex,
                                       std::optional<float>      newFloor,
                                       std::optional<float>      newCeil)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_wallIndex(wallIndex)
    , m_newFloor(newFloor)
    , m_newCeil(newCeil)
{
    // Capture old values at construction time.
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() && m_wallIndex < sectors[m_sectorId].walls.size())
    {
        const auto& wall = sectors[m_sectorId].walls[m_wallIndex];
        m_oldFloor = wall.floorHeightOverride;
        m_oldCeil  = wall.ceilHeightOverride;
    }
}

void CmdSetVertexHeight::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() && m_wallIndex < sectors[m_sectorId].walls.size())
    {
        auto& wall             = sectors[m_sectorId].walls[m_wallIndex];
        wall.floorHeightOverride = m_newFloor;
        wall.ceilHeightOverride  = m_newCeil;
        m_doc.markDirty();
    }
}

void CmdSetVertexHeight::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() && m_wallIndex < sectors[m_sectorId].walls.size())
    {
        auto& wall             = sectors[m_sectorId].walls[m_wallIndex];
        wall.floorHeightOverride = m_oldFloor;
        wall.ceilHeightOverride  = m_oldCeil;
        m_doc.markDirty();
    }
}

} // namespace daedalus::editor
