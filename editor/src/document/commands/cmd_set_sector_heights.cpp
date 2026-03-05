#include "cmd_set_sector_heights.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetSectorHeights::CmdSetSectorHeights(EditMapDocument& doc,
                                         world::SectorId  sectorId,
                                         float            newFloor,
                                         float            newCeil)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_newFloor(newFloor)
    , m_newCeil(newCeil)
{
    // Capture old values at construction time.
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        m_oldFloor = sectors[m_sectorId].floorHeight;
        m_oldCeil  = sectors[m_sectorId].ceilHeight;
    }
}

void CmdSetSectorHeights::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        sectors[m_sectorId].floorHeight = m_newFloor;
        sectors[m_sectorId].ceilHeight  = m_newCeil;
    }
}

void CmdSetSectorHeights::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        sectors[m_sectorId].floorHeight = m_oldFloor;
        sectors[m_sectorId].ceilHeight  = m_oldCeil;
    }
}

} // namespace daedalus::editor
