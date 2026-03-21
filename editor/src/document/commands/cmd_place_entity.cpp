#include "cmd_place_entity.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdPlaceEntity::CmdPlaceEntity(EditMapDocument& doc, EntityDef entity)
    : m_doc(doc)
    , m_entity(std::move(entity))
{}

void CmdPlaceEntity::execute()
{
    m_insertIdx = m_doc.entities().size();
    m_doc.entities().push_back(m_entity);
    m_doc.markEntityDirty();

    SelectionState& sel = m_doc.selection();
    sel.clear();
    sel.items.push_back({SelectionType::Entity,
                         world::INVALID_SECTOR_ID, m_insertIdx});
}

void CmdPlaceEntity::undo()
{
    if (m_insertIdx < m_doc.entities().size())
        m_doc.entities().erase(m_doc.entities().begin() +
                               static_cast<std::ptrdiff_t>(m_insertIdx));
    m_doc.markEntityDirty();

    SelectionState& sel = m_doc.selection();
    if (sel.hasSingleOf(SelectionType::Entity) &&
        sel.items[0].index == m_insertIdx)
        sel.clear();
}

} // namespace daedalus::editor
