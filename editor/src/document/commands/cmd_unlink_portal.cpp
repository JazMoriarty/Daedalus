#include "cmd_unlink_portal.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

namespace daedalus::editor
{

CmdUnlinkPortal::CmdUnlinkPortal(EditMapDocument& doc,
                                  world::SectorId  sectorA, std::size_t wallIdxA,
                                  world::SectorId  sectorB, std::size_t wallIdxB)
    : m_doc(doc)
    , m_sectorA(sectorA)
    , m_wallIdxA(wallIdxA)
    , m_sectorB(sectorB)
    , m_wallIdxB(wallIdxB)
{}

void CmdUnlinkPortal::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorA < sectors.size())
        sectors[m_sectorA].walls[m_wallIdxA].portalSectorId = world::INVALID_SECTOR_ID;
    if (m_sectorB < sectors.size())
        sectors[m_sectorB].walls[m_wallIdxB].portalSectorId = world::INVALID_SECTOR_ID;
}

void CmdUnlinkPortal::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorA < sectors.size())
        sectors[m_sectorA].walls[m_wallIdxA].portalSectorId = m_sectorB;
    if (m_sectorB < sectors.size())
        sectors[m_sectorB].walls[m_wallIdxB].portalSectorId = m_sectorA;
}

} // namespace daedalus::editor
