#include "cmd_move_sector.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"
#include "daedalus/core/assert.h"

namespace daedalus::editor
{

CmdMoveSector::CmdMoveSector(EditMapDocument& doc,
                              world::SectorId  sectorId,
                              glm::vec2        delta)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_delta(delta)
{}

void CmdMoveSector::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    DAEDALUS_ASSERT(m_sectorId < sectors.size(), "CmdMoveSector: invalid sector id");

    if (!m_executed)
    {
        // First call: live drag already applied the delta — nothing to do.
        m_executed = true;
    }
    else
    {
        // Redo: re-apply the delta.
        for (auto& wall : sectors[m_sectorId].walls)
            wall.p0 += m_delta;
    }

    m_doc.markDirty();
}

void CmdMoveSector::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    DAEDALUS_ASSERT(m_sectorId < sectors.size(), "CmdMoveSector::undo: invalid sector id");

    for (auto& wall : sectors[m_sectorId].walls)
        wall.p0 -= m_delta;

    m_doc.markDirty();
}

} // namespace daedalus::editor
