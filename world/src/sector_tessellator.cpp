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
//   Ceiling → (0,-1,0)
//   Walls   → inward-facing horizontal normal computed from wall direction
//
// UV mapping:
//   Floor / ceiling: u = x / uvScale.x,  v = z / uvScale.y  (+ uvOffset)
//   Walls:           u = along-wall distance / uvScale.x,
//                    v = (height - floorHeight) / uvScale.y  (+ uvOffset)

#include "daedalus/world/sector_tessellator.h"

#include <cmath>
#include <unordered_map>

namespace daedalus::world
{

namespace
{

// ─── Helpers ──────────────────────────────────────────────────────────────────

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

// Append a triangle fan from vertex 0 of the polygon vertices for a flat surface.
// polygonPts: N 2D points (x, z) in CCW order.
// y:         constant height of this surface.
// normalY:   +1.0 for floor (normal up), -1.0 for ceiling (normal down).
// tangent:   tangent direction (xyz + handedness w).
// uScale, vScale: UV scale.
// uOff, vOff: UV offset.
// Appends to verts and indices, using the current index base.
void appendHorizontalSurface(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    const std::vector<glm::vec2>&          polygonPts,
    float y,
    float normalY,    // +1 floor, -1 ceiling
    float uScale, float vScale,
    float uOff,   float vOff) noexcept
{
    const u32 base  = static_cast<u32>(verts.size());
    const u32 n     = static_cast<u32>(polygonPts.size());

    // Tangent along world +X (for floor) or -X (for ceiling, flipped winding).
    const float tx = (normalY > 0.0f) ? 1.0f : -1.0f;

    for (u32 i = 0; i < n; ++i)
    {
        const float px = polygonPts[i].x;
        const float pz = polygonPts[i].y;
        const float u  = px / uScale + uOff;
        const float v  = pz / vScale + vOff;
        verts.push_back(makeVertex(px, y, pz,
                                   0.0f, normalY, 0.0f,
                                   u, v,
                                   tx, 0.0f, 0.0f, 1.0f));
    }

    // Triangle fan from v0: (0,1,2), (0,2,3), ..., (0,n-2,n-1)
    // For ceiling we emit the fan in reverse order to preserve inward-facing CCW.
    if (normalY > 0.0f)
    {
        for (u32 i = 1; i + 1 < n; ++i)
        {
            indices.push_back(base);
            indices.push_back(base + i);
            indices.push_back(base + i + 1);
        }
    }
    else
    {
        for (u32 i = 1; i + 1 < n; ++i)
        {
            indices.push_back(base);
            indices.push_back(base + i + 1);
            indices.push_back(base + i);
        }
    }
}

// Append a wall quad (two triangles) between two 3D points at floor and ceiling.
// p0World, p1World: XZ positions of the wall start and end.
// yFloor, yCeil: bottom and top heights.
// The inward normal points into the sector (computed as the perpendicular that
// points left relative to the wall direction, which for CCW sectors is inward).
// uScale, vScale, uOff, vOff: UV parameters.
void appendWallQuad(
    std::vector<render::StaticMeshVertex>& verts,
    std::vector<u32>&                      indices,
    glm::vec2 p0World, glm::vec2 p1World,
    float yFloor, float yCeil,
    float uScale, float vScale,
    float uOff,   float vOff) noexcept
{
    // Wall direction and length (in XZ plane).
    const glm::vec2 dir2D   = p1World - p0World;
    const float     len     = std::sqrt(dir2D.x * dir2D.x + dir2D.y * dir2D.y);
    if (len < 1e-6f) { return; }

    const float invLen = 1.0f / len;
    // Normalised tangent (along wall, in XZ plane).
    const float tx = dir2D.x * invLen;
    const float tz = dir2D.y * invLen;

    // Inward normal: rotate tangent 90° CCW in XZ → points left = inward for
    // CCW winding as seen from above.  N = (-tz, 0, tx)
    const float nx = -tz;
    const float nz =  tx;

    // UV: u runs along wall length, v runs up the wall.
    // Wall height.
    const float height = yCeil - yFloor;

    const float u0 = 0.0f / uScale + uOff;
    const float u1 = len  / uScale + uOff;
    const float v0 = 0.0f   / vScale + vOff;
    const float v1 = height / vScale + vOff;

    const u32 base = static_cast<u32>(verts.size());

    // 4 vertices: bottom-left, bottom-right, top-right, top-left
    verts.push_back(makeVertex(p0World.x, yFloor, p0World.y, nx,0,nz, u0,v0, tx,0,tz,1));
    verts.push_back(makeVertex(p1World.x, yFloor, p1World.y, nx,0,nz, u1,v0, tx,0,tz,1));
    verts.push_back(makeVertex(p1World.x, yCeil,  p1World.y, nx,0,nz, u1,v1, tx,0,tz,1));
    verts.push_back(makeVertex(p0World.x, yCeil,  p0World.y, nx,0,nz, u0,v1, tx,0,tz,1));

    // Two triangles — winding must be CW in NDC so Metal's viewport Y-flip
    // produces CCW in framebuffer (= front-facing with MTLWindingCounterClockwise).
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 1);
    indices.push_back(base + 0);
    indices.push_back(base + 3);
    indices.push_back(base + 2);
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

