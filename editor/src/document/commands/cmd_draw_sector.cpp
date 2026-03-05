#include "cmd_draw_sector.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdDrawSector::CmdDrawSector(EditMapDocument& doc, world::Sector sector)
    : m_doc(doc), m_sector(std::move(sector))
{}

void CmdDrawSector::execute()
{
    m_insertedIndex = m_doc.mapData().sectors.size();
    m_doc.mapData().sectors.push_back(m_sector);
}

void CmdDrawSector::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_insertedIndex < sectors.size())
        sectors.erase(sectors.begin() + static_cast<std::ptrdiff_t>(m_insertedIndex));
}

} // namespace daedalus::editor
