// sector_tessellator.cpp
// Converts WorldMapData sector polygons into StaticMeshVertex geometry.
//
// Vertex layout: StaticMeshVertex (pos 12B + normal 12B + uv 8B + tangent 16B = 48B)
//
// Coordinate conventions (3D world space):
//   X = map X,  Y = height (up),  Z = map Z
//
// Surface normals point toward the interior of the sector:
//   Floor   → (0,+1,0)
//   Ceiling → (0,−1,0)
//   Walls   → inward-facing horizontal normal computed from wall direction
//
// UV mapping:
//   Floor / ceiling: u = x / uvScale.x,  v = z / uvScale.y  (+ uvOffset)
//   Walls:           u = along-wall distance / uvScale.x
//                    v = (yCeil_at_column − y) / uvScale.y  (+ uvOffset)
//                    Ceiling column gets v = vOff (texture top);
//                    floor column gets a larger v value so the texture top aligns
//                    with the ceiling regardless of wall slope.
//
// Phase 1F-A changes:
//   - appendHorizontalSurface: replaced triangle fan with ear-clipping.
//     Handles all convex and concave simple polygons correctly.
//     Now accepts a per-vertex Y heights span for sloped floors and ceilings.
//   - appendWallQuad: now takes per-end floor and ceiling heights
//     (yFloor0/yCeil0 at p0, yFloor1/yCeil1 at p1) for trapezoidal walls.
//     For flat walls (all heights equal) the output is identical to the old version.
//   - appendStairSurface: new function generating tread + riser quads from a
//     StairProfile when sector.floorShape == FloorShape::VisualStairs.
//   - tessellateMap / tessellateMapTagged: build per-vertex height arrays from
//     Wall::floorHeightOverride / ceilHeightOverride (falling back to sector
//     scalar heights when absent) and dispatch to the updated helpers.

#include "daedalus/world/sector_tessellator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <span>
#include <unordered_map>

#include <glm/gtc/constants.hpp>  // glm::two_pi, glm::pi
#include <glm/gtc/matrix_inverse.hpp>