        // Gather polygon points for floor/ceiling tessellation.
        std::vector<glm::vec2> poly;
        poly.reserve(n);
        for (const auto& w : sector.walls)
        {
            poly.push_back(w.p0);
        }

        // ── Floor ──────────────────────────────────────────────────────────────
        appendHorizontalSurface(mesh.vertices, mesh.indices, poly,
                                sector.floorHeight,
                                +1.0f,              // normal up
                                1.0f, 1.0f,         // UV scale
                                0.0f, 0.0f);        // UV offset

        // ── Ceiling ────────────────────────────────────────────────────────────
        appendHorizontalSurface(mesh.vertices, mesh.indices, poly,
                                sector.ceilHeight,
                                -1.0f,              // normal down
                                1.0f, 1.0f,
                                0.0f, 0.0f);

        // ── Walls ──────────────────────────────────────────────────────────────
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const Wall&     wall  = sector.walls[wi];
            const glm::vec2 wallP1 = sector.walls[(wi + 1) % n].p0;

            const float uScale = (wall.uvScale.x > 0.0f) ? wall.uvScale.x : 1.0f;
            const float vScale = (wall.uvScale.y > 0.0f) ? wall.uvScale.y : 1.0f;

            if (wall.portalSectorId == INVALID_SECTOR_ID)
            {
                // Solid wall: full height from floor to ceiling.
                appendWallQuad(mesh.vertices, mesh.indices,
                               wall.p0, wallP1,
                               sector.floorHeight, sector.ceilHeight,
                               uScale, vScale,
                               wall.uvOffset.x, wall.uvOffset.y);
            }
            else
            {
                // Portal wall: generate upper and/or lower strips.
                const std::size_t adjId = wall.portalSectorId;
                if (adjId >= map.sectors.size()) { continue; }
                const Sector& adj = map.sectors[adjId];

                // Upper strip: from adj_ceil to this_ceil (if adj is shorter).
                if (adj.ceilHeight < sector.ceilHeight)
                {
                    appendWallQuad(mesh.vertices, mesh.indices,
                                   wall.p0, wallP1,
                                   adj.ceilHeight, sector.ceilHeight,
                                   uScale, vScale,
                                   wall.uvOffset.x, wall.uvOffset.y);
                }

                // Lower strip: from this_floor to adj_floor (if adj is higher).
                if (adj.floorHeight > sector.floorHeight)
                {
                    appendWallQuad(mesh.vertices, mesh.indices,
                                   wall.p0, wallP1,
                                   sector.floorHeight, adj.floorHeight,
                                   uScale, vScale,
                                   wall.uvOffset.x, wall.uvOffset.y);
                }
            }
        }
    }

    return result;
}

