#include "cmd_set_floor_portal.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetFloorPortal::CmdSetFloorPortal(EditMapDocument&  doc,
                                     world::SectorId  sectorId,
                                     HPortalSurface   surface,
                                     world::SectorId  newTargetId,
                                     UUID             newMaterialId)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_surface(surface)
    , m_newTargetId(newTargetId)
    , m_newMaterialId(newMaterialId)
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        const auto& sec = sectors[m_sectorId];
        if (m_surface == HPortalSurface::Floor)
        {
            m_oldTargetId   = sec.floorPortalSectorId;
            m_oldMaterialId = sec.floorPortalMaterialId;
        }
        else
        {
            m_oldTargetId   = sec.ceilPortalSectorId;
            m_oldMaterialId = sec.ceilPortalMaterialId;
        }
    }
}

void CmdSetFloorPortal::apply(world::SectorId targetId, UUID materialId)
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= sectors.size()) return;
    auto& sec = sectors[m_sectorId];
    if (m_surface == HPortalSurface::Floor)
    {
        sec.floorPortalSectorId   = targetId;
        sec.floorPortalMaterialId = materialId;
    }
    else
    {
        sec.ceilPortalSectorId   = targetId;
        sec.ceilPortalMaterialId = materialId;
    }
    m_doc.markDirty();
}

void CmdSetFloorPortal::execute()
{
    apply(m_newTargetId, m_newMaterialId);
}

void CmdSetFloorPortal::undo()
{
    apply(m_oldTargetId, m_oldMaterialId);
}

} // namespace daedalus::editor
