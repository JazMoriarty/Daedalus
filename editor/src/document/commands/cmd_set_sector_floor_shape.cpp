#include "cmd_set_sector_floor_shape.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetSectorFloorShape::CmdSetSectorFloorShape(EditMapDocument&                    doc,
                                               world::SectorId                     sectorId,
                                               world::FloorShape                   newShape,
                                               std::optional<world::StairProfile>  newProfile)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_newShape(newShape)
    , m_newProfile(newProfile)
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        m_oldShape   = sectors[m_sectorId].floorShape;
        m_oldProfile = sectors[m_sectorId].stairProfile;
    }
}

void CmdSetSectorFloorShape::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        sectors[m_sectorId].floorShape   = m_newShape;
        sectors[m_sectorId].stairProfile = m_newProfile;
        m_doc.markDirty();
    }
}

void CmdSetSectorFloorShape::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        sectors[m_sectorId].floorShape   = m_oldShape;
        sectors[m_sectorId].stairProfile = m_oldProfile;
        m_doc.markDirty();
    }
}

} // namespace daedalus::editor
