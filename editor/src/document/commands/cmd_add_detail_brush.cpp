#include "cmd_add_detail_brush.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdAddDetailBrush::CmdAddDetailBrush(EditMapDocument&       doc,
                                     world::SectorId        sectorId,
                                     world::DetailBrush     brush)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_brush(std::move(brush))
{
}

void CmdAddDetailBrush::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        auto& details     = sectors[m_sectorId].details;
        m_insertedIndex   = details.size();
        details.push_back(m_brush);
        m_doc.markDirty();
    }
}

void CmdAddDetailBrush::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        auto& details = sectors[m_sectorId].details;
        if (!details.empty() && m_insertedIndex < details.size())
        {
            details.erase(details.begin() + static_cast<std::ptrdiff_t>(m_insertedIndex));
            m_doc.markDirty();
        }
    }
}

} // namespace daedalus::editor
