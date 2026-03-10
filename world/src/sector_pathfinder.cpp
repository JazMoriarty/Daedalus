// sector_pathfinder.cpp
// A* pathfinder over the sector portal graph.
//
// Algorithm overview:
//   Nodes     — Sectors (indexed by SectorId == implicit array index).
//   Edges     — Non-blocking portal walls connecting adjacent sectors.
//   Cost      — Euclidean centroid-to-centroid distance.
//   Heuristic — Euclidean distance from current sector centroid to goal
//               sector centroid. Consistent (Euclidean satisfies triangle
//               inequality), so a closed set gives optimal paths.
//   Waypoints — Portal midpoints for each sector transition, followed by
//               the caller-supplied goal position in the final sector.

#include "daedalus/world/i_pathfinder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace daedalus::world
{

namespace
{

// ─── Helpers ──────────────────────────────────────────────────────────────────

/// Average of all wall-start vertices, giving the sector's visual centre.
[[nodiscard]] glm::vec2 centroid(const Sector& sector) noexcept
{
    glm::vec2 sum(0.0f);
    for (const Wall& w : sector.walls) { sum += w.p0; }
    return sum / static_cast<float>(sector.walls.size());
}

/// Midpoint of wall edge i in `sector`.
/// Wall i runs from walls[i].p0 to walls[(i+1)%n].p0.
[[nodiscard]] glm::vec2 portalMidpoint(const Sector& sector, int wallIdx) noexcept
{
    const int n  = static_cast<int>(sector.walls.size());
    const glm::vec2 p0 = sector.walls[wallIdx].p0;
    const glm::vec2 p1 = sector.walls[(wallIdx + 1) % n].p0;
    return (p0 + p1) * 0.5f;
}

// ─── A* node ──────────────────────────────────────────────────────────────────

struct AStarNode
{
    float    fScore = 0.0f;
    SectorId id     = INVALID_SECTOR_ID;

    bool operator>(const AStarNode& o) const noexcept { return fScore > o.fScore; }
};

} // anonymous namespace

// ─── SectorPathfinder ─────────────────────────────────────────────────────────

class SectorPathfinder final : public IPathfinder
{
public:
    PathResult findPath(const IWorldMap& map,
                        glm::vec2 startXZ,
                        glm::vec2 goalXZ) const override
    {
        const WorldMapData& data = map.data();
        const auto numSectors    = static_cast<SectorId>(data.sectors.size());

        if (numSectors == 0) { return { false, {} }; }

        // ── Locate start and goal sectors ─────────────────────────────────────
        const SectorId startId = map.findSector(startXZ);
        const SectorId goalId  = map.findSector(goalXZ);

        if (startId == INVALID_SECTOR_ID || goalId == INVALID_SECTOR_ID) {
            return { false, {} };
        }

        // ── Trivial: same sector ──────────────────────────────────────────────
        if (startId == goalId) {
            return { true, { PathWaypoint{ goalId, goalXZ } } };
        }

        // ── Precompute centroids ───────────────────────────────────────────────
        std::vector<glm::vec2> centroids;
        centroids.reserve(numSectors);
        for (const Sector& s : data.sectors) { centroids.push_back(centroid(s)); }

        const auto h = [&](SectorId s) -> float {
            return glm::length(centroids[s] - centroids[goalId]);
        };

        // ── A* state ──────────────────────────────────────────────────────────
        static constexpr float kInf = std::numeric_limits<float>::infinity();

        std::vector<float>    gScore(numSectors, kInf);
        std::unordered_map<SectorId, SectorId>  cameFrom;    // node → predecessor
        std::unordered_map<SectorId, glm::vec2> entryPoint;  // node → portal midpoint
        std::unordered_set<SectorId>            closed;

        std::priority_queue<AStarNode,
                            std::vector<AStarNode>,
                            std::greater<AStarNode>> open;

        gScore[startId] = 0.0f;
        open.push({ h(startId), startId });

        // ── Main loop ─────────────────────────────────────────────────────────
        while (!open.empty())
        {
            const auto [fCur, curId] = open.top();
            open.pop();

            if (closed.contains(curId)) { continue; }
            closed.insert(curId);

            if (curId == goalId) { break; }

            const Sector& sector = data.sectors[curId];
            const int     n      = static_cast<int>(sector.walls.size());

            for (int i = 0; i < n; ++i)
            {
                const Wall& wall = sector.walls[i];

                // Skip non-portal walls and portal walls that are blocked.
                if (wall.portalSectorId == INVALID_SECTOR_ID) { continue; }
                if (hasFlag(wall.flags, WallFlags::Blocking))  { continue; }

                const SectorId neighborId = wall.portalSectorId;
                if (neighborId >= numSectors) { continue; }  // guard OOB
                if (closed.contains(neighborId)) { continue; }

                // Edge cost: centroid-to-centroid Euclidean distance.
                const float edgeCost =
                    glm::length(centroids[curId] - centroids[neighborId]);
                const float tentative = gScore[curId] + edgeCost;

                if (tentative < gScore[neighborId])
                {
                    gScore[neighborId]   = tentative;
                    cameFrom[neighborId] = curId;
                    entryPoint[neighborId] = portalMidpoint(sector, i);

                    open.push({ tentative + h(neighborId), neighborId });
                }
            }
        }

        // ── Unreachable ───────────────────────────────────────────────────────
        if (gScore[goalId] == kInf) { return { false, {} }; }

        // ── Path reconstruction ───────────────────────────────────────────────
        // Walk cameFrom backward from goalId, collecting portal-entry waypoints.
        // Each waypoint {sectorId, portalMidpoint} is the position where the
        // agent crosses into that sector. After reversing, append the true goal.

        std::vector<PathWaypoint> waypoints;

        SectorId cur = goalId;
        while (cameFrom.contains(cur))
        {
            waypoints.push_back({ cur, entryPoint.at(cur) });
            cur = cameFrom.at(cur);
        }

        std::ranges::reverse(waypoints);

        // Final waypoint: the caller's exact goal position within goalSector.
        waypoints.push_back({ goalId, goalXZ });

        return { true, std::move(waypoints) };
    }
};

// ─── Factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<IPathfinder> makePathfinder()
{
    return std::make_unique<SectorPathfinder>();
}

} // namespace daedalus::world