namespace daedalus::world
{

namespace
{

// ─── Vertex helper ───────────────────────────────────────────────────────────────

[[nodiscard]] inline render::StaticMeshVertex makeVertex(
    float px, float py, float pz,
    float nx, float ny, float nz,
    float u,  float v,
    float tx, float ty, float tz, float tw) noexcept
{
    render::StaticMeshVertex vtx;
    vtx.pos[0] = px; vtx.pos[1] = py; vtx.pos[2] = pz;
    vtx.normal[0] = nx; vtx.normal[1] = ny; vtx.normal[2] = nz;
    vtx.uv[0] = u; vtx.uv[1] = v;
    vtx.tangent[0] = tx; vtx.tangent[1] = ty;
    vtx.tangent[2] = tz; vtx.tangent[3] = tw;
    return vtx;
}

// ─── 2D geometry helpers (XZ plane; glm::vec2{worldX, worldZ}) ────────────────

// Signed cross product of vectors (b−a) × (c−a) in the XZ plane.
// Positive → c is to the LEFT of the directed line a→b (CCW turn).
// Negative → c is to the RIGHT of the directed line a→b (CW turn).
[[nodiscard]] inline float cross2D(glm::vec2 a, glm::vec2 b, glm::vec2 c) noexcept
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Returns true if point p is strictly inside triangle (a, b, c) in the XZ plane.
[[nodiscard]] inline bool pointInTriangle2D(
    glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) noexcept
{
    const float d1 = cross2D(a, b, p);
    const float d2 = cross2D(b, c, p);
    const float d3 = cross2D(c, a, p);
    const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(hasNeg && hasPos);
}

// Sutherland-Hodgman single half-plane clip: retains points where
// dot(p, normal) >= offset, adding intersection points on the boundary.
[[nodiscard]] static std::vector<glm::vec2> clipByHalfPlane(
    const std::vector<glm::vec2>& poly,
    glm::vec2 normal,
    float     offset) noexcept
{
    std::vector<glm::vec2> result;
    result.reserve(poly.size() + 1);
    const u32 n = static_cast<u32>(poly.size());
    for (u32 i = 0; i < n; ++i)
    {
        const glm::vec2 curr = poly[i];
        const glm::vec2 next = poly[(i + 1) % n];
        const float dCurr = glm::dot(curr, normal) - offset;
        const float dNext = glm::dot(next, normal) - offset;
        if (dCurr >= 0.0f) result.push_back(curr);
        if ((dCurr >= 0.0f) != (dNext >= 0.0f))
        {
            const float t = dCurr / (dCurr - dNext);
            result.push_back(curr + t * (next - curr));
        }
    }
    return result;
}

// ─── appendHorizontalSurface (ear-clipping) ───────────────────────────────────────
//
// Tessellates a horizontal (or per-vertex-sloped) surface from a simple polygon.
//
// polygonPts: N 2D points {worldX, worldZ} in CCW order when viewed from above.
//             Concave polygons are supported via ear-clipping.
// heights:    One world Y value per polygon point.  For flat surfaces all values
//             equal the sector's scalar height.
// normalY:    +1.0 for floors (normal up), −1.0 for ceilings (normal down).
//
// N vertices are emitted (one per polygon point, shared across all triangles),
// yielding (N−2) triangles.  Output for convex polygons is identical in vertex
// count to the old triangle-fan; for concave polygons it is always correct.
//
// Normals: flat surfaces use the hardcoded (0, ±1, 0).  Sloped surfaces (any
// height differs from heights[0]) have per-vertex normals recomputed from the
// actual triangle geometry after ear-clipping.  Correct normals are required
// by SSAO/HBAO+, TAA neighbourhood clamping, and the deferred lighting pass;
// without them a sloped floor produces dancing white-noise artefacts.
static void appendHorizontalSurface(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const std::vector<glm::vec2>&          polygonPts,
    std::span<const float>                 heights,
    float normalY,
    float uScale, float vScale,
    float uOff,   float vOff,
    float uvRotation = 0.0f) noexcept
{
    const u32 n = static_cast<u32>(polygonPts.size());
    if (n < 3 || heights.size() < n) return;

    // Detect sloped surfaces so we know whether to recompute normals below.
    bool isSloped = false;
    const float h0 = heights[0];
    for (u32 i = 1; i < n; ++i)
        if (std::abs(heights[i] - h0) > 1e-5f) { isSloped = true; break; }

    const u32   base      = static_cast<u32>(verts.size());
    const u32   indexBase = static_cast<u32>(indices.size()); // for normal post-pass
    const float tx        = (normalY > 0.0f) ? 1.0f : -1.0f;

    const float cosR = std::cos(uvRotation);
    const float sinR = std::sin(uvRotation);

    for (u32 i = 0; i < n; ++i)
    {
        const float px = polygonPts[i].x;
        const float pz = polygonPts[i].y;
        const float py = heights[i];
        // UV: scale → translate → rotate (same convention as wall UV).
        // When uvRotation == 0 the output is identical to the previous code.
        const float u_t = px / uScale + uOff;
        const float v_t = pz / vScale + vOff;
        const float u_f = u_t * cosR - v_t * sinR;
        const float v_f = u_t * sinR + v_t * cosR;
        // Normals start as (0, ±1, 0).  For sloped surfaces these are overwritten
        // by the per-vertex normal post-pass at the end of this function.
        verts.push_back(makeVertex(px, py, pz,
                                   0.0f, normalY, 0.0f,
                                   u_f, v_f,
                                   tx, 0.0f, 0.0f, 1.0f));
    }

    // ── Triangle (n==3): emit directly, then fall through to normal post-pass. ───────
    if (n == 3)
    {
        if (normalY > 0.0f)
        { indices.push_back(base); indices.push_back(base+1); indices.push_back(base+2); }
        else
        { indices.push_back(base); indices.push_back(base+2); indices.push_back(base+1); }
        // Fall through to the normal post-pass below.
    }
    else
    {

    // Ear-clipping: work on a mutable list of LOCAL indices [0..n-1].
    // Emitted triangles reference GPU vertices via (base + localIndex).
    std::vector<u32> active(n);
    std::iota(active.begin(), active.end(), 0u);

    // Normalise to CCW if the polygon is supplied CW.
    float area2 = 0.0f;
    for (u32 i = 0; i < n; ++i)
    {
        const glm::vec2& a = polygonPts[i];
        const glm::vec2& b = polygonPts[(i + 1) % n];
        area2 += a.x * b.y - a.y * b.x;
    }
    if (area2 < 0.0f) std::reverse(active.begin(), active.end());

    auto isEar = [&](u32 ai) -> bool
    {
        const u32 m     = static_cast<u32>(active.size());
        const u32 lprev = active[(ai + m - 1) % m];
        const u32 li    = active[ai];
        const u32 lnext = active[(ai + 1) % m];
        const glm::vec2& p = polygonPts[lprev];
        const glm::vec2& q = polygonPts[li];
        const glm::vec2& r = polygonPts[lnext];
        if (cross2D(p, q, r) <= 0.0f) return false;  // reflex or collinear
        for (u32 j = 0; j < m; ++j)
        {
            if (j == (ai + m - 1) % m || j == ai || j == (ai + 1) % m) continue;
            if (pointInTriangle2D(polygonPts[active[j]], p, q, r)) return false;
        }
        return true;
    };

    while (active.size() > 3)
    {
        bool found = false;
        for (u32 ai = 0; ai < static_cast<u32>(active.size()); ++ai)
        {
            if (!isEar(ai)) continue;
            const u32 m     = static_cast<u32>(active.size());
            const u32 lprev = active[(ai + m - 1) % m];
            const u32 li    = active[ai];
            const u32 lnext = active[(ai + 1) % m];
            if (normalY > 0.0f)
            {
                indices.push_back(base + lprev);
                indices.push_back(base + li);
                indices.push_back(base + lnext);
            }
            else
            {
                indices.push_back(base + lprev);
                indices.push_back(base + lnext);
                indices.push_back(base + li);
            }
            active.erase(active.begin() + ai);
            found = true;
            break;
        }
        if (!found) break;  // degenerate polygon
    }
    if (active.size() == 3)
    {
        if (normalY > 0.0f)
        {
            indices.push_back(base + active[0]);
            indices.push_back(base + active[1]);
            indices.push_back(base + active[2]);
        }
        else
        {
            indices.push_back(base + active[0]);
            indices.push_back(base + active[2]);
            indices.push_back(base + active[1]);
        }
    }

    } // end else (n > 3 ear-clipping path)

    // ── Per-vertex normal post-pass for sloped surfaces ──────────────────────────
    // For flat surfaces (isSloped == false) the hardcoded (0, ±1, 0) is already
    // correct and this block is skipped at no cost.
    if (isSloped)
    {
        // Accumulate area-weighted face normals into each vertex.
        std::vector<glm::vec3> normAccum(n, glm::vec3(0.0f));
        const u32 numNewIdx = static_cast<u32>(indices.size()) - indexBase;
        for (u32 t = 0; t < numNewIdx; t += 3)
        {
            const u32 i0 = indices[indexBase + t];
            const u32 i1 = indices[indexBase + t + 1];
            const u32 i2 = indices[indexBase + t + 2];
            const glm::vec3 p0{verts[i0].pos[0], verts[i0].pos[1], verts[i0].pos[2]};
            const glm::vec3 p1{verts[i1].pos[0], verts[i1].pos[1], verts[i1].pos[2]};
            const glm::vec3 p2{verts[i2].pos[0], verts[i2].pos[1], verts[i2].pos[2]};
            // Area-weighted face normal (unnormalised cross product).
            const glm::vec3 faceN = glm::cross(p1 - p0, p2 - p0);
            if (glm::dot(faceN, faceN) < 1e-10f) continue;  // degenerate, skip
            normAccum[i0 - base] += faceN;
            normAccum[i1 - base] += faceN;
            normAccum[i2 - base] += faceN;
        }
        for (u32 i = 0; i < n; ++i)
        {
            const float len2 = glm::dot(normAccum[i], normAccum[i]);
            if (len2 < 1e-10f) continue;  // degenerate vertex: keep (0, ±1, 0)
            const glm::vec3 nn = normAccum[i] * (1.0f / std::sqrt(len2));
            verts[base + i].normal[0] = nn.x;
            verts[base + i].normal[1] = nn.y;
            verts[base + i].normal[2] = nn.z;
        }
    }
}

// ─── appendWallQuad
//
// Phase 1F-A: per-end heights replace the single yFloor/yCeil pair.
// For flat walls (yFloor0==yFloor1, yCeil0==yCeil1) output is identical to old.
//
// n0_xz / n1_xz: explicit inward XZ normals at p0 and p1.  When both are zero,
// the normal is derived from the chord direction (straight-wall behaviour).
// For curved wall segments these are set to the Bezier tangent normals at each
// endpoint so lighting interpolates smoothly across segment boundaries.
//
// UV v = (yCeil_local − y) / vScale + vOff so the texture top aligns with the
// ceiling at each wall column regardless of slope.
static void appendWallQuad(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    glm::vec2 p0World, glm::vec2 p1World,
    float yFloor0, float yCeil0,
    float yFloor1, float yCeil1,
    float uScale,  float vScale,
    float uOff,    float vOff,
    float uvRotation = 0.0f,
    glm::vec2 n0_xz = {0.0f, 0.0f},  ///< Inward XZ normal at p0; {0,0} → derive from chord.
    glm::vec2 n1_xz = {0.0f, 0.0f}   ///< Inward XZ normal at p1; {0,0} → derive from chord.
) noexcept
{
    const glm::vec2 dir2D = p1World - p0World;
    const float     len   = std::sqrt(dir2D.x * dir2D.x + dir2D.y * dir2D.y);
    if (len < 1e-6f) return;

    // ── Normals ───────────────────────────────────────────────────────────────
    // Default: chord-perpendicular (straight-wall convention, unchanged from before).
    // Override: Bezier tangent normals provided by the caller for curved segments.
    const float invLen  = 1.0f / len;
    const float tx_cord = dir2D.x * invLen;
    const float tz_cord = dir2D.y * invLen;
    const float nx_cord = -tz_cord;
    const float nz_cord =  tx_cord;

    const bool useCustomNormals =
        (n0_xz.x != 0.0f || n0_xz.y != 0.0f) ||
        (n1_xz.x != 0.0f || n1_xz.y != 0.0f);

    const float nx0 = useCustomNormals ? n0_xz.x : nx_cord;
    const float nz0 = useCustomNormals ? n0_xz.y : nz_cord;
    const float nx1 = useCustomNormals ? n1_xz.x : nx_cord;
    const float nz1 = useCustomNormals ? n1_xz.y : nz_cord;

    // Tangent is always from the chord direction for UV mapping.
    const float tx = tx_cord;
    const float tz = tz_cord;

    // ── UVs ───────────────────────────────────────────────────────────────────
    const float u0   = 0.0f / uScale + uOff;
    const float u1   = len  / uScale + uOff;
    const float v_bl = (yCeil0 - yFloor0) / vScale + vOff;
    const float v_br = (yCeil1 - yFloor1) / vScale + vOff;
    const float v_tr = vOff;
    const float v_tl = vOff;

    const float cosR = std::cos(uvRotation);
    const float sinR = std::sin(uvRotation);
    auto rotU = [cosR, sinR](float u, float v) noexcept { return u * cosR - v * sinR; };
    auto rotV = [cosR, sinR](float u, float v) noexcept { return u * sinR + v * cosR; };

    const u32 base = static_cast<u32>(verts.size());
    verts.push_back(makeVertex(p0World.x, yFloor0, p0World.y, nx0,0,nz0,
                               rotU(u0,v_bl), rotV(u0,v_bl), tx,0,tz,1));
    verts.push_back(makeVertex(p1World.x, yFloor1, p1World.y, nx1,0,nz1,
                               rotU(u1,v_br), rotV(u1,v_br), tx,0,tz,1));
    verts.push_back(makeVertex(p1World.x, yCeil1,  p1World.y, nx1,0,nz1,
                               rotU(u1,v_tr), rotV(u1,v_tr), tx,0,tz,1));
    verts.push_back(makeVertex(p0World.x, yCeil0,  p0World.y, nx0,0,nz0,
                               rotU(u0,v_tl), rotV(u0,v_tl), tx,0,tz,1));

    indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 1);
    indices.push_back(base + 0); indices.push_back(base + 3); indices.push_back(base + 2);
}

// ─── appendStairSurface ───────────────────────────────────────────────────────────────
//
// FloorShape::VisualStairs: generates tread + riser quads from a StairProfile.
// Each step: tread is a flat horizontal polygon clipped to the step's depth slab;
// riser is a vertical quad at the entry depth spanning the tread's perp. width.
static void appendStairSurface(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const std::vector<glm::vec2>&          polygonPts,
    float                                  baseFloor,
    const StairProfile&                    profile,
    float uScale, float vScale,
    float uOff,   float vOff) noexcept
{
    if (profile.stepCount == 0 || profile.treadDepth < 1e-6f) return;

    const glm::vec2 stairDir  = { std::cos(profile.directionAngle),
                                   std::sin(profile.directionAngle) };
    const glm::vec2 stairPerp = { -stairDir.y, stairDir.x };

    float dMin = std::numeric_limits<float>::max();
    float dMax = std::numeric_limits<float>::lowest();
    for (const auto& p : polygonPts)
    {
        const float d = glm::dot(p, stairDir);
        dMin = std::min(dMin, d);
        dMax = std::max(dMax, d);
    }
    if (dMax <= dMin) return;

    for (u32 step = 0; step < profile.stepCount; ++step)
    {
        const float stepFloor = baseFloor + static_cast<float>(step) * profile.riserHeight;
        const float d0 = dMin + static_cast<float>(step)     * profile.treadDepth;
        const float d1 = dMin + static_cast<float>(step + 1) * profile.treadDepth;

        std::vector<glm::vec2> treadPoly = polygonPts;
        treadPoly = clipByHalfPlane(treadPoly,  stairDir, d0);
        treadPoly = clipByHalfPlane(treadPoly, -stairDir, -d1);
        if (treadPoly.size() < 3) continue;

        std::vector<float> treadH(treadPoly.size(), stepFloor);
        appendHorizontalSurface(verts, indices, treadPoly,
                                std::span<const float>(treadH),
                                +1.0f, uScale, vScale, uOff, vOff);

        if (step == 0) continue;
        const float prevFloor = baseFloor + static_cast<float>(step - 1) * profile.riserHeight;

        float wMin = std::numeric_limits<float>::max();
        float wMax = std::numeric_limits<float>::lowest();
        for (const auto& p : treadPoly)
        {
            if (std::abs(glm::dot(p, stairDir) - d0) < 1e-3f)
            {
                const float w = glm::dot(p, stairPerp);
                wMin = std::min(wMin, w);
                wMax = std::max(wMax, w);
            }
        }
        if (wMax <= wMin + 1e-6f) continue;

        const glm::vec2 rP0 = wMin * stairPerp + d0 * stairDir;
        const glm::vec2 rP1 = wMax * stairPerp + d0 * stairDir;
        appendWallQuad(verts, indices, rP0, rP1,
                       prevFloor, stepFloor, prevFloor, stepFloor,
                       uScale, vScale, uOff, vOff);
    }
}

// ─── appendHeightfieldFloor (Phase 1F-D step 7) ──────────────────────────────────
//
// Generates a (gridWidth−1) × (gridDepth−1) quad mesh from the heightfield.
//
// Vertex layout: W×D shared vertices, one per sample, indexed by a grid
// index buffer.  Per-vertex normals are computed by central differencing so
// the surface shades smoothly across quad boundaries.
//
// Normal derivation: at interior grid point (i, j) the normal is proportional
// to cross(T_z, T_x) where:
//   T_x = world-space tangent in X = (2*dx, h(i+1,j)−h(i−1,j), 0)
//   T_z = world-space tangent in Z = (0, h(i,j+1)−h(i,j−1), 2*dz)
// Simplifying: normal ∝ (−(h_right−h_left)/(2*dx), 1, −(h_top−h_bot)/(2*dz))
// (normalised).
//
// Edge normal handling: Edge vertices extrapolate the slope from their nearest
// interior neighbor to eliminate lighting discontinuities. A one-sided difference
// would create sharp brightness changes at grid boundaries; extrapolation ensures
// smooth shading across the entire surface.
//
// UV: world XZ position mapped through sector UV offset/scale/rotation.
static void appendHeightfieldFloor(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const HeightfieldFloor&                hf,
    float uScale, float vScale,
    float uOff,   float vOff,
    float uvRotation = 0.0f) noexcept
{
    const u32 W = hf.gridWidth;
    const u32 D = hf.gridDepth;
    if (W < 2u || D < 2u || hf.samples.size() < static_cast<std::size_t>(W * D))
        return;

    const float rangeX = hf.worldMax.x - hf.worldMin.x;
    const float rangeZ = hf.worldMax.y - hf.worldMin.y;
    if (rangeX <= 0.0f || rangeZ <= 0.0f) return;

    const float dx = rangeX / static_cast<float>(W - 1u);
    const float dz = rangeZ / static_cast<float>(D - 1u);

    const u32 base = static_cast<u32>(verts.size());

    // Emit W×D vertices with central-difference normals.
    for (u32 j = 0; j < D; ++j)
    {
        for (u32 i = 0; i < W; ++i)
        {
            const float px = hf.worldMin.x + static_cast<float>(i) * dx;
            const float pz = hf.worldMin.y + static_cast<float>(j) * dz;
            const float py = hf.samples[j * W + i];

            // Compute normal via central differencing at interior vertices,
            // extrapolating from nearest interior at edges.
            glm::vec3 n;
            
            // Determine sample indices for height differences.
            // Interior: use neighbors on both sides (central difference).
            // Edge: extrapolate from the interior direction.
            const u32 iL = (i > 0)      ? i - 1 : 0;      // Left neighbor
            const u32 iR = (i < W - 1u) ? i + 1 : W - 1u; // Right neighbor
            const u32 jB = (j > 0)      ? j - 1 : 0;      // Bottom neighbor
            const u32 jT = (j < D - 1u) ? j + 1 : D - 1u; // Top neighbor
            
            const float hL = hf.samples[j  * W + iL];
            const float hR = hf.samples[j  * W + iR];
            const float hB = hf.samples[jB * W + i];
            const float hT = hf.samples[jT * W + i];
            
            const bool isLeftEdge   = (i == 0);
            const bool isRightEdge  = (i == W - 1u);
            const bool isBottomEdge = (j == 0);
            const bool isTopEdge    = (j == D - 1u);
            
            // X-direction slope (dh/dx).
            float slopeX;
            if (isLeftEdge)
            {
                // Left edge: extrapolate slope from interior (i=1).
                // slope ≈ (h[1] - h[0]) / dx
                slopeX = (hR - py) / dx;
            }
            else if (isRightEdge)
            {
                // Right edge: extrapolate slope from interior (i=W-2).
                // slope ≈ (h[W-1] - h[W-2]) / dx
                slopeX = (py - hL) / dx;
            }
            else
            {
                // Interior: central difference.
                slopeX = (hR - hL) / (2.0f * dx);
            }
            
            // Z-direction slope (dh/dz).
            float slopeZ;
            if (isBottomEdge)
            {
                // Bottom edge: extrapolate slope from interior (j=1).
                slopeZ = (hT - py) / dz;
            }
            else if (isTopEdge)
            {
                // Top edge: extrapolate slope from interior (j=D-2).
                slopeZ = (py - hB) / dz;
            }
            else
            {
                // Interior: central difference.
                slopeZ = (hT - hB) / (2.0f * dz);
            }
            
            // Normal is perpendicular to both tangent directions:
            // T_x = (1, dh/dx, 0),  T_z = (0, dh/dz, 1)
            // N = T_x × T_z = (-dh/dx, 1, -dh/dz), then normalize.
            n = glm::normalize(glm::vec3(-slopeX, 1.0f, -slopeZ));

            // Tangent along world +X direction (for normal mapping).
            // Use the same extrapolated slope for consistency.
            const glm::vec3 t = glm::normalize(
                glm::vec3(1.0f, slopeX, 0.0f));

            // UV: use world XZ coordinates directly, matching appendHorizontalSurface.
            // This ensures texture tiling is consistent between flat and heightfield surfaces.
            const float u_t = px / uScale + uOff;
            const float v_t = pz / vScale + vOff;
            // Rotate.
            const float cosR = std::cos(uvRotation);
            const float sinR = std::sin(uvRotation);
            const float u = u_t * cosR - v_t * sinR;
            const float v = u_t * sinR + v_t * cosR;

            verts.push_back(makeVertex(px, py, pz,
                                       n.x, n.y, n.z,
                                       u,   v,
                                       t.x, t.y, t.z, 1.0f));
        }
    }

    // Emit (W−1)×(D−1) quads as CCW triangles (floor, normal up).
    // Diagonal direction alternates in a checkerboard pattern to eliminate Mach banding.
    for (u32 j = 0; j < D - 1u; ++j)
    {
        for (u32 i = 0; i < W - 1u; ++i)
        {
            const u32 bl = base +  j      * W +  i;       // bottom-left
            const u32 br = base +  j      * W + (i + 1u); // bottom-right
            const u32 tr = base + (j + 1u)* W + (i + 1u); // top-right
            const u32 tl = base + (j + 1u)* W +  i;       // top-left

            // Alternate diagonal direction in checkerboard pattern.
            // This eliminates visible diagonal banding (Mach bands) at grazing angles.
            const bool useBLtoTRdiagonal = ((i + j) & 1u) == 0u;
            
            if (useBLtoTRdiagonal)
            {
                // Diagonal from bottom-left to top-right.
                indices.push_back(bl); indices.push_back(br); indices.push_back(tr);
                indices.push_back(bl); indices.push_back(tr); indices.push_back(tl);
            }
            else
            {
                // Diagonal from bottom-right to top-left.
                indices.push_back(br); indices.push_back(tr); indices.push_back(tl);
                indices.push_back(br); indices.push_back(tl); indices.push_back(bl);
            }
        }
    }
}

// ─── appendHeightfieldCeiling (Phase 1F-D+) ──────────────────────────────────────
//
// Generates a (gridWidth−1) × (gridDepth−1) quad mesh for a heightfield ceiling.
// Identical to appendHeightfieldFloor except:
//   - Normals point downward (normal.y = -1.0)
//   - Triangle winding order reversed (CW when viewed from below)
// Reuses HeightfieldFloor structure; samples are absolute world Y values.
static void appendHeightfieldCeiling(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const HeightfieldFloor&                hf,
    float uScale, float vScale,
    float uOff,   float vOff,
    float uvRotation = 0.0f) noexcept
{
    const u32 W = hf.gridWidth;
    const u32 D = hf.gridDepth;
    if (W < 2u || D < 2u || hf.samples.size() < static_cast<std::size_t>(W * D))
        return;

    const float rangeX = hf.worldMax.x - hf.worldMin.x;
    const float rangeZ = hf.worldMax.y - hf.worldMin.y;
    if (rangeX <= 0.0f || rangeZ <= 0.0f) return;

    const float dx = rangeX / static_cast<float>(W - 1u);
    const float dz = rangeZ / static_cast<float>(D - 1u);

    const u32 base = static_cast<u32>(verts.size());

    // Emit W×D vertices with extrapolated edge normals (inverted for ceiling).
    for (u32 j = 0; j < D; ++j)
    {
        for (u32 i = 0; i < W; ++i)
        {
            const float px = hf.worldMin.x + static_cast<float>(i) * dx;
            const float pz = hf.worldMin.y + static_cast<float>(j) * dz;
            const float py = hf.samples[j * W + i];

            // Compute normal via central differencing at interior vertices,
            // extrapolating from nearest interior at edges (same as floor).
            glm::vec3 n;
            
            const u32 iL = (i > 0)      ? i - 1 : 0;
            const u32 iR = (i < W - 1u) ? i + 1 : W - 1u;
            const u32 jB = (j > 0)      ? j - 1 : 0;
            const u32 jT = (j < D - 1u) ? j + 1 : D - 1u;
            
            const float hL = hf.samples[j  * W + iL];
            const float hR = hf.samples[j  * W + iR];
            const float hB = hf.samples[jB * W + i];
            const float hT = hf.samples[jT * W + i];
            
            const bool isLeftEdge   = (i == 0);
            const bool isRightEdge  = (i == W - 1u);
            const bool isBottomEdge = (j == 0);
            const bool isTopEdge    = (j == D - 1u);
            
            // X-direction slope.
            float slopeX;
            if (isLeftEdge)
                slopeX = (hR - py) / dx;
            else if (isRightEdge)
                slopeX = (py - hL) / dx;
            else
                slopeX = (hR - hL) / (2.0f * dx);
            
            // Z-direction slope.
            float slopeZ;
            if (isBottomEdge)
                slopeZ = (hT - py) / dz;
            else if (isTopEdge)
                slopeZ = (py - hB) / dz;
            else
                slopeZ = (hT - hB) / (2.0f * dz);
            
            // Ceiling normal: inverted Y component.
            n = glm::normalize(glm::vec3(-slopeX, -1.0f, -slopeZ));

            // Tangent along world +X direction (for normal mapping).
            const glm::vec3 t = glm::normalize(
                glm::vec3(1.0f, slopeX, 0.0f));

            // UV: use world XZ coordinates directly, matching appendHorizontalSurface.
            // This ensures texture tiling is consistent between flat and heightfield surfaces.
            const float u_t = px / uScale + uOff;
            const float v_t = pz / vScale + vOff;
            // Rotate.
            const float cosR = std::cos(uvRotation);
            const float sinR = std::sin(uvRotation);
            const float u = u_t * cosR - v_t * sinR;
            const float v = u_t * sinR + v_t * cosR;

            verts.push_back(makeVertex(px, py, pz,
                                       n.x, n.y, n.z,
                                       u,   v,
                                       t.x, t.y, t.z, 1.0f));
        }
    }

    // Emit (W−1)×(D−1) quads with reversed winding (CW when viewed from below).
    // Diagonal direction alternates in a checkerboard pattern to eliminate Mach banding.
    for (u32 j = 0; j < D - 1u; ++j)
    {
        for (u32 i = 0; i < W - 1u; ++i)
        {
            const u32 bl = base +  j      * W +  i;
            const u32 br = base +  j      * W + (i + 1u);
            const u32 tr = base + (j + 1u)* W + (i + 1u);
            const u32 tl = base + (j + 1u)* W +  i;

            // Alternate diagonal direction in checkerboard pattern.
            const bool useBLtoTRdiagonal = ((i + j) & 1u) == 0u;
            
            if (useBLtoTRdiagonal)
            {
                // Diagonal from bottom-left to top-right (ceiling winding).
                indices.push_back(bl); indices.push_back(tl); indices.push_back(tr);
                indices.push_back(bl); indices.push_back(tr); indices.push_back(br);
            }
            else
            {
                // Diagonal from bottom-right to top-left (ceiling winding).
                indices.push_back(br); indices.push_back(bl); indices.push_back(tl);
                indices.push_back(br); indices.push_back(tl); indices.push_back(tr);
            }
        }
    }
}

// ─── buildHeightArrays ─────────────────────────────────────────────────────────────
// Builds per-vertex height arrays from Wall overrides, falling back to the
// sector's scalar heights when no override is present.
static void buildHeightArrays(const Sector&       sector,
                               std::vector<float>& floorH,
                               std::vector<float>& ceilH)
{
    const std::size_t n = sector.walls.size();
    floorH.resize(n);
    ceilH .resize(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        floorH[i] = sector.walls[i].floorHeightOverride.value_or(sector.floorHeight);
        ceilH [i] = sector.walls[i].ceilHeightOverride .value_or(sector.ceilHeight);
    }
}

// ─── Bezier evaluation helpers (Phase 1F-C step 6) ──────────────────────────────────────────

// Evaluate a quadratic Bezier at parameter t ∈ [0, 1].
// B(t) = (1−t)²·p0 + 2t(1−t)·ca + t²·p1
[[nodiscard]] static glm::vec2 evalBezierQuadratic(
    glm::vec2 p0, glm::vec2 ca, glm::vec2 p1, float t) noexcept
{
    const float mt = 1.0f - t;
    return mt*mt*p0 + 2.0f*mt*t*ca + t*t*p1;
}

// Evaluate a cubic Bezier at parameter t ∈ [0, 1].
// B(t) = (1−t)³·p0 + 3t(1−t)²·ca + 3t²(1−t)·cb + t³·p1
[[nodiscard]] static glm::vec2 evalBezierCubic(
    glm::vec2 p0, glm::vec2 ca, glm::vec2 cb, glm::vec2 p1, float t) noexcept
{
    const float mt = 1.0f - t;
    return mt*mt*mt*p0 + 3.0f*mt*mt*t*ca + 3.0f*mt*t*t*cb + t*t*t*p1;
}

// First derivative of a quadratic Bezier: dB/dt = 2(1-t)(ca-p0) + 2t(p1-ca)
[[nodiscard]] static glm::vec2 evalBezierQuadraticTangent(
    glm::vec2 p0, glm::vec2 ca, glm::vec2 p1, float t) noexcept
{
    return 2.0f * (1.0f - t) * (ca - p0) + 2.0f * t * (p1 - ca);
}

// First derivative of a cubic Bezier:
// dB/dt = 3(1-t)²(ca-p0) + 6(1-t)t(cb-ca) + 3t²(p1-cb)
[[nodiscard]] static glm::vec2 evalBezierCubicTangent(
    glm::vec2 p0, glm::vec2 ca, glm::vec2 cb, glm::vec2 p1, float t) noexcept
{
    const float mt = 1.0f - t;
    return 3.0f*mt*mt*(ca-p0) + 6.0f*mt*t*(cb-ca) + 3.0f*t*t*(p1-cb);
}

// Compute the inward-facing XZ wall normal from a 2D tangent vector.
// Matches the convention in appendWallQuad: normal = 90° CCW rotation of the
// unit tangent, i.e. (-tz, tx).
// Falls back to {0, 1} for degenerate (zero-length) tangents.
[[nodiscard]] static glm::vec2 wallNormalFromTangent2D(glm::vec2 tangent) noexcept
{
    const float len = std::sqrt(tangent.x*tangent.x + tangent.y*tangent.y);
    if (len < 1e-6f) return {0.0f, 1.0f};
    const glm::vec2 t = tangent / len;
    return {-t.y, t.x};  // 90° CCW rotation — inward normal for CCW wall winding
}

// ─── buildExpandedPoly ───────────────────────────────────────────────────────────────────────
// Builds the XZ polygon used for floor and ceiling tessellation, inserting
// Bezier subdivision points for any curved walls.  For straight walls the
// output is identical to iterating over wall.p0 values.
//
// This ensures the floor and ceiling surfaces exactly fill the space swept
// by each wall, with no gap between the floor/ceiling edge and the curved
// wall face when a wall bends inward or outward.
//
// floorHExp and ceilHExp are expanded height arrays aligned to the output
// polygon (heights at interior subdivision points are linearly interpolated
// between the wall's two endpoint heights).
//
// The base height arrays (one per wall vertex, from buildHeightArrays) must
// be passed in and are still used by the wall tessellation code.
static void buildExpandedPoly(
    const Sector&              sector,
    const std::vector<float>&  baseFloorH,
    const std::vector<float>&  baseCeilH,
    std::vector<glm::vec2>&    poly,
    std::vector<float>&        floorHExp,
    std::vector<float>&        ceilHExp)
{
    const std::size_t n = sector.walls.size();
    poly.reserve(n);
    floorHExp.reserve(n);
    ceilHExp .reserve(n);

    for (std::size_t wi = 0; wi < n; ++wi)
    {
        const Wall&     wall   = sector.walls[wi];
        const glm::vec2 wallP1 = sector.walls[(wi + 1) % n].p0;

        // Always emit this wall's start vertex.
        poly.push_back(wall.p0);
        floorHExp.push_back(baseFloorH[wi]);
        ceilHExp .push_back(baseCeilH[wi]);

        if (wall.curveControlA.has_value())
        {
            // Insert interior subdivision points (k = 1 .. nSeg-1).
            // The endpoint at t=1 is wall[(wi+1)%n].p0 and is emitted in the
            // next iteration, so it is deliberately NOT emitted here.
            const u32         nSeg   = std::clamp(wall.curveSubdivisions, 4u, 64u);
            const std::size_t wiNext = (wi + 1) % n;

            for (u32 k = 1; k < nSeg; ++k)
            {
                const float t = static_cast<float>(k) / static_cast<float>(nSeg);

                glm::vec2 pt;
                if (wall.curveControlB)
                    pt = evalBezierCubic(wall.p0, *wall.curveControlA,
                                         *wall.curveControlB, wallP1, t);
                else
                    pt = evalBezierQuadratic(wall.p0, *wall.curveControlA, wallP1, t);

                poly.push_back(pt);
                floorHExp.push_back(baseFloorH[wi] + t * (baseFloorH[wiNext] - baseFloorH[wi]));
                ceilHExp .push_back(baseCeilH [wi] + t * (baseCeilH [wiNext] - baseCeilH [wi]));
            }
        }
    }
}

// ─── Detail brush geometry generators (Phase 1F-C step 9) ───────────────────────

// Append a single quad to verts/indices.  pos[4] are in CCW order from the
// outside (the correct front face for the given normal direction).
// Uses CW-in-NDC winding to match the wall / ceiling convention.
static void appendDetailQuad(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const glm::vec3 pos[4],
    glm::vec3       worldNormal,
    glm::vec3       worldTangent,
    const float     uvCoords[4][2]) noexcept
{
    const u32 base = static_cast<u32>(verts.size());
    for (int i = 0; i < 4; ++i)
    {
        verts.push_back(makeVertex(
            pos[i].x, pos[i].y, pos[i].z,
            worldNormal.x,  worldNormal.y,  worldNormal.z,
            uvCoords[i][0], uvCoords[i][1],
            worldTangent.x, worldTangent.y, worldTangent.z, 1.0f));
    }
    // CW in NDC (matches appendWallQuad convention).
    indices.push_back(base+0); indices.push_back(base+2); indices.push_back(base+1);
    indices.push_back(base+0); indices.push_back(base+3); indices.push_back(base+2);
}

// Helper: transform a local-space position by xform.
[[nodiscard]] static glm::vec3 xformPos(const glm::mat4& xform,
                                         glm::vec3 local) noexcept
{
    return glm::vec3(xform * glm::vec4(local, 1.0f));
}

// Helper: transform a local-space normal by the normal matrix (upper-3×3 of
// the inverse-transpose of the transform).
[[nodiscard]] static glm::vec3 xformNormal(const glm::mat3& normalMat,
                                            glm::vec3 local) noexcept
{
    return glm::normalize(normalMat * local);
}

// ─── appendDetailBox ─────────────────────────────────────────────────────────────
// 6-faced axis-aligned box in local space, transformed to world space.
static void appendDetailBox(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const glm::mat4&                       xform,
    const glm::mat3&                       normalMat,
    const DetailBrushGeomParams&           p) noexcept
{
    const float hx = p.halfExtents.x, hy = p.halfExtents.y, hz = p.halfExtents.z;

    // Each face: local normal, tangent, 4 CCW positions from outside.
    // Use explicit glm::vec3 constructors; brace-initialization does not deduce
    // template arguments for aggregate sub-objects in all compilers.
    struct FaceDesc { glm::vec3 n, t; glm::vec3 v[4]; };
    const FaceDesc faces[6] = {
        // Bottom (-Y)
        {glm::vec3{0,-1,0}, glm::vec3{1,0,0},
         {glm::vec3{-hx,-hy,-hz}, glm::vec3{hx,-hy,-hz},
          glm::vec3{hx,-hy,hz},   glm::vec3{-hx,-hy,hz}}},
        // Top (+Y)
        {glm::vec3{0,+1,0}, glm::vec3{1,0,0},
         {glm::vec3{-hx,+hy,+hz}, glm::vec3{hx,+hy,+hz},
          glm::vec3{hx,+hy,-hz},  glm::vec3{-hx,+hy,-hz}}},
        // Front (+Z)
        {glm::vec3{0,0,+1}, glm::vec3{1,0,0},
         {glm::vec3{-hx,-hy,+hz}, glm::vec3{hx,-hy,+hz},
          glm::vec3{hx,+hy,+hz},  glm::vec3{-hx,+hy,+hz}}},
        // Back (-Z)
        {glm::vec3{0,0,-1}, glm::vec3{-1,0,0},
         {glm::vec3{hx,-hy,-hz},  glm::vec3{-hx,-hy,-hz},
          glm::vec3{-hx,+hy,-hz}, glm::vec3{hx,+hy,-hz}}},
        // Right (+X)
        {glm::vec3{+1,0,0}, glm::vec3{0,0,1},
         {glm::vec3{hx,-hy,-hz}, glm::vec3{hx,-hy,+hz},
          glm::vec3{hx,+hy,+hz}, glm::vec3{hx,+hy,-hz}}},
        // Left (-X)
        {glm::vec3{-1,0,0}, glm::vec3{0,0,-1},
         {glm::vec3{-hx,-hy,+hz}, glm::vec3{-hx,-hy,-hz},
          glm::vec3{-hx,+hy,-hz}, glm::vec3{-hx,+hy,+hz}}},
    };

    for (const auto& face : faces)
    {
        glm::vec3 wpos[4];
        for (int i = 0; i < 4; ++i) wpos[i] = xformPos(xform, face.v[i]);
        const glm::vec3 wn = xformNormal(normalMat, face.n);
        const glm::vec3 wt = xformNormal(normalMat, face.t);

        // UV: project local vertex onto the face's 2D tangent/bitangent plane.
        const glm::vec3 bt = glm::cross(face.n, face.t);
        float uvs[4][2];
        for (int i = 0; i < 4; ++i)
        {
            uvs[i][0] = glm::dot(face.v[i], face.t)  + 0.5f;
            uvs[i][1] = glm::dot(face.v[i], bt) + 0.5f;
        }
        appendDetailQuad(verts, indices, wpos, wn, wt, uvs);
    }
}

// ─── appendDetailWedge ────────────────────────────────────────────────────────────
// Right triangular prism with slopeAxis=1 (Z-slope, default): full height
// at z=−hz tapering to zero at z=+hz.
// slopeAxis=0 (X-slope): full height at x=−hx tapering to zero at x=+hx.
static void appendDetailWedge(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const glm::mat4&                       xform,
    const glm::mat3&                       normalMat,
    const DetailBrushGeomParams&           p) noexcept
{
    const float hx = p.halfExtents.x, hy = p.halfExtents.y, hz = p.halfExtents.z;

    // Vertex key abbreviations (slopeAxis=1, slope along Z):
    //   BLB = bottom-left-back, BRB = bottom-right-back, TLB = top-left-back, TRB = top-right-back
    //   BLF = bottom-left-front, BRF = bottom-right-front  (no top-front vertices)
    const glm::vec3 BLB{-hx,-hy,-hz}, BRB{+hx,-hy,-hz},
                    TLB{-hx,+hy,-hz}, TRB{+hx,+hy,-hz},
                    BLF{-hx,-hy,+hz}, BRF{+hx,-hy,+hz};

    // Slope normal for the top face: the slope rises from z=+hz (floor height)
    // to full height at z=−hz.  Normal points away from the prism interior:
    // slope = (rise/run) in YZ plane → normal = normalize(0, hz, hy).
    const glm::vec3 slopeN = glm::normalize(glm::vec3(0.0f, hz, hy));

    // Only slopeAxis=1 is implemented; slopeAxis=0 (X slope) can be achieved
    // by rotating the brush 90° around Y in the transform.
    // (slopeAxis=0 support deferred; document here for future implementor.)

    struct QuadFace { glm::vec3 n, t, v[4]; };
    const QuadFace quads[3] = {
        // Bottom (-Y)
        {{0,-1,0},{1,0,0},{BLB,BRB,BRF,BLF}},
        // Back (-Z)
        {{0,0,-1},{-1,0,0},{BRB,BLB,TLB,TRB}},
        // Slope (top face)
        {slopeN,{1,0,0},{TLB,BLF,BRF,TRB}},
    };
    for (const auto& q : quads)
    {
        glm::vec3 wpos[4];
        for (int i = 0; i < 4; ++i) wpos[i] = xformPos(xform, q.v[i]);
        const glm::vec3 wn = xformNormal(normalMat, q.n);
        const glm::vec3 wt = xformNormal(normalMat, q.t);
        const glm::vec3 bt = glm::cross(q.n, q.t);
        float uvs[4][2];
        for (int i = 0; i < 4; ++i)
        { uvs[i][0] = glm::dot(q.v[i], q.t)+0.5f; uvs[i][1] = glm::dot(q.v[i], bt)+0.5f; }
        appendDetailQuad(verts, indices, wpos, wn, wt, uvs);
    }

    // Two triangular end faces (left and right).
    // Left (-X): BLB, BLF, TLB.
    // Right (+X): BRF, BRB, TRB.
    const glm::vec3 leftN  = xformNormal(normalMat, {-1,0,0});
    const glm::vec3 rightN = xformNormal(normalMat, {+1,0,0});
    const glm::vec3 leftT  = xformNormal(normalMat, {0,0,1});
    const glm::vec3 rightT = xformNormal(normalMat, {0,0,-1});

    auto emitTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c,
                        glm::vec3 wn, glm::vec3 wt)
    {
        const u32 base = static_cast<u32>(verts.size());
        auto emit = [&](glm::vec3 lp, float u, float v)
        {
            const glm::vec3 wp = xformPos(xform, lp);
            verts.push_back(makeVertex(wp.x,wp.y,wp.z, wn.x,wn.y,wn.z, u,v, wt.x,wt.y,wt.z,1));
        };
        emit(a, 0.0f, 0.0f); emit(b, 1.0f, 0.0f); emit(c, 0.5f, 1.0f);
        // CW in NDC
        indices.push_back(base+0); indices.push_back(base+2); indices.push_back(base+1);
    };
    emitTri(BLB, BLF, TLB, leftN,  leftT);
    emitTri(BRF, BRB, TRB, rightN, rightT);
}

