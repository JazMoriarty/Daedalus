#include "cmd_rotate_entity.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdRotateEntity::CmdRotateEntity(EditMapDocument& doc,
                                 std::size_t      entityIndex,
                                 float            oldYaw,
                                 float            newYaw)
    : m_doc(doc)
    , m_idx(entityIndex)
    , m_oldYaw(oldYaw)
    , m_newYaw(newYaw)
{}

void CmdRotateEntity::execute()
{
    if (m_idx >= m_doc.entities().size()) return;
    m_doc.entities()[m_idx].yaw = m_newYaw;
    m_doc.markEntityDirty();
}

void CmdRotateEntity::undo()
{
    if (m_idx >= m_doc.entities().size()) return;
    m_doc.entities()[m_idx].yaw = m_oldYaw;
    m_doc.markEntityDirty();
}

} // namespace daedalus::editor
