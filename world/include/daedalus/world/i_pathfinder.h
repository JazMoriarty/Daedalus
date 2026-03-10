// i_pathfinder.h
// Pure interface for sector-graph pathfinding.
//
// The pathfinder operates over the portal graph implicit in WorldMapData:
// each sector is a node and each non-blocking portal wall is a directed edge.
// A* with centroid-based heuristic finds the optimal sequence of sectors.
//
// Results are expressed as a list of PathWaypoints — world-space XZ positions
// that a navigation agent should move through in order, finishing at the
// caller-supplied goal position.  Intermediate waypoints land on portal
// midpoints so the agent passes cleanly through each doorway.

#pragma once

#include "daedalus/world/i_world_map.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace daedalus::world
{

// ─── PathWaypoint ─────────────────────────────────────────────────────────────
// A single step in a computed path.
//
// `position` is a world-space XZ point (y = 0; callers add floor height if
// needed).  `sectorId` identifies the sector that contains the position so
// consumers can look up floor heights, materials, or trigger zones.
//
// For intermediate waypoints, `position` is the midpoint of the portal through
// which the agent enters `sectorId`.  For the final waypoint, `position` is
// the caller-supplied goal.

struct PathWaypoint
{
    SectorId  sectorId = INVALID_SECTOR_ID;
    glm::vec2 position = {};
};

// ─── PathResult ───────────────────────────────────────────────────────────────
// Return value of IPathfinder::findPath().

struct PathResult
{
    bool reachable = false;     ///< False if goal is disconnected or out of bounds.
    std::vector<PathWaypoint> waypoints;
};

// ─── IPathfinder ──────────────────────────────────────────────────────────────

class IPathfinder
{
public:
    virtual ~IPathfinder() = default;

    IPathfinder(const IPathfinder&)            = delete;
    IPathfinder& operator=(const IPathfinder&) = delete;

    /// Find the cheapest path through the portal graph from `startXZ` to `goalXZ`.
    ///
    /// @param map      The world map providing portal connectivity and sector
    ///                 geometry.  The map must remain valid for the duration of
    ///                 the call.
    /// @param startXZ  World-space XZ start position.  If outside all sectors,
    ///                 returns an unreachable result.
    /// @param goalXZ   World-space XZ goal position.  Same restriction.
    /// @return         PathResult with `reachable=true` and a non-empty waypoint
    ///                 list on success.  On failure: `reachable=false`, empty list.
    [[nodiscard]] virtual PathResult
        findPath(const IWorldMap& map,
                 glm::vec2 startXZ,
                 glm::vec2 goalXZ) const = 0;

protected:
    IPathfinder() = default;
};

// ─── Factory ──────────────────────────────────────────────────────────────────

/// Construct the sector A*-based pathfinder implementation.
[[nodiscard]] std::unique_ptr<IPathfinder> makePathfinder();

} // namespace daedalus::world
