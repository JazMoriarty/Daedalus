// test_sector_tessellator.cpp
// Unit tests for the sector tessellator.

#include "daedalus/world/sector_tessellator.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace daedalus;
using namespace daedalus::world;

// ─── Helper: build a simple 4-wall rectangular sector ─────────────────────────

static WorldMapData makeSingleRoomMap(float halfX = 5.0f, float halfZ = 5.0f,
                                       float floor = 0.0f, float ceil = 4.0f)
{
    WorldMapData map;
    Sector sec;
    sec.floorHeight = floor;
    sec.ceilHeight  = ceil;

    // CCW from above (Y+): SW → SE → NE → NW
    Wall w0; w0.p0 = {-halfX, -halfZ}; sec.walls.push_back(w0);  // SW
    Wall w1; w1.p0 = { halfX, -halfZ}; sec.walls.push_back(w1);  // SE
    Wall w2; w2.p0 = { halfX,  halfZ}; sec.walls.push_back(w2);  // NE
    Wall w3; w3.p0 = {-halfX,  halfZ}; sec.walls.push_back(w3);  // NW

    map.sectors.push_back(std::move(sec));
    return map;
}

// ─── Tests ────────────────────────────────────────────────────────────────────

TEST(SectorTessellatorTest, SingleRoomVertexCount)
{
    const WorldMapData map = makeSingleRoomMap();
    const auto meshes = tessellateMap(map);

    ASSERT_EQ(meshes.size(), 1u);
    const render::MeshData& mesh = meshes[0];

    // 4-wall rectangle:
    //  floor:   4 vertices, 2 triangles (fan from v0)
    //  ceiling: 4 vertices, 2 triangles
    //  4 walls: 4 × 4 vertices, 4 × 2 triangles
    //  Total: 4+4+16 = 24 vertices, 4+4+16 = 24 indices * 1... no:
    //  Total indices: 2*3 + 2*3 + 4*(2*3) = 6+6+24 = 36
    ASSERT_EQ(mesh.vertices.size(), 24u);
    ASSERT_EQ(mesh.indices.size(),  36u);
}

TEST(SectorTessellatorTest, EmptyMapProducesEmptyResult)
{
    WorldMapData map;
    const auto meshes = tessellateMap(map);
    ASSERT_TRUE(meshes.empty());
}

TEST(SectorTessellatorTest, SectorWithTooFewWallsProducesEmptyMesh)
{
    WorldMapData map;
    Sector sec;
    sec.floorHeight = 0.0f; sec.ceilHeight = 3.0f;
    Wall w0; w0.p0 = {0,0}; sec.walls.push_back(w0);
    Wall w1; w1.p0 = {1,0}; sec.walls.push_back(w1);
    map.sectors.push_back(std::move(sec));

    const auto meshes = tessellateMap(map);
    ASSERT_EQ(meshes.size(), 1u);
    ASSERT_TRUE(meshes[0].vertices.empty());
    ASSERT_TRUE(meshes[0].indices.empty());
}

TEST(SectorTessellatorTest, FloorNormalPointsUp)
{
    const WorldMapData map = makeSingleRoomMap();
    const auto meshes = tessellateMap(map);
    ASSERT_FALSE(meshes.empty());

    // Floor vertices are the first 4 (appendHorizontalSurface floor first).
    for (u32 i = 0; i < 4; ++i)
    {
        const auto& v = meshes[0].vertices[i];
        EXPECT_NEAR(v.normal[0], 0.0f, 1e-5f) << "floor vertex " << i << " nx";
        EXPECT_NEAR(v.normal[1], 1.0f, 1e-5f) << "floor vertex " << i << " ny";
        EXPECT_NEAR(v.normal[2], 0.0f, 1e-5f) << "floor vertex " << i << " nz";
    }
}

TEST(SectorTessellatorTest, CeilingNormalPointsDown)
{
    const WorldMapData map = makeSingleRoomMap();
    const auto meshes = tessellateMap(map);
    ASSERT_FALSE(meshes.empty());

    // Ceiling vertices are the next 4 (vertices [4..7]).
    for (u32 i = 4; i < 8; ++i)
    {
        const auto& v = meshes[0].vertices[i];
        EXPECT_NEAR(v.normal[0],  0.0f, 1e-5f) << "ceiling vertex " << i << " nx";
        EXPECT_NEAR(v.normal[1], -1.0f, 1e-5f) << "ceiling vertex " << i << " ny";
        EXPECT_NEAR(v.normal[2],  0.0f, 1e-5f) << "ceiling vertex " << i << " nz";
    }
}

