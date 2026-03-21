#include "cmd_set_sector_surface_uv.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

namespace daedalus::editor
{

CmdSetSectorSurfaceUV::CmdSetSectorSurfaceUV(EditMapDocument& doc,
                                              world::SectorId  sectorId,
                                              SectorSurface    surface,
                                              glm::vec2        newOffset,
                                              glm::vec2        newScale,
                                              float            newRotation)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_surface(surface)
    , m_newOffset(newOffset)
    , m_newScale(newScale)
    , m_newRotation(newRotation)
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= static_cast<world::SectorId>(sectors.size())) return;
    const auto& sec = sectors[m_sectorId];
    if (m_surface == SectorSurface::Floor)
    {
        m_oldOffset   = sec.floorUvOffset;
        m_oldScale    = sec.floorUvScale;
        m_oldRotation = sec.floorUvRotation;
    }
    else
    {
        m_oldOffset   = sec.ceilUvOffset;
        m_oldScale    = sec.ceilUvScale;
        m_oldRotation = sec.ceilUvRotation;
    }
}

void CmdSetSectorSurfaceUV::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= static_cast<world::SectorId>(sectors.size())) return;
    auto& sec = sectors[m_sectorId];
    if (m_surface == SectorSurface::Floor)
    {
        sec.floorUvOffset   = m_newOffset;
        sec.floorUvScale    = m_newScale;
        sec.floorUvRotation = m_newRotation;
    }
    else
    {
        sec.ceilUvOffset   = m_newOffset;
        sec.ceilUvScale    = m_newScale;
        sec.ceilUvRotation = m_newRotation;
    }
    m_doc.markDirty();
}

void CmdSetSectorSurfaceUV::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= static_cast<world::SectorId>(sectors.size())) return;
    auto& sec = sectors[m_sectorId];
    if (m_surface == SectorSurface::Floor)
    {
        sec.floorUvOffset   = m_oldOffset;
        sec.floorUvScale    = m_oldScale;
        sec.floorUvRotation = m_oldRotation;
    }
    else
    {
        sec.ceilUvOffset   = m_oldOffset;
        sec.ceilUvScale    = m_oldScale;
        sec.ceilUvRotation = m_oldRotation;
    }
    m_doc.markDirty();
}

} // namespace daedalus::editor
