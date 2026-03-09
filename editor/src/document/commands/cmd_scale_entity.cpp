#include "cmd_scale_entity.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdScaleEntity::CmdScaleEntity(EditMapDocument& doc,
                               std::size_t      entityIndex,
                               glm::vec3        oldScale,
                               glm::vec3        newScale)
    : m_doc(doc)
    , m_idx(entityIndex)
    , m_oldScale(oldScale)
    , m_newScale(newScale)
{}

void CmdScaleEntity::execute()
{
    if (m_idx >= m_doc.entities().size()) return;
    m_doc.entities()[m_idx].scale = m_newScale;
    m_doc.markEntityDirty();
}

void CmdScaleEntity::undo()
{
    if (m_idx >= m_doc.entities().size()) return;
    m_doc.entities()[m_idx].scale = m_oldScale;
    m_doc.markEntityDirty();
}

} // namespace daedalus::editor
