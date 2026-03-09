#include "cmd_set_sector_material.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetSectorMaterial::CmdSetSectorMaterial(EditMapDocument& doc,
                                           world::SectorId  sectorId,
                                           SectorSurface    surface,
                                           UUID             newId)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_surface(surface)
    , m_newId(newId)
{
    const UUID* id = const_cast<CmdSetSectorMaterial*>(this)->targetId();
    if (id) m_oldId = *id;
}

UUID* CmdSetSectorMaterial::targetId() noexcept
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= sectors.size()) return nullptr;
    world::Sector& s = sectors[m_sectorId];
    switch (m_surface)
    {
    case SectorSurface::Floor: return &s.floorMaterialId;
    case SectorSurface::Ceil:  return &s.ceilMaterialId;
    }
    return nullptr;
}

void CmdSetSectorMaterial::execute()
{
    if (UUID* id = targetId())
    {
        *id = m_newId;
        m_doc.markDirty();
    }
}

void CmdSetSectorMaterial::undo()
{
    if (UUID* id = targetId())
    {
        *id = m_oldId;
        m_doc.markDirty();
    }
}

} // namespace daedalus::editor
