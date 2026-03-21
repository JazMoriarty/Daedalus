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

    // Snapshot original positions and curve control points for undo().
    m_origPositions.reserve(walls.size());
    m_origCurveA.reserve(walls.size());
    m_origCurveB.reserve(walls.size());
    for (const auto& w : walls)
    {
        m_origPositions.push_back(w.p0);
        m_origCurveA.push_back(w.curveControlA);
        m_origCurveB.push_back(w.curveControlB);
    }
}

void CmdRotateSector::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    DAEDALUS_ASSERT(m_sectorId < sectors.size(), "CmdRotateSector::execute: invalid sector id");

    const float rad = glm::pi<float>() / 180.0f * m_angleDeg;
    const float cosA = std::cos(rad);
    const float sinA = std::sin(rad);

    auto rotatePoint = [&](glm::vec2 pt) -> glm::vec2 {
        const glm::vec2 d = pt - m_centroid;
        return m_centroid + glm::vec2{d.x * cosA - d.y * sinA,
                                      d.x * sinA + d.y * cosA};
    };

    for (auto& wall : sectors[m_sectorId].walls)
    {
        wall.p0 = rotatePoint(wall.p0);
        if (wall.curveControlA.has_value()) *wall.curveControlA = rotatePoint(*wall.curveControlA);
        if (wall.curveControlB.has_value()) *wall.curveControlB = rotatePoint(*wall.curveControlB);
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
    {
        walls[i].p0 = m_origPositions[i];
        walls[i].curveControlA = (i < m_origCurveA.size()) ? m_origCurveA[i] : std::nullopt;
        walls[i].curveControlB = (i < m_origCurveB.size()) ? m_origCurveB[i] : std::nullopt;
    }

    m_doc.markDirty();
}

} // namespace daedalus::editor
