#include "cmd_slide_wall.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"
#include "daedalus/core/assert.h"

namespace daedalus::editor
{

CmdSlideWall::CmdSlideWall(EditMapDocument& doc,
                            world::SectorId  sectorId,
                            std::size_t      wallIdx,
                            glm::vec2        origPos1,
                            glm::vec2        newPos1,
                            glm::vec2        origPos2,
                            glm::vec2        newPos2)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_wallIdx(wallIdx)
    , m_origPos1(origPos1)
    , m_newPos1(newPos1)
    , m_origPos2(origPos2)
    , m_newPos2(newPos2)
{}

void CmdSlideWall::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    DAEDALUS_ASSERT(m_sectorId < sectors.size(), "CmdSlideWall: invalid sector id");
    auto& walls = sectors[m_sectorId].walls;
    DAEDALUS_ASSERT(m_wallIdx < walls.size(), "CmdSlideWall: invalid wall index");

    if (!m_executed)
    {
        // First call: live drag already placed vertices at final positions.
        m_executed = true;
    }
    else
    {
        // Redo: restore final vertex positions and re-apply delta to curve control points.
        const std::size_t n     = walls.size();
        walls[m_wallIdx].p0             = m_newPos1;
        walls[(m_wallIdx + 1) % n].p0   = m_newPos2;
        const glm::vec2 delta = m_newPos1 - m_origPos1;
        if (walls[m_wallIdx].curveControlA.has_value()) *walls[m_wallIdx].curveControlA += delta;
        if (walls[m_wallIdx].curveControlB.has_value()) *walls[m_wallIdx].curveControlB += delta;
    }

    m_doc.markDirty();
}

void CmdSlideWall::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    DAEDALUS_ASSERT(m_sectorId < sectors.size(), "CmdSlideWall::undo: invalid sector id");
    auto& walls = sectors[m_sectorId].walls;
    DAEDALUS_ASSERT(m_wallIdx < walls.size(), "CmdSlideWall::undo: invalid wall index");

    const std::size_t n     = walls.size();
    walls[m_wallIdx].p0             = m_origPos1;
    walls[(m_wallIdx + 1) % n].p0   = m_origPos2;
    const glm::vec2 delta = m_newPos1 - m_origPos1;
    if (walls[m_wallIdx].curveControlA.has_value()) *walls[m_wallIdx].curveControlA -= delta;
    if (walls[m_wallIdx].curveControlB.has_value()) *walls[m_wallIdx].curveControlB -= delta;

    m_doc.markDirty();
}

} // namespace daedalus::editor
