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
