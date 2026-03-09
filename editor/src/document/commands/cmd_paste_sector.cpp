#include "cmd_paste_sector.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/world/map_data.h"

namespace daedalus::editor
{

CmdPasteSector::CmdPasteSector(EditMapDocument&     doc,
                                const world::Sector& source,
                                glm::vec2            offset)
    : m_doc(doc)
    , m_copy(source)
{
    for (auto& wall : m_copy.walls)
        wall.p0 += offset;
    // Portal links are already cleared by EditMapDocument::copySector();
    // assert here just to be safe.
    for (auto& wall : m_copy.walls)
        wall.portalSectorId = world::INVALID_SECTOR_ID;
}

// ─── execute ──────────────────────────────────────────────────────────────────

void CmdPasteSector::execute()
{
    auto& sectors = m_doc.mapData().sectors;

    if (!m_executed)
    {
        // First execute: append and record the assigned index.
        m_insertedId = static_cast<world::SectorId>(sectors.size());
        sectors.push_back(m_copy);
        m_executed = true;
    }
    else
    {
        // Redo: re-insert at the same position.
        sectors.insert(sectors.begin() +
                           static_cast<std::ptrdiff_t>(m_insertedId),
                       m_copy);
    }

    m_doc.selection().clear();
    m_doc.selection().type = SelectionType::Sector;
    m_doc.selection().sectors.push_back(m_insertedId);
    m_doc.markDirty();
}

// ─── undo ─────────────────────────────────────────────────────────────────────

void CmdPasteSector::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_insertedId >= static_cast<world::SectorId>(sectors.size())) return;

    sectors.erase(sectors.begin() +
                      static_cast<std::ptrdiff_t>(m_insertedId));
    m_doc.selection().clear();
    m_doc.markDirty();
}

} // namespace daedalus::editor