// ─── appendDetailCylinder ────────────────────────────────────────────────────────
// Faceted cylinder: N side quads + top and bottom polygon caps.
static void appendDetailCylinder(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const glm::mat4&                       xform,
    const glm::mat3&                       normalMat,
    const DetailBrushGeomParams&           p) noexcept
{
    const u32   N  = std::clamp(p.segmentCount, 4u, 64u);
    const float r  = p.radius;
    const float hh = p.height * 0.5f;
    const float tau = glm::two_pi<float>();

    // Side quads
    for (u32 i = 0; i < N; ++i)
    {
        const float t0 = tau * static_cast<float>(i)   / static_cast<float>(N);
        const float t1 = tau * static_cast<float>(i+1) / static_cast<float>(N);
        const float tm = (t0 + t1) * 0.5f;  // midpoint angle for face normal

        const glm::vec3 localP0 = {r*std::cos(t0), -hh, r*std::sin(t0)};
        const glm::vec3 localP1 = {r*std::cos(t1), -hh, r*std::sin(t1)};
        const glm::vec3 localP2 = {r*std::cos(t1),  hh, r*std::sin(t1)};
        const glm::vec3 localP3 = {r*std::cos(t0),  hh, r*std::sin(t0)};

        const glm::vec3 localN  = {std::cos(tm), 0.0f, std::sin(tm)};
        const glm::vec3 localT  = {-std::sin(t0), 0.0f, std::cos(t0)};  // tangent at p0

        const glm::vec3 wn = xformNormal(normalMat, localN);
        const glm::vec3 wt = xformNormal(normalMat, localT);
        glm::vec3 wpos[4] = {
            xformPos(xform, localP0), xformPos(xform, localP1),
            xformPos(xform, localP2), xformPos(xform, localP3)
        };
        // UV: u wraps around circumference, v = 0 bottom, 1 top
        const float u0 = static_cast<float>(i)   / static_cast<float>(N);
        const float u1 = static_cast<float>(i+1) / static_cast<float>(N);
        const float uvs[4][2] = {{u0,0},{u1,0},{u1,1},{u0,1}};
        appendDetailQuad(verts, indices, wpos, wn, wt, uvs);
    }

    // Top and bottom caps using appendHorizontalSurface.
    // Build capPoly (XZ world positions) and both height arrays in a single
    // pass so each vertex position is transformed once, not twice.
    std::vector<glm::vec2> capPoly(N);
    std::vector<float> botH(N), topH(N);
    for (u32 i = 0; i < N; ++i)
    {
        const float t  = tau * static_cast<float>(i) / static_cast<float>(N);
        const float cx = r * std::cos(t);
        const float cz = r * std::sin(t);
        const glm::vec3 bot = xformPos(xform, {cx, -hh, cz});
        const glm::vec3 top = xformPos(xform, {cx,  hh, cz});
        capPoly[i] = {bot.x, bot.z};  // XZ from bottom cap (same footprint as top for upright cylinders)
        botH[i]    = bot.y;
        topH[i]    = top.y;
    }
    appendHorizontalSurface(verts, indices, capPoly, std::span<const float>(botH),
                            -1.0f, 1.0f, 1.0f, 0.0f, 0.0f);  // bottom cap (normal down)
    appendHorizontalSurface(verts, indices, capPoly, std::span<const float>(topH),
                            +1.0f, 1.0f, 1.0f, 0.0f, 0.0f);  // top cap (normal up)
}