TEST(SectorTessellatorTest, WallNormalsAreUnitLength)
{
    const WorldMapData map = makeSingleRoomMap();
    const auto meshes = tessellateMap(map);
    ASSERT_FALSE(meshes.empty());

    // Skip floor (4) and ceiling (4), check all wall vertices.
    for (std::size_t i = 8; i < meshes[0].vertices.size(); ++i)
    {
        const auto& v = meshes[0].vertices[i];
        const float len = std::sqrt(v.normal[0]*v.normal[0] +
                                    v.normal[1]*v.normal[1] +
                                    v.normal[2]*v.normal[2]);
        EXPECT_NEAR(len, 1.0f, 1e-5f) << "wall vertex " << i << " normal length";
    }
}

TEST(SectorTessellatorTest, FloorHeightMatchesVertexY)
{
    constexpr float FLOOR = 1.5f;
    const WorldMapData map = makeSingleRoomMap(5, 5, FLOOR, 4.0f);
    const auto meshes = tessellateMap(map);
    ASSERT_FALSE(meshes.empty());

    // First 4 vertices are the floor at FLOOR height.
    for (u32 i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(meshes[0].vertices[i].pos[1], FLOOR, 1e-5f)
            << "floor vertex " << i << " Y";
    }
}

// ─── Phase 1F-A tests ───────────────────────────────────────────────────────────────

// Helper: build a map with a single sector from an explicit vertex list.
static WorldMapData makePolygonMap(std::vector<glm::vec2> pts,
                                   float floor = 0.0f, float ceil = 4.0f)
{
    WorldMapData map;
    Sector sec;
    sec.floorHeight = floor;
    sec.ceilHeight  = ceil;
    for (const auto& p : pts)
    {
        Wall w; w.p0 = p;
        sec.walls.push_back(w);
    }
    map.sectors.push_back(std::move(sec));
    return map;
}

// Γ-shaped (concave) 6-vertex polygon.
// Vertices (CCW from above):  (0,0) → (4,0) → (4,4) → (2,4) → (2,2) → (0,2)
// Vertex (2,2) is reflex — the old triangle fan from v0 would produce a
// triangle that exits the polygon.  Ear-clipping must handle this correctly.
TEST(SectorTessellatorTest, ConcaveGammaShapeVertexCount)
{
    // Reversed-L (Gamma) polygon, CCW.
    const WorldMapData map = makePolygonMap({
        {0,0}, {4,0}, {4,4}, {2,4}, {2,2}, {0,2}
    });
    const auto meshes = tessellateMap(map);
    ASSERT_EQ(meshes.size(), 1u);
    const render::MeshData& mesh = meshes[0];

    // N=6 polygon:
    //   floor:   6 vertices, (6-2)*3 = 12 indices
    //   ceiling: 6 vertices, 12 indices
    //   6 walls: 6*4 = 24 vertices, 6*6 = 36 indices
    //   Total: 36v, 60 indices
    EXPECT_EQ(mesh.vertices.size(), 36u);
    EXPECT_EQ(mesh.indices.size(),  60u);

    // All floor normals must point up.
    for (u32 i = 0; i < 6; ++i)
    {
        EXPECT_NEAR(mesh.vertices[i].normal[1], 1.0f, 1e-5f)
            << "floor vertex " << i << " normal Y";
    }
}

// Per-vertex floor height overrides: check that each floor vertex Y matches
// the override set on its corresponding wall.
TEST(SectorTessellatorTest, SlopedFloorPerVertexHeights)
{
    WorldMapData map;
    Sector sec;
    sec.floorHeight = 0.0f;
    sec.ceilHeight  = 4.0f;

    // 4-wall rectangle.  Floor heights: SW=0, SE=1, NE=2, NW=1 (rising slope).
    Wall w0; w0.p0 = {-5,-5}; w0.floorHeightOverride = 0.0f; sec.walls.push_back(w0);
    Wall w1; w1.p0 = { 5,-5}; w1.floorHeightOverride = 1.0f; sec.walls.push_back(w1);
    Wall w2; w2.p0 = { 5, 5}; w2.floorHeightOverride = 2.0f; sec.walls.push_back(w2);
    Wall w3; w3.p0 = {-5, 5}; w3.floorHeightOverride = 1.0f; sec.walls.push_back(w3);
    map.sectors.push_back(std::move(sec));

    const auto meshes = tessellateMap(map);
    ASSERT_EQ(meshes.size(), 1u);
    const render::MeshData& mesh = meshes[0];

    // Floor vertices are [0..3].  Their Y values must match the overrides.
    const float expected[] = {0.0f, 1.0f, 2.0f, 1.0f};
    for (u32 i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(mesh.vertices[i].pos[1], expected[i], 1e-5f)
            << "floor vertex " << i << " Y";
    }
    // Ceiling vertices [4..7] must still be at scalar ceilHeight (4.0) since
    // no ceilHeightOverride is set.
    for (u32 i = 4; i < 8; ++i)
    {
        EXPECT_NEAR(mesh.vertices[i].pos[1], 4.0f, 1e-5f)
            << "ceiling vertex " << i << " Y";
    }
}

