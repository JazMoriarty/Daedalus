// portal_traversal.cpp
// Portal visibility traversal — spec Pass 1.
//
// Algorithm:
//   1. Start with the camera's sector and a full-screen NDC window (-1,-1)→(1,1).
//   2. For each portal wall in the current sector:
//      a. Project all four corners of the portal opening (floor/ceil × left/right
//         end) through viewProj into clip space, perform perspective divide.
//      b. Compute the AABB of the projected corners in NDC (clamped to [-1,1]).
//      c. Intersect the portal AABB with the current visible window.
//      d. If the intersection is non-degenerate and the target sector has not
//         been visited yet, recurse with the intersection as the new window.
//   3. After wall portals, check floor and ceiling portals (Phase 1F-B):
//      Project the sector's full floor/ceiling polygon polygon at floorHeight/
//      ceilHeight, compute its NDC AABB, intersect with the current window, and
//      recurse into floorPortalSectorId / ceilPortalSectorId if non-empty.
//   4. Recursion terminates at maxDepth or when no new sectors are reachable.
//
// Cycle guard: a flat visited-sector bitset (std::vector<bool>) ensures each
// sector is added to the result at most once, preventing infinite loops in
// bidirectional portal graphs.
//
// Note on clipping window representation: the visible window is maintained as
// an AABB (min/max in NDC).  For wall portals this is conservative in the same
// way as the baseline implementation.  The full convex-polygon clipper described
// in the design spec (§Portal Traversal Extension for SoS) is a planned
// tightening optimisation; the AABB approach is always conservative (never
// incorrectly culls visible geometry) so it is the correct baseline.

#include "daedalus/world/i_portal_traversal.h"

#include <algorithm>
#include <vector>