// ─── tessellateMapTagged ──────────────────────────────────────────────────
//
// Groups every surface in each sector into per-materialId batches so the
// renderer can issue one draw call per material per sector and bind the
// correct texture from the MaterialCatalog.

std::vector<std::vector<TaggedMeshBatch>> tessellateMapTagged(const WorldMapData& map)
{
    std::vector<std::vector<TaggedMeshBatch>> result;
    result.resize(map.sectors.size());

    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const Sector& sector = map.sectors[si];
        const auto    n      = sector.walls.size();
        if (n < 3) continue;

        // Map UUID → index in result[si] for O(1) batch lookup.
        std::unordered_map<daedalus::UUID, std::size_t, daedalus::UUIDHash> uuidToIdx;

        // Return a reference to the batch for uuid, creating it if needed.
        // IMPORTANT: each returned reference is only used within its own block;
        // never hold two references simultaneously across a push_back.
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

        // Gather polygon points for floor/ceiling tessellation.
        std::vector<glm::vec2> poly;
        poly.reserve(n);
        for (const auto& w : sector.walls)
            poly.push_back(w.p0);

        // ── Floor ───────────────────────────────────────────────────────────────
        {
            TaggedMeshBatch& batch = getBatch(sector.floorMaterialId);
            appendHorizontalSurface(batch.mesh.vertices, batch.mesh.indices, poly,
                                    sector.floorHeight, +1.0f,
                                    1.0f, 1.0f, 0.0f, 0.0f);
        }

        // ── Ceiling ────────────────────────────────────────────────────────────
        {
            TaggedMeshBatch& batch = getBatch(sector.ceilMaterialId);
            appendHorizontalSurface(batch.mesh.vertices, batch.mesh.indices, poly,
                                    sector.ceilHeight, -1.0f,
                                    1.0f, 1.0f, 0.0f, 0.0f);
        }

        // ── Walls ────────────────────────────────────────────────────────────
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const Wall&     wall   = sector.walls[wi];
            const glm::vec2 wallP1 = sector.walls[(wi + 1) % n].p0;

            const float uScale = (wall.uvScale.x > 0.0f) ? wall.uvScale.x : 1.0f;
            const float vScale = (wall.uvScale.y > 0.0f) ? wall.uvScale.y : 1.0f;

            if (wall.portalSectorId == INVALID_SECTOR_ID)
            {
                // Solid wall: full height, frontMaterialId.
                TaggedMeshBatch& batch = getBatch(wall.frontMaterialId);
                appendWallQuad(batch.mesh.vertices, batch.mesh.indices,
                               wall.p0, wallP1,
                               sector.floorHeight, sector.ceilHeight,
                               uScale, vScale,
                               wall.uvOffset.x, wall.uvOffset.y);
            }
            else
            {
                const std::size_t adjId = wall.portalSectorId;
                if (adjId >= map.sectors.size()) continue;
                const Sector& adj = map.sectors[adjId];

                // Upper strip: upperMaterialId.
                if (adj.ceilHeight < sector.ceilHeight)
                {
                    TaggedMeshBatch& batch = getBatch(wall.upperMaterialId);
                    appendWallQuad(batch.mesh.vertices, batch.mesh.indices,
                                   wall.p0, wallP1,
                                   adj.ceilHeight, sector.ceilHeight,
                                   uScale, vScale,
                                   wall.uvOffset.x, wall.uvOffset.y);
                }

                // Lower strip: lowerMaterialId.
                if (adj.floorHeight > sector.floorHeight)
                {
                    TaggedMeshBatch& batch = getBatch(wall.lowerMaterialId);
                    appendWallQuad(batch.mesh.vertices, batch.mesh.indices,
                                   wall.p0, wallP1,
                                   sector.floorHeight, adj.floorHeight,
                                   uScale, vScale,
                                   wall.uvOffset.x, wall.uvOffset.y);
                }
            }
        }
    }

    return result;
}

} // namespace daedalus::world
