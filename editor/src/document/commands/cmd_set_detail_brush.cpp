#include "cmd_set_detail_brush.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetDetailBrush::CmdSetDetailBrush(EditMapDocument&       doc,
                                     world::SectorId        sectorId,
                                     std::size_t            brushIndex,
                                     world::DetailBrush     newBrush)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_brushIndex(brushIndex)
    , m_newBrush(std::move(newBrush))
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() && m_brushIndex < sectors[m_sectorId].details.size())
        m_oldBrush = sectors[m_sectorId].details[m_brushIndex];
}

void CmdSetDetailBrush::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() && m_brushIndex < sectors[m_sectorId].details.size())
    {
        sectors[m_sectorId].details[m_brushIndex] = m_newBrush;
        m_doc.markDirty();
    }
}

void CmdSetDetailBrush::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size() && m_brushIndex < sectors[m_sectorId].details.size())
    {
        sectors[m_sectorId].details[m_brushIndex] = m_oldBrush;
        m_doc.markDirty();
    }
}

} // namespace daedalus::editor
