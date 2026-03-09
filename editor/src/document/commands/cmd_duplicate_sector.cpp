#include "cmd_duplicate_sector.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"

namespace daedalus::editor
{

CmdDuplicateSector::CmdDuplicateSector(EditMapDocument& doc,
                                        world::SectorId  srcId,
                                        glm::vec2        offset)
    : m_doc(doc)
{
    const auto& sectors = m_doc.mapData().sectors;
    if (srcId >= sectors.size()) return;

    // Deep copy the source sector and apply the offset to every wall vertex.
    m_copy = sectors[srcId];
    for (auto& wall : m_copy.walls)
        wall.p0 += offset;

    // Clear any portal links — the copy is not connected to anything yet.
    for (auto& wall : m_copy.walls)
        wall.portalSectorId = world::INVALID_SECTOR_ID;
}

void CmdDuplicateSector::execute()
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

    // Select the newly inserted sector.
    m_doc.selection().clear();
    m_doc.selection().type = SelectionType::Sector;
    m_doc.selection().sectors.push_back(m_insertedId);
}

void CmdDuplicateSector::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_insertedId >= sectors.size()) return;

    sectors.erase(sectors.begin() +
                      static_cast<std::ptrdiff_t>(m_insertedId));
    m_doc.selection().clear();
}

} // namespace daedalus::editor