// ─── appendDetailArchSpan ────────────────────────────────────────────────────────
// Curved arch band in the local XY plane, with depth `thickness` along Z.
// Profile: Semicircular (elliptical), Gothic, and Segmental share the same
// elliptical code for Phase 1F-C; profile differentiation is a Phase 1F-D item.
// The arch spans from x=−spanWidth/2 to x=+spanWidth/2 along local X,
// rising to archHeight along local Y, with depth thickness along local Z.
// Generates: inner face (Z=0), outer face (Z=thickness), and two base end quads.
static void appendDetailArchSpan(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const glm::mat4&                       xform,
    const glm::mat3&                       normalMat,
    const DetailBrushGeomParams&           p) noexcept
{
    const u32   N    = std::clamp(p.archSegments, 4u, 64u);
    const float rx   = p.spanWidth  * 0.5f;   // X semi-axis
    const float ry   = p.archHeight;           // Y semi-axis
    const float thk  = p.thickness;
    const float pi   = glm::pi<float>();

    // Gothic / Segmental: both currently fall back to Semicircular.
    // TODO(Phase 1F-D): implement distinct Gothic and Segmental arch profiles.

    // Arch curve: parametric ellipse from θ=0 to θ=π.
    // x(θ) = −cos(θ)*rx,  y(θ) = sin(θ)*ry
    // Normal at θ: radially outward = normalize(sin(θ)/rx, cos(θ)/ry, 0).
    // (This is the outward ellipse normal, perpendicular to the arch curve.)
    auto archPt = [&](float theta) -> glm::vec2
    {
        return {-std::cos(theta) * rx, std::sin(theta) * ry};
    };
    auto archNormal2D = [&](float theta) -> glm::vec2
    {
        return glm::normalize(glm::vec2(std::sin(theta) * ry, std::cos(theta) * rx));
    };

    // Emit arch face (N quads) at a given Z position, with inward/outward normal.
    auto emitArchFace = [&](float z, float normalZ)
    {
        for (u32 i = 0; i < N; ++i)
        {
            const float t0 = pi * static_cast<float>(i)   / static_cast<float>(N);
            const float t1 = pi * static_cast<float>(i+1) / static_cast<float>(N);
            const float tm = (t0 + t1) * 0.5f;

            const glm::vec2 a0 = archPt(t0), a1 = archPt(t1);

            const glm::vec3 lp0{a0.x, a0.y, z},      lp1{a1.x, a1.y, z};
            const glm::vec3 lp2{a1.x, a1.y, z+thk},  lp3{a0.x, a0.y, z+thk};

            const glm::vec2 n2 = archNormal2D(tm);
            const glm::vec3 localN{n2.x * normalZ, n2.y * normalZ, 0.0f};
            const glm::vec3 localT = glm::normalize(
                glm::vec3(a1.x - a0.x, a1.y - a0.y, 0.0f));

            const glm::vec3 wn = xformNormal(normalMat, localN);
            const glm::vec3 wt = xformNormal(normalMat, localT);
            glm::vec3 wpos[4] = {
                xformPos(xform, lp0), xformPos(xform, lp1),
                xformPos(xform, lp2), xformPos(xform, lp3)
            };
            const float u0 = static_cast<float>(i)   / static_cast<float>(N);
            const float u1 = static_cast<float>(i+1) / static_cast<float>(N);
            const float uvs[4][2] = {{u0,0},{u1,0},{u1,1},{u0,1}};
            appendDetailQuad(verts, indices, wpos, wn, wt, uvs);
        }
    };

    emitArchFace(0.0f, -1.0f);  // inner face: quads span Z=[0, thk], normals inward toward arch centre
    emitArchFace(0.0f,  1.0f);  // outer face: same Z=[0, thk] quads, normals outward from arch curve

    // Left end cap: vertical quad from base-left corner (X=−rx,Y=0) to arch curve
    // start point (which is also at X=−rx, Y=0 for a symmetric ellipse), spanning Z=[0,thk].
    {
        const glm::vec2 a0 = archPt(0.0f);
        const glm::vec3 endPts[4] = {
            xformPos(xform, {-rx, 0.0f, 0.0f}),
            xformPos(xform, {-rx, 0.0f, thk}),
            xformPos(xform, {a0.x, a0.y, thk}),
            xformPos(xform, {a0.x, a0.y, 0.0f})
        };
        const glm::vec3 wn = xformNormal(normalMat, {-1,0,0});
        const glm::vec3 wt = xformNormal(normalMat, {0,0,1});
        const float uvs[4][2] = {{0,0},{1,0},{1,1},{0,1}};
        appendDetailQuad(verts, indices, endPts, wn, wt, uvs);
    }
    {
        const glm::vec2 a1 = archPt(pi);
        const glm::vec3 endPts[4] = {
            xformPos(xform, {rx, 0.0f, thk}), xformPos(xform, {rx, 0.0f, 0.0f}),
            xformPos(xform, {a1.x, a1.y, 0.0f}), xformPos(xform, {a1.x, a1.y, thk})
        };
        const glm::vec3 wn = xformNormal(normalMat, {+1,0,0});
        const glm::vec3 wt = xformNormal(normalMat, {0,0,-1});
        const float uvs[4][2] = {{0,0},{1,0},{1,1},{0,1}};
        appendDetailQuad(verts, indices, endPts, wn, wt, uvs);
    }
}

