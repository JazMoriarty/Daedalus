#include "cmd_rotate_sector.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"
#include "daedalus/core/assert.h"

#include <glm/gtc/constants.hpp>
#include <cmath>

namespace daedalus::editor
{

CmdRotateSector::CmdRotateSector(EditMapDocument& doc,
                                  world::SectorId  sectorId,
                                  float            angleDeg)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_angleDeg(angleDeg)
{
    const auto& sectors = m_doc.mapData().sectors;
    DAEDALUS_ASSERT(sectorId < sectors.size(), "CmdRotateSector: invalid sector id");

    const auto& walls = sectors[sectorId].walls;

    // Compute centroid (arithmetic mean of vertex positions).
    glm::vec2 sum{};
    for (const auto& w : walls)
        sum += w.p0;
    m_centroid = walls.empty() ? glm::vec2{} : sum / static_cast<float>(walls.size());

    // Snapshot original positions so undo() can restore them exactly.
    m_origPositions.reserve(walls.size());
    for (const auto& w : walls)
        m_origPositions.push_back(w.p0);
}

void CmdRotateSector::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    DAEDALUS_ASSERT(m_sectorId < sectors.size(), "CmdRotateSector::execute: invalid sector id");

    const float rad = glm::pi<float>() / 180.0f * m_angleDeg;
    const float cosA = std::cos(rad);
    const float sinA = std::sin(rad);

    for (auto& wall : sectors[m_sectorId].walls)
    {
        const glm::vec2 d = wall.p0 - m_centroid;
        wall.p0 = m_centroid + glm::vec2{
            d.x * cosA - d.y * sinA,
            d.x * sinA + d.y * cosA
        };
    }

    m_doc.markDirty();
}

void CmdRotateSector::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    DAEDALUS_ASSERT(m_sectorId < sectors.size(), "CmdRotateSector::undo: invalid sector id");

    auto& walls = sectors[m_sectorId].walls;
    DAEDALUS_ASSERT(walls.size() == m_origPositions.size(),
                    "CmdRotateSector::undo: wall count mismatch");

    for (std::size_t i = 0; i < walls.size(); ++i)
        walls[i].p0 = m_origPositions[i];

    m_doc.markDirty();
}

} // namespace daedalus::editor
