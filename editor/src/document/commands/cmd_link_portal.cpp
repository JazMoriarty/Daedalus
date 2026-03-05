#include "cmd_link_portal.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

#include "daedalus/core/assert.h"

namespace daedalus::editor
{

CmdLinkPortal::CmdLinkPortal(EditMapDocument& doc,
                              world::SectorId  sectorA, std::size_t wallIdxA,
                              world::SectorId  sectorB, std::size_t wallIdxB)
    : m_doc(doc)
    , m_sectorA(sectorA)
    , m_wallIdxA(wallIdxA)
    , m_sectorB(sectorB)
    , m_wallIdxB(wallIdxB)
{
    // Snapshot current state for undo.
    auto& sectors = doc.mapData().sectors;
    DAEDALUS_ASSERT(sectorA < sectors.size(), "CmdLinkPortal: invalid sectorA");
    DAEDALUS_ASSERT(sectorB < sectors.size(), "CmdLinkPortal: invalid sectorB");
    m_prevPortalA = sectors[sectorA].walls[wallIdxA].portalSectorId;
    m_prevPortalB = sectors[sectorB].walls[wallIdxB].portalSectorId;
}

void CmdLinkPortal::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    sectors[m_sectorA].walls[m_wallIdxA].portalSectorId = m_sectorB;
    sectors[m_sectorB].walls[m_wallIdxB].portalSectorId = m_sectorA;
}

void CmdLinkPortal::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    sectors[m_sectorA].walls[m_wallIdxA].portalSectorId = m_prevPortalA;
    sectors[m_sectorB].walls[m_wallIdxB].portalSectorId = m_prevPortalB;
}

} // namespace daedalus::editor
