#include "cmd_set_wall_uv.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetWallUV::CmdSetWallUV(EditMapDocument& doc,
                            world::SectorId  sectorId,
                            std::size_t      wallIndex,
                            glm::vec2        newOffset,
                            glm::vec2        newScale,
                            float            newRotation)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_wallIndex(wallIndex)
    , m_newOffset(newOffset)
    , m_newScale(newScale)
    , m_newRotation(newRotation)
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() &&
        m_wallIndex < sectors[m_sectorId].walls.size())
    {
        const auto& wall = sectors[m_sectorId].walls[m_wallIndex];
        m_oldOffset   = wall.uvOffset;
        m_oldScale    = wall.uvScale;
        m_oldRotation = wall.uvRotation;
    }
}

void CmdSetWallUV::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() &&
        m_wallIndex < sectors[m_sectorId].walls.size())
    {
        auto& wall     = sectors[m_sectorId].walls[m_wallIndex];
        wall.uvOffset   = m_newOffset;
        wall.uvScale    = m_newScale;
        wall.uvRotation = m_newRotation;
    }
}

void CmdSetWallUV::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() &&
        m_wallIndex < sectors[m_sectorId].walls.size())
    {
        auto& wall     = sectors[m_sectorId].walls[m_wallIndex];
        wall.uvOffset   = m_oldOffset;
        wall.uvScale    = m_oldScale;
        wall.uvRotation = m_oldRotation;
    }
}

} // namespace daedalus::editor
