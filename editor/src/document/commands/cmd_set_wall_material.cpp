#include "cmd_set_wall_material.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetWallMaterial::CmdSetWallMaterial(EditMapDocument& doc,
                                       world::SectorId  sectorId,
                                       std::size_t      wallIndex,
                                       WallSurface      surface,
                                       UUID             newId)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_wallIndex(wallIndex)
    , m_surface(surface)
    , m_newId(newId)
{
    const UUID* id = const_cast<CmdSetWallMaterial*>(this)->targetId();
    if (id) m_oldId = *id;
}

UUID* CmdSetWallMaterial::targetId() noexcept
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= sectors.size()) return nullptr;
    auto& walls = sectors[m_sectorId].walls;
    if (m_wallIndex >= walls.size()) return nullptr;
    world::Wall& w = walls[m_wallIndex];
    switch (m_surface)
    {
    case WallSurface::Front: return &w.frontMaterialId;
    case WallSurface::Upper: return &w.upperMaterialId;
    case WallSurface::Lower: return &w.lowerMaterialId;
    case WallSurface::Back:  return &w.backMaterialId;
    }
    return nullptr;
}

void CmdSetWallMaterial::execute()
{
    if (UUID* id = targetId())
    {
        *id = m_newId;
        m_doc.markDirty();
    }
}

void CmdSetWallMaterial::undo()
{
    if (UUID* id = targetId())
    {
        *id = m_oldId;
        m_doc.markDirty();
    }
}

} // namespace daedalus::editor