// Trapezoidal wall quad: when a wall has different floor heights at each end,
// the wall geometry must reflect the slope.  Check the four wall vertex Y values.
TEST(SectorTessellatorTest, TrapezoidalWallBottomVerticesMatchOverrides)
{
    WorldMapData map;
    Sector sec;
    sec.floorHeight = 0.0f;
    sec.ceilHeight  = 3.0f;

    // Single solid wall from (0,0) to (4,0) with a ramp:
    //   wall[0].p0=(0,0), floorOverride=0.0
    //   wall[1].p0=(4,0), floorOverride=2.0  <- end vertex
    //   wall[2].p0=(4,4): no override, falls back to sector.floorHeight=0
    //   wall[3].p0=(0,4): no override
    Wall w0; w0.p0 = {0,0}; w0.floorHeightOverride = 0.0f; sec.walls.push_back(w0);
    Wall w1; w1.p0 = {4,0}; w1.floorHeightOverride = 2.0f; sec.walls.push_back(w1);
    Wall w2; w2.p0 = {4,4}; sec.walls.push_back(w2);
    Wall w3; w3.p0 = {0,4}; sec.walls.push_back(w3);
    map.sectors.push_back(std::move(sec));

    const auto meshes = tessellateMap(map);
    ASSERT_FALSE(meshes[0].vertices.empty());

    // Wall 0 (from w0.p0 to w1.p0) occupies vertices [8..11] (floor 4v + ceil 4v first).
    // Bottom-left  (v8):  Y = floorH[0] = 0.0
    // Bottom-right (v9):  Y = floorH[1] = 2.0
    // Top-right    (v10): Y = ceilH[1]  = 3.0 (no override)
    // Top-left     (v11): Y = ceilH[0]  = 3.0
    const auto& verts = meshes[0].vertices;
    EXPECT_NEAR(verts[8].pos[1],  0.0f, 1e-5f) << "wall0 bottom-left Y";
    EXPECT_NEAR(verts[9].pos[1],  2.0f, 1e-5f) << "wall0 bottom-right Y";
    EXPECT_NEAR(verts[10].pos[1], 3.0f, 1e-5f) << "wall0 top-right Y";
    EXPECT_NEAR(verts[11].pos[1], 3.0f, 1e-5f) << "wall0 top-left Y";
}

// Sloped portal strip: when this sector has a sloped ceiling (via override)
// and shares a portal with a flat adjacent sector, the upper strip height
// must follow the sloped ceiling on this sector's side.
TEST(SectorTessellatorTest, SlopedPortalStripHeightFollowsCeilingOverride)
{
    WorldMapData map;

    // Sector 0: 4-wall box.  East wall is a portal to sector 1.
    // Ceiling override at east end: wall[1] ceilOverride=2.0 (lower at that vertex).
    // Sector default ceilHeight = 4.0 so west end stays at 4.0.
    Sector s0;
    s0.floorHeight = 0.0f;
    s0.ceilHeight  = 4.0f;
    Wall a0; a0.p0 = {-5,-5};                                    s0.walls.push_back(a0);
    Wall a1; a1.p0 = { 5,-5}; a1.ceilHeightOverride = 2.0f;
             a1.portalSectorId = 1u;                              s0.walls.push_back(a1);
    Wall a2; a2.p0 = { 5, 5};                                    s0.walls.push_back(a2);
    Wall a3; a3.p0 = {-5, 5};                                    s0.walls.push_back(a3);

    // Sector 1: smaller room adjacent to sector 0, flat ceiling at 3.0.
    // This triggers an upper strip on the portal wall (s0.ceilH > s1.ceilH).
    Sector s1;
    s1.floorHeight = 0.0f;
    s1.ceilHeight  = 3.0f;
    Wall b0; b0.p0 = { 5,-5}; b0.portalSectorId = 0u; s1.walls.push_back(b0);
    Wall b1; b1.p0 = {13,-5};                         s1.walls.push_back(b1);
    Wall b2; b2.p0 = {13, 5};                         s1.walls.push_back(b2);
    Wall b3; b3.p0 = { 5, 5};                         s1.walls.push_back(b3);

    map.sectors.push_back(std::move(s0));
    map.sectors.push_back(std::move(s1));

    const auto meshes = tessellateMap(map);
    ASSERT_EQ(meshes.size(), 2u);

    // Sector 0 vertex layout (wall loop order: a0, a1-portal, a2, a3):
    //   [0..3]   floor (4v)
    //   [4..7]   ceiling (4v)
    //   [8..11]  wall a0 solid (4v)
    //   [12..15] wall a1 upper strip (4v) — emitted during portal wall processing
    //   [16..19] wall a2 solid (4v)
    //   [20..23] wall a3 solid (4v)
    //   Total: 24v
    EXPECT_EQ(meshes[0].vertices.size(), 24u);

    // Strip for portal wall a1 (wi=1, p0=(5,-5), p1=(5,5)):
    //   sF0 = adjCeil = 3.0        (bottom of strip at p0)
    //   sF1 = adjCeil = 3.0        (bottom of strip at p1)
    //   sC0 = ceilH[1] = 2.0       (top of strip at p0 — sloped down via override)
    //   sC1 = ceilH[2] = 4.0       (top of strip at p1 — no override, sector default)
    // appendWallQuad order: bottom-left, bottom-right, top-right, top-left
    const auto& sv = meshes[0].vertices;
    EXPECT_NEAR(sv[12].pos[1], 3.0f, 1e-5f) << "strip bottom-left  (adjCeil at p0)";
    EXPECT_NEAR(sv[13].pos[1], 3.0f, 1e-5f) << "strip bottom-right (adjCeil at p1)";
    EXPECT_NEAR(sv[14].pos[1], 4.0f, 1e-5f) << "strip top-right    (sector ceilH at p1, no override)";
    EXPECT_NEAR(sv[15].pos[1], 2.0f, 1e-5f) << "strip top-left     (sloped ceilH at p0 via override)";
}

