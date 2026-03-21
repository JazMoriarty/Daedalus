#include "cmd_remove_detail_brush.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdRemoveDetailBrush::CmdRemoveDetailBrush(EditMapDocument& doc,
                                           world::SectorId  sectorId,
                                           std::size_t      brushIndex)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_brushIndex(brushIndex)
{
}

void CmdRemoveDetailBrush::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= sectors.size()) return;
    auto& details = sectors[m_sectorId].details;
    if (m_brushIndex >= details.size()) return;

    // Capture for undo before erasing.
    m_savedBrush = details[m_brushIndex];
    details.erase(details.begin() + static_cast<std::ptrdiff_t>(m_brushIndex));
    m_doc.markDirty();
}

void CmdRemoveDetailBrush::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId >= sectors.size()) return;
    auto& details = sectors[m_sectorId].details;

    // Re-insert at the original index (clamped to end if vector is shorter).
    const std::size_t insertAt = std::min(m_brushIndex, details.size());
    details.insert(details.begin() + static_cast<std::ptrdiff_t>(insertAt), m_savedBrush);
    m_doc.markDirty();
}

} // namespace daedalus::editor
