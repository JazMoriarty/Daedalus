#include "cmd_move_entity.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdMoveEntity::CmdMoveEntity(EditMapDocument& doc,
                             std::size_t      entityIndex,
                             glm::vec3        oldPosition,
                             glm::vec3        newPosition)
    : m_doc(doc)
    , m_idx(entityIndex)
    , m_oldPos(oldPosition)
    , m_newPos(newPosition)
{}

void CmdMoveEntity::execute()
{
    if (m_idx >= m_doc.entities().size()) return;
    m_doc.entities()[m_idx].position = m_newPos;
    m_doc.markEntityDirty();
}

void CmdMoveEntity::undo()
{
    if (m_idx >= m_doc.entities().size()) return;
    m_doc.entities()[m_idx].position = m_oldPos;
    m_doc.markEntityDirty();
}

} // namespace daedalus::editor