namespace daedalus::world
{

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace
{

// When the camera is standing exactly on a portal wall all four portal corners
// project to clip.w = 0 (view-space depth = 0).  The clip.w <= 0 guard marks
// them all as "behind", anyVisible stays false, and the old early-out fired —
// silently dropping the adjacent sector from the visible list regardless of
// look direction.
//
// The fix: track the maximum |clip.w| among behind-or-at-plane corners.
// A small value (< kBoundaryEps) means the camera is at the portal plane →
// force full-screen.  A large value means the portal is genuinely behind the
// camera → skip it as before.
static constexpr float kBoundaryEps = 0.5f;  // world units

// Compute the 2D NDC AABB of a portal opening.
// The portal spans from (x0,z0) to (x1,z1) horizontally and
// floorY to ceilY vertically.
// Returns false if the opening is entirely and clearly behind the camera.
[[nodiscard]] bool portalNDCBounds(const glm::vec2& wallP0,
                                    const glm::vec2& wallP1,
                                    float            floorY,
                                    float            ceilY,
                                    const glm::mat4& viewProj,
                                    glm::vec2&       outMin,
                                    glm::vec2&       outMax) noexcept
{
    const glm::vec3 corners[4] =
    {
        {wallP0.x, floorY, wallP0.y},
        {wallP1.x, floorY, wallP1.y},
        {wallP0.x, ceilY,  wallP0.y},
        {wallP1.x, ceilY,  wallP1.y},
    };

    glm::vec2 ndcMin = glm::vec2( 2.0f,  2.0f);
    glm::vec2 ndcMax = glm::vec2(-2.0f, -2.0f);
    bool  anyVisible    = false;
    bool  anyBehind     = false;
    float behindMaxAbsW = 0.0f;  // max |clip.w| for at-or-behind-plane corners

    for (const auto& c : corners)
    {
        const glm::vec4 clip = viewProj * glm::vec4(c, 1.0f);
        if (clip.w <= 0.0f)
        {
            anyBehind     = true;
            behindMaxAbsW = std::max(behindMaxAbsW, std::abs(clip.w));
            continue;
        }
        const float invW = 1.0f / clip.w;
        ndcMin.x = std::min(ndcMin.x, clip.x * invW);
        ndcMin.y = std::min(ndcMin.y, clip.y * invW);
        ndcMax.x = std::max(ndcMax.x, clip.x * invW);
        ndcMax.y = std::max(ndcMax.y, clip.y * invW);
        anyVisible = true;
    }

    if (!anyVisible)
    {
        // All corners are at or behind the near plane.
        // If they are close to the plane (|w| < kBoundaryEps) the camera is
        // sitting on the portal wall — force full-screen so the adjacent sector
        // always renders regardless of look direction.
        // If they are far behind (|w| >= kBoundaryEps) the portal is genuinely
        // behind the camera and should be skipped.
        if (anyBehind && behindMaxAbsW < kBoundaryEps)
        {
            outMin = glm::vec2(-1.0f, -1.0f);
            outMax = glm::vec2( 1.0f,  1.0f);
            return true;
        }
        return false;
    }

    // Some corners in front, some behind — conservative full-screen window.
    if (anyBehind)
    {
        outMin = glm::vec2(-1.0f, -1.0f);
        outMax = glm::vec2( 1.0f,  1.0f);
        return true;
    }

    outMin = glm::clamp(ndcMin, glm::vec2(-1.0f), glm::vec2(1.0f));
    outMax = glm::clamp(ndcMax, glm::vec2(-1.0f), glm::vec2(1.0f));
    return true;
}

// Intersect two NDC windows. Returns false if the intersection is empty.
[[nodiscard]] bool intersectWindows(glm::vec2 aMin, glm::vec2 aMax,
                                     glm::vec2 bMin, glm::vec2 bMax,
                                     glm::vec2& outMin, glm::vec2& outMax) noexcept
{
    outMin = glm::max(aMin, bMin);
    outMax = glm::min(aMax, bMax);
    constexpr float k_epsilon = 1e-4f;
    return (outMax.x - outMin.x > k_epsilon) && (outMax.y - outMin.y > k_epsilon);
}

// Same boundary fix as portalNDCBounds — applied to floor/ceiling portal
// polygon projections for Sector-Over-Sector traversal.
[[nodiscard]] bool portalHorizontalNDCBounds(
    const std::vector<glm::vec2>& polygonPts,
    float            y,
    const glm::mat4& viewProj,
    glm::vec2&       outMin,
    glm::vec2&       outMax) noexcept
{
    glm::vec2 ndcMin( 2.0f,  2.0f);
    glm::vec2 ndcMax(-2.0f, -2.0f);
    bool  anyVisible    = false;
    bool  anyBehind     = false;
    float behindMaxAbsW = 0.0f;

    for (const auto& p : polygonPts)
    {
        const glm::vec4 clip = viewProj * glm::vec4(p.x, y, p.y, 1.0f);
        if (clip.w <= 0.0f)
        {
            anyBehind     = true;
            behindMaxAbsW = std::max(behindMaxAbsW, std::abs(clip.w));
            continue;
        }
        const float invW = 1.0f / clip.w;
        ndcMin.x = std::min(ndcMin.x, clip.x * invW);
        ndcMin.y = std::min(ndcMin.y, clip.y * invW);
        ndcMax.x = std::max(ndcMax.x, clip.x * invW);
        ndcMax.y = std::max(ndcMax.y, clip.y * invW);
        anyVisible = true;
    }

    if (!anyVisible)
    {
        if (anyBehind && behindMaxAbsW < kBoundaryEps)
        {
            outMin = glm::vec2(-1.0f, -1.0f);
            outMax = glm::vec2( 1.0f,  1.0f);
            return true;
        }
        return false;
    }
    if (anyBehind)
    {
        outMin = glm::vec2(-1.0f, -1.0f);
        outMax = glm::vec2( 1.0f,  1.0f);
        return true;
    }
    outMin = glm::clamp(ndcMin, glm::vec2(-1.0f), glm::vec2(1.0f));
    outMax = glm::clamp(ndcMax, glm::vec2(-1.0f), glm::vec2(1.0f));
    return true;
}

// Per-sector accumulated NDC window used by PortalTraversal::recurse.
struct WindowState
{
    bool      valid = false;
    glm::vec2 min   = glm::vec2(0.0f);
    glm::vec2 max   = glm::vec2(0.0f);
};

} // anonymous namespace

class PortalTraversal final : public IPortalTraversal
{
public:
    [[nodiscard]] std::vector<VisibleSector>
    traverse(const IWorldMap& map,
             SectorId         cameraSector,
             const glm::mat4& viewProj,
             u32              maxDepth) const override
    {
        if (cameraSector == INVALID_SECTOR_ID) { return {}; }

        const auto& mapData   = map.data();
        const auto  numSectors = static_cast<u32>(mapData.sectors.size());

        if (cameraSector >= numSectors) { return {}; }

        std::vector<VisibleSector> result;
        result.reserve(numSectors);

        // Per-sector accumulated visible window.
        // Using a window-merge approach instead of a boolean visited flag:
        // a sector can be revisited when a new portal path provides a wider
        // window than previously seen, updating the scissor rect for rendering.
        std::vector<WindowState> windows(numSectors);
        // Maps sector id -> index in result[], or -1 when not yet emitted.
        std::vector<i32> resultIndex(numSectors, -1);

        const glm::vec2 fullMin(-1.0f, -1.0f);
        const glm::vec2 fullMax( 1.0f,  1.0f);

        recurse(mapData, cameraSector, viewProj, fullMin, fullMax,
                0, maxDepth, windows, resultIndex, result);

        return result;
    }

private:
    void recurse(const WorldMapData&       mapData,
                 SectorId                  sectorId,
                 const glm::mat4&          viewProj,
                 glm::vec2                 windowMin,
                 glm::vec2                 windowMax,
                 u32                       depth,
                 const u32                 maxDepth,
                 std::vector<WindowState>& windows,
                 std::vector<i32>&         resultIndex,
                 std::vector<VisibleSector>& result) const
    {
        if (depth > maxDepth) { return; }

        // If we already reached this sector with an equal-or-larger window,
        // no new visibility can be discovered through this path.
        auto containsWindow = [](glm::vec2 aMin, glm::vec2 aMax,
                                 glm::vec2 bMin, glm::vec2 bMax) noexcept
        {
            constexpr float eps = 1e-4f;
            return (bMin.x >= aMin.x - eps) && (bMin.y >= aMin.y - eps) &&
                   (bMax.x <= aMax.x + eps) && (bMax.y <= aMax.y + eps);
        };

        WindowState& ws = windows[sectorId];
        if (ws.valid)
        {
            if (containsWindow(ws.min, ws.max, windowMin, windowMax))
            {
                return;
            }
            // Merge windows so alternate portal paths can widen coverage.
            ws.min = glm::min(ws.min, windowMin);
            ws.max = glm::max(ws.max, windowMax);
            windowMin = ws.min;
            windowMax = ws.max;
        }
        else
        {
            ws.valid = true;
            ws.min   = windowMin;
            ws.max   = windowMax;
        }

        if (resultIndex[sectorId] < 0)
        {
            resultIndex[sectorId] = static_cast<i32>(result.size());
            result.push_back({ sectorId, windowMin, windowMax });
        }
        else
        {
            VisibleSector& vs = result[static_cast<std::size_t>(resultIndex[sectorId])];
            vs.windowMin = windowMin;
            vs.windowMax = windowMax;
        }

        const Sector& sector    = mapData.sectors[sectorId];
        const auto    n         = sector.walls.size();
        const auto    numSectors = static_cast<SectorId>(mapData.sectors.size());

        // ── Wall portals ──────────────────────────────────────────────────────
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const Wall& wall = sector.walls[wi];
            if (wall.portalSectorId == INVALID_SECTOR_ID) { continue; }
            if (wall.portalSectorId >= numSectors)         { continue; }

            const glm::vec2 wallP1 = sector.walls[(wi + 1) % n].p0;

            // Heights of the portal opening: intersection of both sectors.
            const Sector& adj        = mapData.sectors[wall.portalSectorId];
            const float   portalFloor = std::max(sector.floorHeight, adj.floorHeight);
            const float   portalCeil  = std::min(sector.ceilHeight,  adj.ceilHeight);
            if (portalCeil <= portalFloor) { continue; }  // degenerate opening

            glm::vec2 portalMin, portalMax;
            if (!portalNDCBounds(wall.p0, wallP1, portalFloor, portalCeil,
                                 viewProj, portalMin, portalMax))
            { continue; }

            // Verify the portal is within the current sector's visible window.
            // The intersection is used only as a visibility gate — it is NOT
            // propagated as the window for the adjacent sector.
            //
            // Propagating the clipped intersection causes two compounding bugs:
            //   1. Each portal hop narrows the window further.  After 3+ hops the
            //      window becomes a sliver; the renderer stamps this as the scissor
            //      rect for that sector's draw calls, clipping almost all geometry.
            //   2. The narrowed window can fail the epsilon test for the next portal,
            //      skipping visible sectors entirely (traversal false negatives).
            //
            // Instead, the adjacent sector's scissor window is set to portalMin/Max —
            // the direct NDC extent of the portal that leads into it.  This is the
            // correct screen region to restrict that sector's rendering to (prevents
            // geometry bleeding outside the portal opening), and it never shrinks
            // below the true portal footprint regardless of chain depth.
            glm::vec2 gateMin, gateMax;
            if (!intersectWindows(windowMin, windowMax, portalMin, portalMax,
                                  gateMin, gateMax))
            { continue; }

            recurse(mapData, wall.portalSectorId, viewProj,
                    portalMin, portalMax,
                    depth + 1, maxDepth, windows, resultIndex, result);
        }

