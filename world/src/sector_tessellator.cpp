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
static void appendHorizontalSurface(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const std::vector<glm::vec2>&          polygonPts,
    std::span<const float>                 heights,
    float normalY,
    float uScale, float vScale,
    float uOff,   float vOff) noexcept
{
    const u32 n = static_cast<u32>(polygonPts.size());
    if (n < 3 || heights.size() < n) return;

    const u32   base = static_cast<u32>(verts.size());
    const float tx   = (normalY > 0.0f) ? 1.0f : -1.0f;

    for (u32 i = 0; i < n; ++i)
    {
        const float px = polygonPts[i].x;
        const float pz = polygonPts[i].y;
        const float py = heights[i];
        verts.push_back(makeVertex(px, py, pz,
                                   0.0f, normalY, 0.0f,
                                   px / uScale + uOff,
                                   pz / vScale + vOff,
                                   tx, 0.0f, 0.0f, 1.0f));
    }

    if (n == 3)
    {
        if (normalY > 0.0f)
        { indices.push_back(base); indices.push_back(base+1); indices.push_back(base+2); }
        else
        { indices.push_back(base); indices.push_back(base+2); indices.push_back(base+1); }
        return;
    }

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
}

// ─── appendWallQuad ───────────────────────────────────────────────────────────────
//
// Phase 1F-A: per-end heights replace the single yFloor/yCeil pair.
// For flat walls (yFloor0==yFloor1, yCeil0==yCeil1) output is identical to old.
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
    float uvRotation = 0.0f) noexcept
{
    const glm::vec2 dir2D = p1World - p0World;
    const float     len   = std::sqrt(dir2D.x * dir2D.x + dir2D.y * dir2D.y);
    if (len < 1e-6f) return;

    const float invLen = 1.0f / len;
    const float tx = dir2D.x * invLen;
    const float tz = dir2D.y * invLen;
    const float nx = -tz;
    const float nz =  tx;

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
    verts.push_back(makeVertex(p0World.x, yFloor0, p0World.y, nx,0,nz,
                               rotU(u0,v_bl), rotV(u0,v_bl), tx,0,tz,1));
    verts.push_back(makeVertex(p1World.x, yFloor1, p1World.y, nx,0,nz,
                               rotU(u1,v_br), rotV(u1,v_br), tx,0,tz,1));
    verts.push_back(makeVertex(p1World.x, yCeil1,  p1World.y, nx,0,nz,
                               rotU(u1,v_tr), rotV(u1,v_tr), tx,0,tz,1));
    verts.push_back(makeVertex(p0World.x, yCeil0,  p0World.y, nx,0,nz,
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

        std::vector<glm::vec2> poly;
        poly.reserve(n);
        for (const auto& w : sector.walls) poly.push_back(w.p0);

        std::vector<float> floorH, ceilH;
        buildHeightArrays(sector, floorH, ceilH);

        // ── Floor ────────────────────────────────────────────────────────────
        if (sector.floorShape == FloorShape::VisualStairs && sector.stairProfile)
        {
            appendStairSurface(mesh.vertices, mesh.indices, poly,
                               sector.floorHeight, *sector.stairProfile,
                               1.0f, 1.0f, 0.0f, 0.0f);
        }
        else
        {
            appendHorizontalSurface(mesh.vertices, mesh.indices, poly,
                                    std::span<const float>(floorH),
                                    +1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
        }

        // ── Ceiling ────────────────────────────────────────────────────────────
        appendHorizontalSurface(mesh.vertices, mesh.indices, poly,
                                std::span<const float>(ceilH),
                                -1.0f, 1.0f, 1.0f, 0.0f, 0.0f);

        // ── Walls ────────────────────────────────────────────────────────────
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const Wall&       wall   = sector.walls[wi];
            const glm::vec2   wallP1 = sector.walls[(wi + 1) % n].p0;
            const std::size_t wiNext = (wi + 1) % n;

            const float yF0 = floorH[wi];    const float yC0 = ceilH[wi];
            const float yF1 = floorH[wiNext]; const float yC1 = ceilH[wiNext];

            const float uScale = (wall.uvScale.x > 0.0f) ? wall.uvScale.x : 1.0f;
            const float vScale = (wall.uvScale.y > 0.0f) ? wall.uvScale.y : 1.0f;

            if (wall.portalSectorId == INVALID_SECTOR_ID)
            {
                appendWallQuad(mesh.vertices, mesh.indices,
                               wall.p0, wallP1,
                               yF0, yC0, yF1, yC1,
                               uScale, vScale,
                               wall.uvOffset.x, wall.uvOffset.y,
                               wall.uvRotation);
            }
            else
            {
                const std::size_t adjId = wall.portalSectorId;
                if (adjId >= map.sectors.size()) { continue; }
                const Sector& adj = map.sectors[adjId];

                // Use adj scalar heights for strip bounds (Phase 1F-A).
                // Per-vertex cross-sector strips deferred to Phase 1F-B.
                const float adjCeil  = adj.ceilHeight;
                const float adjFloor = adj.floorHeight;

                if (yC0 > adjCeil || yC1 > adjCeil)
                {
                    // Clamp: where this sector's ceiling dips below adjCeil, the
                    // strip top is clamped to adjCeil (zero-height, invisible edge)
                    // rather than producing an inverted quad.
                    const float sF0 = adjCeil, sC0 = std::max(yC0, adjCeil);
                    const float sF1 = adjCeil, sC1 = std::max(yC1, adjCeil);
                    if (sC0 > sF0 || sC1 > sF1)
                        appendWallQuad(mesh.vertices, mesh.indices,
                                       wall.p0, wallP1,
                                       sF0, sC0, sF1, sC1,
                                       uScale, vScale,
                                       wall.uvOffset.x, wall.uvOffset.y,
                                       wall.uvRotation);
                }
                if (yF0 < adjFloor || yF1 < adjFloor)
                {
                    // Clamp: where this sector's floor rises above adjFloor, the
                    // strip bottom is clamped to adjFloor (zero-height, invisible edge)
                    // rather than producing an inverted quad.
                    const float sF0 = std::min(yF0, adjFloor), sC0 = adjFloor;
                    const float sF1 = std::min(yF1, adjFloor), sC1 = adjFloor;
                    if (sC0 > sF0 || sC1 > sF1)
                        appendWallQuad(mesh.vertices, mesh.indices,
                                       wall.p0, wallP1,
                                       sF0, sC0, sF1, sC1,
                                       uScale, vScale,
                                       wall.uvOffset.x, wall.uvOffset.y,
                                       wall.uvRotation);
                }
            }
        }
    }

    return result;
}

// ─── tessellateMapTagged ─────────────────────────────────────────────────────────────
//
// Groups every surface into per-materialId batches.

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

        std::vector<glm::vec2> poly;
        poly.reserve(n);
        for (const auto& w : sector.walls) poly.push_back(w.p0);

        std::vector<float> floorH, ceilH;
        buildHeightArrays(sector, floorH, ceilH);

        // ── Floor ─────────────────────────────────────────────────────────────────
        // When the floor is a portal, use the portal material UUID so the renderer
        // binds the correct transparent/grate texture.  The geometry is identical.
        {
            const daedalus::UUID& floorMat =
                (sector.floorPortalSectorId != INVALID_SECTOR_ID)
                ? sector.floorPortalMaterialId
                : sector.floorMaterialId;

            if (sector.floorShape == FloorShape::VisualStairs && sector.stairProfile)
            {
                TaggedMeshBatch& batch = getBatch(floorMat);
                appendStairSurface(batch.mesh.vertices, batch.mesh.indices, poly,
                                   sector.floorHeight, *sector.stairProfile,
                                   1.0f, 1.0f, 0.0f, 0.0f);
            }
            else
            {
                TaggedMeshBatch& batch = getBatch(floorMat);
                appendHorizontalSurface(batch.mesh.vertices, batch.mesh.indices, poly,
                                        std::span<const float>(floorH),
                                        +1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
            }
        }

        // ── Ceiling ────────────────────────────────────────────────────────────
        {
            const daedalus::UUID& ceilMat =
                (sector.ceilPortalSectorId != INVALID_SECTOR_ID)
                ? sector.ceilPortalMaterialId
                : sector.ceilMaterialId;
            TaggedMeshBatch& batch = getBatch(ceilMat);
            appendHorizontalSurface(batch.mesh.vertices, batch.mesh.indices, poly,
                                    std::span<const float>(ceilH),
                                    -1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
        }

        // ── Walls ────────────────────────────────────────────────────────────────
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const Wall&       wall   = sector.walls[wi];
            const glm::vec2   wallP1 = sector.walls[(wi + 1) % n].p0;
            const std::size_t wiNext = (wi + 1) % n;

            const float yF0 = floorH[wi];    const float yC0 = ceilH[wi];
            const float yF1 = floorH[wiNext]; const float yC1 = ceilH[wiNext];

            const float uScale = (wall.uvScale.x > 0.0f) ? wall.uvScale.x : 1.0f;
            const float vScale = (wall.uvScale.y > 0.0f) ? wall.uvScale.y : 1.0f;

            if (wall.portalSectorId == INVALID_SECTOR_ID)
            {
                TaggedMeshBatch& batch = getBatch(wall.frontMaterialId);
                appendWallQuad(batch.mesh.vertices, batch.mesh.indices,
                               wall.p0, wallP1,
                               yF0, yC0, yF1, yC1,
                               uScale, vScale,
                               wall.uvOffset.x, wall.uvOffset.y,
                               wall.uvRotation);
            }
            else
            {
                const std::size_t adjId = wall.portalSectorId;
                if (adjId >= map.sectors.size()) continue;
                const Sector& adj = map.sectors[adjId];

                const float adjCeil  = adj.ceilHeight;
                const float adjFloor = adj.floorHeight;

                if (yC0 > adjCeil || yC1 > adjCeil)
                {
                    const float sF0 = adjCeil, sC0 = std::max(yC0, adjCeil);
                    const float sF1 = adjCeil, sC1 = std::max(yC1, adjCeil);
                    if (sC0 > sF0 || sC1 > sF1)
                    {
                        TaggedMeshBatch& batch = getBatch(wall.upperMaterialId);
                        appendWallQuad(batch.mesh.vertices, batch.mesh.indices,
                                       wall.p0, wallP1,
                                       sF0, sC0, sF1, sC1,
                                       uScale, vScale,
                                       wall.uvOffset.x, wall.uvOffset.y,
                                       wall.uvRotation);
                    }
                }
                if (yF0 < adjFloor || yF1 < adjFloor)
                {
                    const float sF0 = std::min(yF0, adjFloor), sC0 = adjFloor;
                    const float sF1 = std::min(yF1, adjFloor), sC1 = adjFloor;
                    if (sC0 > sF0 || sC1 > sF1)
                    {
                        TaggedMeshBatch& batch = getBatch(wall.lowerMaterialId);
                        appendWallQuad(batch.mesh.vertices, batch.mesh.indices,
                                       wall.p0, wallP1,
                                       sF0, sC0, sF1, sC1,
                                       uScale, vScale,
                                       wall.uvOffset.x, wall.uvOffset.y,
                                       wall.uvRotation);
                    }
                }
            }
        }
    }

    return result;
}

} // namespace daedalus::world
