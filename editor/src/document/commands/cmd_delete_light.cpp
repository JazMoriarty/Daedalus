#include "cmd_delete_light.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdDeleteLight::CmdDeleteLight(EditMapDocument& doc, std::size_t lightIndex)
    : m_doc(doc)
    , m_idx(lightIndex)
{}

void CmdDeleteLight::execute()
{
    auto& lights = m_doc.lights();
    if (m_idx >= lights.size()) return;

    if (!m_executed)
    {
        m_stored   = lights[m_idx];
        m_executed = true;
    }

    lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(m_idx));

    // Clear selection if the deleted light was selected.
    SelectionState& sel = m_doc.selection();
    if (sel.type == SelectionType::Light && sel.lightIndex == m_idx)
        sel.clear();
}

void CmdDeleteLight::undo()
{
    auto& lights = m_doc.lights();
    const std::size_t insertAt = std::min(m_idx, lights.size());
    lights.insert(lights.begin() + static_cast<std::ptrdiff_t>(insertAt), m_stored);

    // Re-select the restored light.
    SelectionState& sel = m_doc.selection();
    sel.clear();
    sel.type       = SelectionType::Light;
    sel.lightIndex = insertAt;
}

} // namespace daedalus::editor
