// geometry_utils.h
// 2D geometry helpers used by the editor tools.

#pragma once

#include <glm/glm.hpp>
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

} // namespace daedalus::editor::geometry
