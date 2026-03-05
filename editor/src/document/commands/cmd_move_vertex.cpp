#include "cmd_move_vertex.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

#include "daedalus/core/assert.h"

namespace daedalus::editor
{

CmdMoveVertex::CmdMoveVertex(EditMapDocument& doc,
                              world::SectorId  sectorId,
                              std::size_t      wallIndex,
                              glm::vec2        oldPos,
                              glm::vec2        newPos)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_wallIndex(wallIndex)
    , m_oldPos(oldPos)
    , m_newPos(newPos)
{}

void CmdMoveVertex::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    DAEDALUS_ASSERT(m_sectorId < sectors.size(), "CmdMoveVertex: invalid sector id");
    auto& walls = sectors[m_sectorId].walls;
    DAEDALUS_ASSERT(m_wallIndex < walls.size(), "CmdMoveVertex: invalid wall index");
    walls[m_wallIndex].p0 = m_newPos;
}

void CmdMoveVertex::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    DAEDALUS_ASSERT(m_sectorId < sectors.size(), "CmdMoveVertex::undo: invalid sector id");
    auto& walls = sectors[m_sectorId].walls;
    DAEDALUS_ASSERT(m_wallIndex < walls.size(), "CmdMoveVertex::undo: invalid wall index");
    walls[m_wallIndex].p0 = m_oldPos;
}

} // namespace daedalus::editor
