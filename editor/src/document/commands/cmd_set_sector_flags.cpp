#include "cmd_set_sector_flags.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetSectorFlags::CmdSetSectorFlags(EditMapDocument&   doc,
                                     world::SectorId    sectorId,
                                     world::SectorFlags newFlags)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_newFlags(newFlags)
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
        m_oldFlags = sectors[m_sectorId].flags;
}

void CmdSetSectorFlags::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
        sectors[m_sectorId].flags = m_newFlags;
}

void CmdSetSectorFlags::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
        sectors[m_sectorId].flags = m_oldFlags;
}

} // namespace daedalus::editor
