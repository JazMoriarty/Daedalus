#include "cmd_place_light.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdPlaceLight::CmdPlaceLight(EditMapDocument& doc, LightDef light)
    : m_doc(doc)
    , m_light(std::move(light))
    , m_insertIdx(0)
{}

void CmdPlaceLight::execute()
{
    m_insertIdx = m_doc.lights().size();
    m_doc.lights().push_back(m_light);

    // Select the newly placed light.
    SelectionState& sel = m_doc.selection();
    sel.clear();
    sel.type       = SelectionType::Light;
    sel.lightIndex = m_insertIdx;
}

void CmdPlaceLight::undo()
{
    if (m_insertIdx < m_doc.lights().size())
        m_doc.lights().erase(m_doc.lights().begin() +
                             static_cast<std::ptrdiff_t>(m_insertIdx));

    SelectionState& sel = m_doc.selection();
    if (sel.type == SelectionType::Light && sel.lightIndex == m_insertIdx)
        sel.clear();
}

} // namespace daedalus::editor
