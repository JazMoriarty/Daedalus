#include "cmd_set_heightfield.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetHeightfield::CmdSetHeightfield(EditMapDocument&                          doc,
                                     world::SectorId                           sectorId,
                                     std::optional<world::HeightfieldFloor>    newData)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_newData(std::move(newData))
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        m_oldData  = sectors[m_sectorId].heightfield;
        m_oldFlags = sectors[m_sectorId].flags;
    }
}

void CmdSetHeightfield::applyData(const std::optional<world::HeightfieldFloor>& data,
                                  world::SectorFlags                            flags)
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= sectors.size()) return;
    auto& sec    = sectors[m_sectorId];
    sec.heightfield = data;
    sec.flags       = flags;
    m_doc.markDirty();
}

void CmdSetHeightfield::execute()
{
    // Compute new flags: set or clear HasHeightfield according to whether
    // the new data is present.
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= sectors.size()) return;

    world::SectorFlags newFlags = sectors[m_sectorId].flags;
    if (m_newData.has_value())
    {
        newFlags = newFlags | world::SectorFlags::HasHeightfield;
    }
    else
    {
        newFlags = static_cast<world::SectorFlags>(
            static_cast<unsigned>(newFlags) &
            ~static_cast<unsigned>(world::SectorFlags::HasHeightfield));
    }
    applyData(m_newData, newFlags);
}

void CmdSetHeightfield::undo()
{
    applyData(m_oldData, m_oldFlags);
}

} // namespace daedalus::editor
