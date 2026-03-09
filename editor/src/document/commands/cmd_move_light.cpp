#include "cmd_move_light.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdMoveLight::CmdMoveLight(EditMapDocument& doc,
                             std::size_t      lightIndex,
                             glm::vec3        oldPosition,
                             glm::vec3        newPosition)
    : m_doc(doc)
    , m_idx(lightIndex)
    , m_oldPos(oldPosition)
    , m_newPos(newPosition)
{}

void CmdMoveLight::execute()
{
    if (m_idx < m_doc.lights().size())
        m_doc.lights()[m_idx].position = m_newPos;
}

void CmdMoveLight::undo()
{
    if (m_idx < m_doc.lights().size())
        m_doc.lights()[m_idx].position = m_oldPos;
}

} // namespace daedalus::editor
