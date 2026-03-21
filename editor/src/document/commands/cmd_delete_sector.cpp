#include "cmd_delete_sector.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

namespace daedalus::editor
{

CmdDeleteSector::CmdDeleteSector(EditMapDocument& doc, world::SectorId sectorId)
    : m_doc(doc)
    , m_sectorId(sectorId)
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= sectors.size()) return;

    // Save a full copy of the sector to be deleted.
    m_savedSector = sectors[m_sectorId];

    // Record portal IDs for every wall (in OTHER sectors) that execute() will
    // modify: those pointing at m_sectorId (cleared) or at a higher index (decremented).
    for (std::size_t si = 0; si < sectors.size(); ++si)
    {
        if (si == m_sectorId) continue;
        for (std::size_t wi = 0; wi < sectors[si].walls.size(); ++wi)
        {
            const world::SectorId p = sectors[si].walls[wi].portalSectorId;
            if (p == m_sectorId ||
                (p != world::INVALID_SECTOR_ID && p > m_sectorId))
            {
                m_savedPortals.push_back({si, wi, p});
            }
        }
    }
}

void CmdDeleteSector::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= sectors.size()) return;

    // 1. Fix portal IDs in every other sector.
    for (std::size_t si = 0; si < sectors.size(); ++si)
    {
        if (si == m_sectorId) continue;
        for (auto& wall : sectors[si].walls)
        {
            if (wall.portalSectorId == m_sectorId)
                wall.portalSectorId = world::INVALID_SECTOR_ID;
            else if (wall.portalSectorId != world::INVALID_SECTOR_ID &&
                     wall.portalSectorId > m_sectorId)
                --wall.portalSectorId;
        }
    }

    // 2. Erase the sector.
    sectors.erase(sectors.begin() + static_cast<std::ptrdiff_t>(m_sectorId));

    // 3. Clear selection and refresh.
    m_doc.selection().clear();
    m_doc.markDirty();
}

void CmdDeleteSector::undo()
{
    auto& sectors = m_doc.mapData().sectors;

    // 1. Re-insert the saved sector at its original index.
    sectors.insert(sectors.begin() + static_cast<std::ptrdiff_t>(m_sectorId),
                   m_savedSector);

    // 2. Restore portal IDs.  After the re-insert all sector indices are back to
    //    their pre-delete values, so m_savedPortals indices remain valid.
    for (const auto& r : m_savedPortals)
        sectors[r.sectorIdx].walls[r.wallIdx].portalSectorId = r.oldPortal;

    // 3. Re-select the restored sector.
    m_doc.selection().clear();
    m_doc.selection().items.push_back(
        {SelectionType::Sector, m_sectorId, 0});
    m_doc.markDirty();
}

} // namespace daedalus::editor
