#include "cmd_set_light_props.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetLightProps::CmdSetLightProps(EditMapDocument& doc,
                                   std::size_t      lightIndex,
                                   LightDef         oldDef,
                                   LightDef         newDef)
    : m_doc(doc)
    , m_idx(lightIndex)
    , m_oldDef(std::move(oldDef))
    , m_newDef(std::move(newDef))
{}

void CmdSetLightProps::execute()
{
    if (m_idx >= m_doc.lights().size()) return;
    m_doc.lights()[m_idx] = m_newDef;
}

void CmdSetLightProps::undo()
{
    if (m_idx >= m_doc.lights().size()) return;
    m_doc.lights()[m_idx] = m_oldDef;
}

} // namespace daedalus::editor
