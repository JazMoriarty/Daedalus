#include "cmd_delete_entity.h"
#include "daedalus/editor/edit_map_document.h"

#include <algorithm>

namespace daedalus::editor
{

CmdDeleteEntity::CmdDeleteEntity(EditMapDocument& doc, std::size_t entityIndex)
    : m_doc(doc)
    , m_idx(entityIndex)
{}

void CmdDeleteEntity::execute()
{
    if (m_idx >= m_doc.entities().size()) return;

    m_stored   = m_doc.entities()[m_idx];
    m_executed = true;
    m_doc.entities().erase(m_doc.entities().begin() +
                           static_cast<std::ptrdiff_t>(m_idx));
    m_doc.markEntityDirty();

    SelectionState& sel = m_doc.selection();
    if (sel.hasSingleOf(SelectionType::Entity) && sel.items[0].index == m_idx)
        sel.clear();
}

void CmdDeleteEntity::undo()
{
    if (!m_executed) return;

    const std::size_t insertAt = std::min(m_idx, m_doc.entities().size());
    m_doc.entities().insert(m_doc.entities().begin() +
                            static_cast<std::ptrdiff_t>(insertAt), m_stored);
    m_doc.markEntityDirty();

    SelectionState& sel = m_doc.selection();
    sel.clear();
    sel.items.push_back({SelectionType::Entity,
                         world::INVALID_SECTOR_ID, insertAt});
}

} // namespace daedalus::editor