// ─── appendDetailBrush ────────────────────────────────────────────────────────────
// Dispatch to the appropriate generator.  ImportedMesh brushes are skipped
// here — they are handled by the asset pipeline compile step.
static void appendDetailBrush(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const DetailBrush&                     brush) noexcept
{
    if (brush.type == DetailBrushType::ImportedMesh) return;  // asset pipeline step

    // Compute the normal matrix once per brush: upper-3×3 of the inverse-transpose.
    const glm::mat3 normalMat = glm::mat3(
        glm::transpose(glm::inverse(glm::mat3(brush.transform))));

    switch (brush.type)
    {
    case DetailBrushType::Box:
        appendDetailBox(verts, indices, brush.transform, normalMat, brush.geom);
        break;
    case DetailBrushType::Wedge:
        appendDetailWedge(verts, indices, brush.transform, normalMat, brush.geom);
        break;
    case DetailBrushType::Cylinder:
        appendDetailCylinder(verts, indices, brush.transform, normalMat, brush.geom);
        break;
    case DetailBrushType::ArchSpan:
        appendDetailArchSpan(verts, indices, brush.transform, normalMat, brush.geom);
        break;
    default:
        break;  // ImportedMesh and unknown types: nothing to emit here
    }
}

} // anonymous namespace

// ─── tessellateMap ───────────────────────────────────────────────────────────────

