// geometry_utils.h
// 2D geometry helpers used by the editor tools.

#pragma once

#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <utility>
#include <vector>

namespace daedalus::editor::geometry
{

/// Returns true if segments [a0,a1] and [b0,b1] properly intersect.
/// Endpoint-touching is NOT considered an intersection (allows shared
/// vertices in adjacent sectors).
[[nodiscard]] bool segmentsIntersect(glm::vec2 a0, glm::vec2 a1,
                                     glm::vec2 b0, glm::vec2 b1) noexcept;

/// Returns true if the polygon formed by `verts` is self-intersecting.
/// Skips adjacent-edge pairs (which share an endpoint and would always report
/// intersection via endpoint-touching).
[[nodiscard]] bool isSelfIntersecting(const std::vector<glm::vec2>& verts) noexcept;

/// Returns the signed area of a polygon (positive → CCW, negative → CW).
/// Uses the shoelace formula.
[[nodiscard]] float signedArea(const std::vector<glm::vec2>& verts) noexcept;

/// Returns true if the point p lies inside the polygon defined by verts.
/// Uses a ray-casting test in the XZ plane.
[[nodiscard]] bool pointInPolygon(glm::vec2 p,
                                  const std::vector<glm::vec2>& verts) noexcept;

/// Returns the squared distance from point p to the segment [a, b].
[[nodiscard]] float pointToSegmentDistSq(glm::vec2 p,
                                          glm::vec2 a,
                                          glm::vec2 b) noexcept;

/// Returns true if polygon `a` and polygon `b` have overlapping interior area.
/// Handles partial overlap (edge-edge intersection) and full containment.
/// Adjacent sectors that share only a boundary edge return false.
[[nodiscard]] bool polygonsOverlap(const std::vector<glm::vec2>& a,
                                   const std::vector<glm::vec2>& b) noexcept;

/// Finds a wall in a different sector whose endpoints match the endpoints of
/// wall wA of sector sA (within epsilon), in either winding order.
/// Returns {matchSector, matchWallIndex}, or {INVALID_SECTOR_ID, 0} if none.
[[nodiscard]] std::pair<world::SectorId, std::size_t>
findMatchingWall(world::SectorId          sA,
                 std::size_t              wA,
                 const world::WorldMapData& map) noexcept;

} // namespace daedalus::editor::geometry
