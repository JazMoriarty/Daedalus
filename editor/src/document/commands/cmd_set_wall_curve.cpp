#include "cmd_set_wall_curve.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

namespace daedalus::editor
{

CmdSetWallCurve::CmdSetWallCurve(EditMapDocument&                  doc,
                                 world::SectorId                   sectorId,
                                 std::size_t                       wallIndex,
                                 std::optional<glm::vec2>          newControlA,
                                 std::optional<glm::vec2>          newControlB,
                                 uint32_t                          newSubdivisions)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_wallIndex(wallIndex)
    , m_newControlA(newControlA)
    , m_newControlB(newControlB)
    , m_newSubdivisions(newSubdivisions)
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() && m_wallIndex < sectors[m_sectorId].walls.size())
    {
        const auto& wall   = sectors[m_sectorId].walls[m_wallIndex];
        m_oldControlA      = wall.curveControlA;
        m_oldControlB      = wall.curveControlB;
        m_oldSubdivisions  = wall.curveSubdivisions;
    }
}

void CmdSetWallCurve::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() && m_wallIndex < sectors[m_sectorId].walls.size())
    {
        auto& wall            = sectors[m_sectorId].walls[m_wallIndex];
        wall.curveControlA    = m_newControlA;
        wall.curveControlB    = m_newControlB;
        wall.curveSubdivisions = m_newSubdivisions;
        m_doc.markDirty();
    }
}

void CmdSetWallCurve::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() && m_wallIndex < sectors[m_sectorId].walls.size())
    {
        auto& wall            = sectors[m_sectorId].walls[m_wallIndex];
        wall.curveControlA    = m_oldControlA;
        wall.curveControlB    = m_oldControlB;
        wall.curveSubdivisions = m_oldSubdivisions;
        m_doc.markDirty();
    }
}

} // namespace daedalus::editor
