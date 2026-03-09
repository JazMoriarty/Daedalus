#include "cmd_set_entity_props.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetEntityProps::CmdSetEntityProps(EditMapDocument& doc,
                                     std::size_t      entityIndex,
                                     EntityDef        oldDef,
                                     EntityDef        newDef)
    : m_doc(doc)
    , m_idx(entityIndex)
    , m_oldDef(std::move(oldDef))
    , m_newDef(std::move(newDef))
{}

void CmdSetEntityProps::execute()
{
    if (m_idx >= m_doc.entities().size()) return;
    m_doc.entities()[m_idx] = m_newDef;
    m_doc.markEntityDirty();
}

void CmdSetEntityProps::undo()
{
    if (m_idx >= m_doc.entities().size()) return;
    m_doc.entities()[m_idx] = m_oldDef;
    m_doc.markEntityDirty();
}

} // namespace daedalus::editor
