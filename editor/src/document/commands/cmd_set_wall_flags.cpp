#include "cmd_set_wall_flags.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetWallFlags::CmdSetWallFlags(EditMapDocument& doc,
                                  world::SectorId  sectorId,
                                  std::size_t      wallIndex,
                                  world::WallFlags newFlags)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_wallIndex(wallIndex)
    , m_newFlags(newFlags)
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() &&
        m_wallIndex < sectors[m_sectorId].walls.size())
    {
        m_oldFlags = sectors[m_sectorId].walls[m_wallIndex].flags;
    }
}

void CmdSetWallFlags::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() &&
        m_wallIndex < sectors[m_sectorId].walls.size())
    {
        sectors[m_sectorId].walls[m_wallIndex].flags = m_newFlags;
    }
}

void CmdSetWallFlags::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() &&
        m_wallIndex < sectors[m_sectorId].walls.size())
    {
        sectors[m_sectorId].walls[m_wallIndex].flags = m_oldFlags;
    }
}

} // namespace daedalus::editor