std::vector<render::MeshData> tessellateMap(const WorldMapData& map)
{
    std::vector<render::MeshData> result;
    result.resize(map.sectors.size());

    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const Sector& sector = map.sectors[si];
        const auto    n      = sector.walls.size();
        if (n < 3) { continue; }

        render::MeshData& mesh = result[si];

        // Build per-vertex base heights (one per wall, indexed by wall index).
        // These are still used by the wall tessellation loop.
        std::vector<float> floorH, ceilH;
        buildHeightArrays(sector, floorH, ceilH);

        // Build the expanded polygon for floor/ceiling surfaces: curved walls
        // contribute Bezier interior points so the horizontal surfaces fill
        // the full area swept by each wall, with no gap at curved walls.
        std::vector<glm::vec2> poly;
        std::vector<float> floorHExp, ceilHExp;
        buildExpandedPoly(sector, floorH, ceilH, poly, floorHExp, ceilHExp);

        // ── Floor ────────────────────────────────────────────────────────────────────────────────
        if (sector.floorShape == FloorShape::Heightfield && sector.heightfield)
        {
            const float fuS = sector.floorUvScale.x > 0.0f ? sector.floorUvScale.x : 1.0f;
            const float fvS = sector.floorUvScale.y > 0.0f ? sector.floorUvScale.y : 1.0f;
            appendHeightfieldFloor(mesh.vertices, mesh.indices, *sector.heightfield,
                                   fuS, fvS,
                                   sector.floorUvOffset.x, sector.floorUvOffset.y,
                                   sector.floorUvRotation);
        }
        else if (sector.floorShape == FloorShape::VisualStairs && sector.stairProfile)
        {
            appendStairSurface(mesh.vertices, mesh.indices, poly,
                               sector.floorHeight, *sector.stairProfile,
                               1.0f, 1.0f, 0.0f, 0.0f);
        }
        else
        {
            const float fuS = sector.floorUvScale.x > 0.0f ? sector.floorUvScale.x : 1.0f;
            const float fvS = sector.floorUvScale.y > 0.0f ? sector.floorUvScale.y : 1.0f;
            appendHorizontalSurface(mesh.vertices, mesh.indices, poly,
                                    std::span<const float>(floorHExp),
                                    +1.0f, fuS, fvS,
                                    sector.floorUvOffset.x, sector.floorUvOffset.y,
                                    sector.floorUvRotation);
        }

        // ── Ceiling ───────────────────────────────────────────────────────────────────────────
        if (sector.ceilingShape == CeilingShape::Heightfield && sector.ceilHeightfield)
        {
            const float cuS = sector.ceilUvScale.x > 0.0f ? sector.ceilUvScale.x : 1.0f;
            const float cvS = sector.ceilUvScale.y > 0.0f ? sector.ceilUvScale.y : 1.0f;
            appendHeightfieldCeiling(mesh.vertices, mesh.indices, *sector.ceilHeightfield,
                                     cuS, cvS,
                                     sector.ceilUvOffset.x, sector.ceilUvOffset.y,
                                     sector.ceilUvRotation);
        }
        else
        {
            const float cuS = sector.ceilUvScale.x > 0.0f ? sector.ceilUvScale.x : 1.0f;
            const float cvS = sector.ceilUvScale.y > 0.0f ? sector.ceilUvScale.y : 1.0f;
            appendHorizontalSurface(mesh.vertices, mesh.indices, poly,
                                    std::span<const float>(ceilHExp),
                                    -1.0f, cuS, cvS,
                                    sector.ceilUvOffset.x, sector.ceilUvOffset.y,
                                    sector.ceilUvRotation);
        }

        // ── Walls ──────────────────────────────────────────────────────────────────────────
        // Wall tessellation uses base height arrays (indexed by wall index wi).
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const Wall&       wall   = sector.walls[wi];
            const glm::vec2   wallP1 = sector.walls[(wi + 1) % n].p0;
            const std::size_t wiNext = (wi + 1) % n;

            const float yF0 = floorH[wi];    const float yC0 = ceilH[wi];
            const float yF1 = floorH[wiNext]; const float yC1 = ceilH[wiNext];

            const float uScale = (wall.uvScale.x > 0.0f) ? wall.uvScale.x : 1.0f;
            const float vScale = (wall.uvScale.y > 0.0f) ? wall.uvScale.y : 1.0f;

            // ── Curved wall subdivision (Phase 1F-C step 6) ─────────────────────
            // When curveControlA is set, evaluate the Bezier at N+1 parameter
            // values and emit N wall quads (one per segment).  Heights are
            // linearly interpolated from [yF0,yC0] at t=0 to [yF1,yC1] at t=1.
            // Portal curved walls reuse the same strip logic after subdivision.
            const bool isCurved = wall.curveControlA.has_value();
            const u32  nSeg     = isCurved
                ? std::clamp(wall.curveSubdivisions, 4u, 64u)
                : 1u;

            // Bezier tangent helper: evaluates dB/dt in the XZ plane at t.
            // Used to derive per-vertex normals that interpolate smoothly across
            // segment boundaries, eliminating lighting discontinuities.
            auto evalCurveTangent = [&](float t) -> glm::vec2
            {
                const glm::vec2 p0 = wall.p0, p1 = wallP1;
                if (wall.curveControlB)
                    return evalBezierCubicTangent(p0, *wall.curveControlA,
                                                  *wall.curveControlB, p1, t);
                return evalBezierQuadraticTangent(p0, *wall.curveControlA, p1, t);
            };

            auto evalCurve = [&](float t) -> glm::vec2
            {
                const glm::vec2 p0 = wall.p0, p1 = wallP1;
                if (wall.curveControlB)
                    return evalBezierCubic(p0, *wall.curveControlA, *wall.curveControlB, p1, t);
                return evalBezierQuadratic(p0, *wall.curveControlA, p1, t);
            };

            // Segment loop: executes once (t: 0→1) for straight walls,
            // nSeg times for curved walls.
            for (u32 si = 0; si < nSeg; ++si)
            {
                const float t0 = static_cast<float>(si)   / static_cast<float>(nSeg);
                const float t1 = static_cast<float>(si+1) / static_cast<float>(nSeg);

                const glm::vec2 segP0 = isCurved ? evalCurve(t0) : wall.p0;
                const glm::vec2 segP1 = isCurved ? evalCurve(t1) : wallP1;

                // Per-vertex smooth normals for curved walls: derived from the
                // Bezier tangent at each endpoint so adjacent segments share the
                // same normal direction — eliminating lighting discontinuities.
                // Straight walls pass {0,0} → appendWallQuad falls back to
                // chord-derived normal (identical to previous behaviour).
                const glm::vec2 segN0 = isCurved
                    ? wallNormalFromTangent2D(evalCurveTangent(t0)) : glm::vec2{0.0f, 0.0f};
                const glm::vec2 segN1 = isCurved
                    ? wallNormalFromTangent2D(evalCurveTangent(t1)) : glm::vec2{0.0f, 0.0f};

                // Per-segment heights by linear interpolation.
                const float segYF0 = yF0 + t0 * (yF1 - yF0);
                const float segYC0 = yC0 + t0 * (yC1 - yC0);
                const float segYF1 = yF0 + t1 * (yF1 - yF0);
                const float segYC1 = yC0 + t1 * (yC1 - yC0);

                if (wall.portalSectorId == INVALID_SECTOR_ID)
                {
                    appendWallQuad(mesh.vertices, mesh.indices,
                                   segP0, segP1,
                                   segYF0, segYC0, segYF1, segYC1,
                                   uScale, vScale,
                                   wall.uvOffset.x, wall.uvOffset.y,
                                   wall.uvRotation, segN0, segN1);
                }
                else
                {
                    const std::size_t adjId = wall.portalSectorId;
                    if (adjId >= map.sectors.size()) { break; }
                    const Sector& adj = map.sectors[adjId];

                    // Use adj scalar heights for strip bounds (Phase 1F-A).
                    // Per-vertex cross-sector strips deferred to Phase 1F-B.
                    const float adjCeil  = adj.ceilHeight;
                    const float adjFloor = adj.floorHeight;

                    if (segYC0 > adjCeil || segYC1 > adjCeil)
                    {
                        const float sF0 = adjCeil, sC0 = std::max(segYC0, adjCeil);
                        const float sF1 = adjCeil, sC1 = std::max(segYC1, adjCeil);
                        if (sC0 > sF0 || sC1 > sF1)
                            appendWallQuad(mesh.vertices, mesh.indices,
                                           segP0, segP1,
                                           sF0, sC0, sF1, sC1,
                                           uScale, vScale,
                                           wall.uvOffset.x, wall.uvOffset.y,
                                           wall.uvRotation, segN0, segN1);
                    }
                    if (segYF0 < adjFloor || segYF1 < adjFloor)
                    {
                        const float sF0 = std::min(segYF0, adjFloor), sC0 = adjFloor;
                        const float sF1 = std::min(segYF1, adjFloor), sC1 = adjFloor;
                        if (sC0 > sF0 || sC1 > sF1)
                            appendWallQuad(mesh.vertices, mesh.indices,
                                           segP0, segP1,
                                           sF0, sC0, sF1, sC1,
                                           uScale, vScale,
                                           wall.uvOffset.x, wall.uvOffset.y,
                                           wall.uvRotation, segN0, segN1);
                    }
                }
            }  // end segment loop
        }  // end wall loop

        // ── Detail brushes (Phase 1F-C step 9) ──────────────────────────────
        for (const auto& brush : sector.details)
            appendDetailBrush(mesh.vertices, mesh.indices, brush);
    }  // end sector loop

    return result;
}