        // ── Floor and ceiling portals (Phase 1F-B) ─────────────────────────────
        // Build the sector's XZ polygon once; reused for both floor and ceiling
        // portal projection.  Only allocated when at least one portal is set—
        // the common case (no SoS) pays zero allocation cost.
        const bool hasFloorPortal = (sector.floorPortalSectorId != INVALID_SECTOR_ID
                                     && sector.floorPortalSectorId < numSectors);
        const bool hasCeilPortal  = (sector.ceilPortalSectorId != INVALID_SECTOR_ID
                                     && sector.ceilPortalSectorId < numSectors);

        if (hasFloorPortal || hasCeilPortal)
        {
            // Sector polygon shared between floor and ceiling portal projections.
            std::vector<glm::vec2> poly;
            poly.reserve(n);
            for (const auto& w : sector.walls) poly.push_back(w.p0);

            if (hasFloorPortal)
            {
                glm::vec2 portalMin, portalMax;
                if (portalHorizontalNDCBounds(poly, sector.floorHeight,
                                              viewProj, portalMin, portalMax))
                {
                    glm::vec2 gateMin, gateMax;
                    if (intersectWindows(windowMin, windowMax, portalMin, portalMax,
                                         gateMin, gateMax))
                    {
                        recurse(mapData, sector.floorPortalSectorId, viewProj,
                                portalMin, portalMax,
                                depth + 1, maxDepth, windows, resultIndex, result);
                    }
                }
            }

            if (hasCeilPortal)
            {
                glm::vec2 portalMin, portalMax;
                if (portalHorizontalNDCBounds(poly, sector.ceilHeight,
                                              viewProj, portalMin, portalMax))
                {
                    glm::vec2 gateMin, gateMax;
                    if (intersectWindows(windowMin, windowMax, portalMin, portalMax,
                                         gateMin, gateMax))
                    {
                        recurse(mapData, sector.ceilPortalSectorId, viewProj,
                                portalMin, portalMax,
                                depth + 1, maxDepth, windows, resultIndex, result);
                    }
                }
            }
        }
    }
};

// ─── Factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<IPortalTraversal> makePortalTraversal()
{
    return std::make_unique<PortalTraversal>();
}

} // namespace daedalus::world
