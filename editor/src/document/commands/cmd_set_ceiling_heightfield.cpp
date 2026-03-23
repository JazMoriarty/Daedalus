#include "cmd_set_ceiling_heightfield.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetCeilingHeightfield::CmdSetCeilingHeightfield(EditMapDocument&                          doc,
                                                   world::SectorId                           sectorId,
                                                   std::optional<world::HeightfieldFloor>    newData)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_newData(std::move(newData))
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        m_oldData = sectors[m_sectorId].ceilHeightfield;
    }
}

void CmdSetCeilingHeightfield::applyData(const std::optional<world::HeightfieldFloor>& data)
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= sectors.size()) return;
    auto& sec = sectors[m_sectorId];
    sec.ceilHeightfield = data;
    m_doc.markDirty();
}

void CmdSetCeilingHeightfield::execute()
{
    applyData(m_newData);
}

void CmdSetCeilingHeightfield::undo()
{
    applyData(m_oldData);
}

} // namespace daedalus::editor
