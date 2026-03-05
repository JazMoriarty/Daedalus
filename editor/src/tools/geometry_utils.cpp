#include "geometry_utils.h"

#include <cmath>
#include <cstddef>
#include <utility>

namespace daedalus::editor::geometry
{

// ─── segmentsIntersect ────────────────────────────────────────────────────────
// Cross-product orientation test.
// Returns the sign of the cross product (a1-a0) × (p-a0).
static float cross2(glm::vec2 o, glm::vec2 a, glm::vec2 b) noexcept
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool segmentsIntersect(glm::vec2 a0, glm::vec2 a1,
                       glm::vec2 b0, glm::vec2 b1) noexcept
{
    const float d1 = cross2(b0, b1, a0);
    const float d2 = cross2(b0, b1, a1);
    const float d3 = cross2(a0, a1, b0);
    const float d4 = cross2(a0, a1, b1);

    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
        ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
        return true;

    // Collinear cases — for the editor's purposes, treat as non-intersecting
    // to avoid false positives at shared polygon vertices.
    return false;
}

// ─── isSelfIntersecting ───────────────────────────────────────────────────────

bool isSelfIntersecting(const std::vector<glm::vec2>& verts) noexcept
{
    const std::size_t n = verts.size();
    if (n < 4) return false;  // A triangle cannot self-intersect.

    for (std::size_t i = 0; i < n; ++i)
    {
        const glm::vec2 a0 = verts[i];
        const glm::vec2 a1 = verts[(i + 1) % n];

        for (std::size_t j = i + 2; j < n; ++j)
        {
            // Skip the pair (last, first) which is adjacent to edge 0.
            if (i == 0 && j == n - 1) continue;

            const glm::vec2 b0 = verts[j];
            const glm::vec2 b1 = verts[(j + 1) % n];

            if (segmentsIntersect(a0, a1, b0, b1)) return true;
        }
    }
    return false;
}

// ─── signedArea ───────────────────────────────────────────────────────────────

float signedArea(const std::vector<glm::vec2>& verts) noexcept
{
    const std::size_t n = verts.size();
    if (n < 3) return 0.0f;

    float area = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
    {
        const glm::vec2& a = verts[i];
        const glm::vec2& b = verts[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

// ─── pointInPolygon ───────────────────────────────────────────────────────────

bool pointInPolygon(glm::vec2 p, const std::vector<glm::vec2>& verts) noexcept
{
    const std::size_t n = verts.size();
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++)
    {
        const glm::vec2& vi = verts[i];
        const glm::vec2& vj = verts[j];
        if (((vi.y > p.y) != (vj.y > p.y)) &&
            (p.x < (vj.x - vi.x) * (p.y - vi.y) / (vj.y - vi.y) + vi.x))
        {
            inside = !inside;
        }
    }
    return inside;
}

// ─── pointToSegmentDistSq ─────────────────────────────────────────────────────

float pointToSegmentDistSq(glm::vec2 p, glm::vec2 a, glm::vec2 b) noexcept
{
    const glm::vec2 ab = b - a;
    const float     lenSq = glm::dot(ab, ab);
    if (lenSq < 1e-12f)
        return glm::dot(p - a, p - a);  // degenerate segment: dist to point a

    const float t = glm::clamp(glm::dot(p - a, ab) / lenSq, 0.0f, 1.0f);
    const glm::vec2 proj = a + t * ab;
    const glm::vec2 diff = p - proj;
    return glm::dot(diff, diff);
}

// ─── findMatchingWall ─────────────────────────────────────────────────────────

static constexpr float k_matchEpsilonSq = 0.01f * 0.01f;  // 1 cm squared

std::pair<world::SectorId, std::size_t>
findMatchingWall(world::SectorId sA, std::size_t wA,
                 const world::WorldMapData& map) noexcept
{
    if (sA >= map.sectors.size()) return {world::INVALID_SECTOR_ID, 0};

    const world::Sector& secA = map.sectors[sA];
    const std::size_t    nA   = secA.walls.size();
    if (wA >= nA) return {world::INVALID_SECTOR_ID, 0};

    const glm::vec2 a0 = secA.walls[wA].p0;
    const glm::vec2 a1 = secA.walls[(wA + 1) % nA].p0;

    for (world::SectorId sB = 0; sB < static_cast<world::SectorId>(map.sectors.size()); ++sB)
    {
        if (sB == sA) continue;
        const world::Sector& secB = map.sectors[sB];
        const std::size_t    nB   = secB.walls.size();
        for (std::size_t wB = 0; wB < nB; ++wB)
        {
            const glm::vec2 b0 = secB.walls[wB].p0;
            const glm::vec2 b1 = secB.walls[(wB + 1) % nB].p0;

            // Match in same or opposite winding order.
            const auto distSq = [](glm::vec2 x, glm::vec2 y) {
                return glm::dot(x - y, x - y);
            };
            const bool fwd = distSq(a0, b0) < k_matchEpsilonSq &&
                             distSq(a1, b1) < k_matchEpsilonSq;
            const bool rev = distSq(a0, b1) < k_matchEpsilonSq &&
                             distSq(a1, b0) < k_matchEpsilonSq;
            if (fwd || rev)
                return {sB, wB};
        }
    }
    return {world::INVALID_SECTOR_ID, 0};
}

} // namespace daedalus::editor::geometry