// ─── tessellateMapTagged ─────────────────────────────────────────────────────────────
//
// Groups every surface into per-materialId batches.  Detail brushes are
// compiled into the batch for their materialId.  Curved walls are subdivided
// into per-segment quads using the same Bezier logic as tessellateMap.

std::vector<std::vector<TaggedMeshBatch>> tessellateMapTagged(const WorldMapData& map)
{
    std::vector<std::vector<TaggedMeshBatch>> result;
    result.resize(map.sectors.size());

    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const Sector& sector = map.sectors[si];
        const auto    n      = sector.walls.size();
        if (n < 3) continue;

        std::unordered_map<daedalus::UUID, std::size_t, daedalus::UUIDHash> uuidToIdx;

        auto getBatch = [&](const daedalus::UUID& uuid) -> TaggedMeshBatch&
        {
            const auto it = uuidToIdx.find(uuid);
            if (it != uuidToIdx.end())
                return result[si][it->second];
            const std::size_t idx = result[si].size();
            result[si].push_back(TaggedMeshBatch{ render::MeshData{}, uuid });
            uuidToIdx[uuid] = idx;
            return result[si].back();
        };

        // Build per-vertex base heights (one per wall, indexed by wall index).
        std::vector<float> floorH, ceilH;
        buildHeightArrays(sector, floorH, ceilH);

        // Build expanded polygon: curved walls contribute Bezier interior points
        // so floor/ceiling surfaces fill the space swept by each wall face.
        std::vector<glm::vec2> poly;
        std::vector<float> floorHExp, ceilHExp;
        buildExpandedPoly(sector, floorH, ceilH, poly, floorHExp, ceilHExp);

        // ── Floor ───────────────────────────────────────────────────────────────────────────────
        // When the floor is a portal, use the portal material UUID so the renderer
        // binds the correct transparent/grate texture.  The geometry is identical.
        {
            const daedalus::UUID& floorMat =
                (sector.floorPortalSectorId != INVALID_SECTOR_ID)
                ? sector.floorPortalMaterialId
                : sector.floorMaterialId;

            if (sector.floorShape == FloorShape::Heightfield && sector.heightfield)
            {
                const float fuS2 = sector.floorUvScale.x > 0.0f ? sector.floorUvScale.x : 1.0f;
                const float fvS2 = sector.floorUvScale.y > 0.0f ? sector.floorUvScale.y : 1.0f;
                TaggedMeshBatch& batch = getBatch(floorMat);
                appendHeightfieldFloor(batch.mesh.vertices, batch.mesh.indices,
                                       *sector.heightfield,
                                       fuS2, fvS2,
                                       sector.floorUvOffset.x, sector.floorUvOffset.y,
                                       sector.floorUvRotation);
            }
            else if (sector.floorShape == FloorShape::VisualStairs && sector.stairProfile)
            {
                TaggedMeshBatch& batch = getBatch(floorMat);
                appendStairSurface(batch.mesh.vertices, batch.mesh.indices, poly,
                                   sector.floorHeight, *sector.stairProfile,
                                   1.0f, 1.0f, 0.0f, 0.0f);
            }
            else
            {
                const float fuS2 = sector.floorUvScale.x > 0.0f ? sector.floorUvScale.x : 1.0f;
                const float fvS2 = sector.floorUvScale.y > 0.0f ? sector.floorUvScale.y : 1.0f;
                TaggedMeshBatch& batch = getBatch(floorMat);
                appendHorizontalSurface(batch.mesh.vertices, batch.mesh.indices, poly,
                                        std::span<const float>(floorHExp),
                                        +1.0f, fuS2, fvS2,
                                        sector.floorUvOffset.x, sector.floorUvOffset.y,
                                        sector.floorUvRotation);
            }
        }

        // ── Ceiling ───────────────────────────────────────────────────────────────────────────────────────────────
        {
            const daedalus::UUID& ceilMat =
                (sector.ceilPortalSectorId != INVALID_SECTOR_ID)
                ? sector.ceilPortalMaterialId
                : sector.ceilMaterialId;

            if (sector.ceilingShape == CeilingShape::Heightfield && sector.ceilHeightfield)
            {
                const float cuS2 = sector.ceilUvScale.x > 0.0f ? sector.ceilUvScale.x : 1.0f;
                const float cvS2 = sector.ceilUvScale.y > 0.0f ? sector.ceilUvScale.y : 1.0f;
                TaggedMeshBatch& batch = getBatch(ceilMat);
                appendHeightfieldCeiling(batch.mesh.vertices, batch.mesh.indices,
                                         *sector.ceilHeightfield,
                                         cuS2, cvS2,
                                         sector.ceilUvOffset.x, sector.ceilUvOffset.y,
                                         sector.ceilUvRotation);
            }
            else
            {
                const float cuS2 = sector.ceilUvScale.x > 0.0f ? sector.ceilUvScale.x : 1.0f;
                const float cvS2 = sector.ceilUvScale.y > 0.0f ? sector.ceilUvScale.y : 1.0f;
                TaggedMeshBatch& batch = getBatch(ceilMat);
                appendHorizontalSurface(batch.mesh.vertices, batch.mesh.indices, poly,
                                        std::span<const float>(ceilHExp),
                                        -1.0f, cuS2, cvS2,
                                        sector.ceilUvOffset.x, sector.ceilUvOffset.y,
                                        sector.ceilUvRotation);
            }
        }

        // ── Walls (with curved subdivision, Phase 1F-C) ─────────────────────────────────
        // Wall tessellation uses the base height arrays (indexed by wall index wi).
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const Wall&       wall   = sector.walls[wi];
            const glm::vec2   wallP1 = sector.walls[(wi + 1) % n].p0;
            const std::size_t wiNext = (wi + 1) % n;

            const float yF0 = floorH[wi];    const float yC0 = ceilH[wi];
            const float yF1 = floorH[wiNext]; const float yC1 = ceilH[wiNext];

            const float uScale = (wall.uvScale.x > 0.0f) ? wall.uvScale.x : 1.0f;
            const float vScale = (wall.uvScale.y > 0.0f) ? wall.uvScale.y : 1.0f;

            const bool isCurved = wall.curveControlA.has_value();
            const u32  nSeg     = isCurved ? std::clamp(wall.curveSubdivisions, 4u, 64u) : 1u;

            // Bezier tangent helper (same logic as tessellateMap variant above).
            auto evalCurveTangent2 = [&](float t) -> glm::vec2
            {
                const glm::vec2 p0 = wall.p0, p1 = wallP1;
                if (wall.curveControlB)
                    return evalBezierCubicTangent(p0, *wall.curveControlA,
                                                  *wall.curveControlB, p1, t);
                return evalBezierQuadraticTangent(p0, *wall.curveControlA, p1, t);
            };

            auto evalCurve2 = [&](float t) -> glm::vec2
            {
                const glm::vec2 p0 = wall.p0, p1 = wallP1;
                if (wall.curveControlB)
                    return evalBezierCubic(p0, *wall.curveControlA, *wall.curveControlB, p1, t);
                return evalBezierQuadratic(p0, *wall.curveControlA, p1, t);
            };

            for (u32 si = 0; si < nSeg; ++si)
            {
                const float t0 = static_cast<float>(si)   / static_cast<float>(nSeg);
                const float t1 = static_cast<float>(si+1) / static_cast<float>(nSeg);

                const glm::vec2 segP0 = isCurved ? evalCurve2(t0) : wall.p0;
                const glm::vec2 segP1 = isCurved ? evalCurve2(t1) : wallP1;

                const glm::vec2 segN0 = isCurved
                    ? wallNormalFromTangent2D(evalCurveTangent2(t0)) : glm::vec2{0.0f, 0.0f};
                const glm::vec2 segN1 = isCurved
                    ? wallNormalFromTangent2D(evalCurveTangent2(t1)) : glm::vec2{0.0f, 0.0f};

                const float segYF0 = yF0 + t0 * (yF1 - yF0);
                const float segYC0 = yC0 + t0 * (yC1 - yC0);
                const float segYF1 = yF0 + t1 * (yF1 - yF0);
                const float segYC1 = yC0 + t1 * (yC1 - yC0);

                if (wall.portalSectorId == INVALID_SECTOR_ID)
                {
                    TaggedMeshBatch& batch = getBatch(wall.frontMaterialId);
                    appendWallQuad(batch.mesh.vertices, batch.mesh.indices,
                                   segP0, segP1,
                                   segYF0, segYC0, segYF1, segYC1,
                                   uScale, vScale,
                                   wall.uvOffset.x, wall.uvOffset.y,
                                   wall.uvRotation, segN0, segN1);
                }
                else
                {
                    const std::size_t adjId = wall.portalSectorId;
                    if (adjId >= map.sectors.size()) break;
                    const Sector& adj = map.sectors[adjId];

                    const float adjCeil  = adj.ceilHeight;
                    const float adjFloor = adj.floorHeight;

                    if (segYC0 > adjCeil || segYC1 > adjCeil)
                    {
                        const float sF0 = adjCeil, sC0 = std::max(segYC0, adjCeil);
                        const float sF1 = adjCeil, sC1 = std::max(segYC1, adjCeil);
                        if (sC0 > sF0 || sC1 > sF1)
                        {
                            TaggedMeshBatch& batch = getBatch(wall.upperMaterialId);
                            appendWallQuad(batch.mesh.vertices, batch.mesh.indices,
                                           segP0, segP1,
                                           sF0, sC0, sF1, sC1,
                                           uScale, vScale,
                                           wall.uvOffset.x, wall.uvOffset.y,
                                           wall.uvRotation, segN0, segN1);
                        }
                    }
                    if (segYF0 < adjFloor || segYF1 < adjFloor)
                    {
                        const float sF0 = std::min(segYF0, adjFloor), sC0 = adjFloor;
                        const float sF1 = std::min(segYF1, adjFloor), sC1 = adjFloor;
                        if (sC0 > sF0 || sC1 > sF1)
                        {
                            TaggedMeshBatch& batch = getBatch(wall.lowerMaterialId);
                            appendWallQuad(batch.mesh.vertices, batch.mesh.indices,
                                           segP0, segP1,
                                           sF0, sC0, sF1, sC1,
                                           uScale, vScale,
                                           wall.uvOffset.x, wall.uvOffset.y,
                                           wall.uvRotation, segN0, segN1);
                        }
                    }
                }
            }  // end segment loop
        }

        // ── Detail brushes (Phase 1F-C step 9) ──────────────────────────────
        for (const auto& brush : sector.details)
        {
            TaggedMeshBatch& batch = getBatch(brush.materialId);
            appendDetailBrush(batch.mesh.vertices, batch.mesh.indices, brush);
        }
    }

    return result;
}

} // namespace daedalus::world