// Visual stairs: FloorShape::VisualStairs must produce strictly more floor
// geometry than the flat default for the same sector polygon.
TEST(SectorTessellatorTest, VisualStairsProducesMoreGeometryThanFlat)
{
    // Flat reference: 4-wall box, default FloorShape::Flat.
    const WorldMapData flatMap = makeSingleRoomMap(5.0f, 5.0f, 0.0f, 4.0f);
    const auto flatMeshes = tessellateMap(flatMap);
    ASSERT_EQ(flatMeshes.size(), 1u);
    const std::size_t flatFloorVerts = 4u;  // flat 4-vertex floor

    // Stair map: same 4-wall box, FloorShape::VisualStairs with 4 steps.
    WorldMapData stairMap;
    Sector sec;
    sec.floorHeight = 0.0f;
    sec.ceilHeight  = 4.0f;
    sec.floorShape  = FloorShape::VisualStairs;
    sec.stairProfile = StairProfile{ 4u, 0.25f, 1.0f, 0.0f };  // 4 steps running +X
    Wall w0; w0.p0 = {-5,-5}; sec.walls.push_back(w0);
    Wall w1; w1.p0 = { 5,-5}; sec.walls.push_back(w1);
    Wall w2; w2.p0 = { 5, 5}; sec.walls.push_back(w2);
    Wall w3; w3.p0 = {-5, 5}; sec.walls.push_back(w3);
    stairMap.sectors.push_back(std::move(sec));

    const auto stairMeshes = tessellateMap(stairMap);
    ASSERT_EQ(stairMeshes.size(), 1u);

    // The stair mesh must have more vertices than the flat mesh.
    EXPECT_GT(stairMeshes[0].vertices.size(), flatMeshes[0].vertices.size())
        << "VisualStairs should generate more floor geometry than FloorShape::Flat";
    // And more indices (more triangles).
    EXPECT_GT(stairMeshes[0].indices.size(), flatMeshes[0].indices.size())
        << "VisualStairs should generate more index entries than FloorShape::Flat";
}

// ─────────────────────────────────────────────────────────────────────────────
// Original tests (unchanged below)
// ─────────────────────────────────────────────────────────────────────────────

TEST(SectorTessellatorTest, PortalWallProducesNoFullQuad)
{
    WorldMapData map;
    Sector sec;
    sec.floorHeight = 0.0f;
    sec.ceilHeight  = 4.0f;

    // 4-wall box. Wall index 1 is a portal (same sector height = no strip).
    Wall w0; w0.p0 = {-5, -5}; sec.walls.push_back(w0);
    Wall w1; w1.p0 = { 5, -5}; w1.portalSectorId = 0u; sec.walls.push_back(w1);
    Wall w2; w2.p0 = { 5,  5}; sec.walls.push_back(w2);
    Wall w3; w3.p0 = {-5,  5}; sec.walls.push_back(w3);

    // Portal into sector 0 itself (degenerate, used only to count geometry).
    // Both sectors have the same floor/ceil so no strips are generated.
    map.sectors.push_back(sec);  // sector 0 with portal to 0 (same heights)

    const auto meshes = tessellateMap(map);
    ASSERT_EQ(meshes.size(), 1u);

    // 3 solid walls + 1 portal (no strips since same heights):
    // floor: 4v + ceiling: 4v + 3 walls × 4v = 20 vertices
    EXPECT_EQ(meshes[0].vertices.size(), 20u);
}
