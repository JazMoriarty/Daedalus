// test_pathfinder.cpp
// Unit tests for the sector-graph A* pathfinder.
//
// Map topology used in most tests (sectors as boxes in XZ):
//
//   Sector 0: x ∈ [0,10], z ∈ [0,10]   (start room)
//   Sector 1: x ∈ [10,20], z ∈ [0,10]  (middle room)
//   Sector 2: x ∈ [20,30], z ∈ [0,10]  (goal room)
//
// Portals (one-way in each direction):
//   s0 → s1 via wall edge (10,0)→(10,10)
//   s1 → s0 via wall edge (10,10)→(10,0)
//   s1 → s2 via wall edge (20,0)→(20,10)
//   s2 → s1 via wall edge (20,10)→(20,0)

#include "daedalus/world/i_pathfinder.h"
#include "daedalus/world/i_world_map.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

using namespace daedalus::world;

// ─── Map builders ─────────────────────────────────────────────────────────────

/// Three collinear rooms connected by portals: s0 → s1 → s2.
static WorldMapData makeLinearThreeRoomMap()
{
    WorldMapData map;

    // Sector 0: x ∈ [0,10], z ∈ [0,10], portal on east wall.
    Sector s0;
    s0.floorHeight = 0.0f; s0.ceilHeight = 4.0f;
    Wall s0w0; s0w0.p0 = { 0, 0};                          s0.walls.push_back(s0w0);
    Wall s0w1; s0w1.p0 = {10, 0}; s0w1.portalSectorId = 1; s0.walls.push_back(s0w1);
    Wall s0w2; s0w2.p0 = {10,10};                          s0.walls.push_back(s0w2);
    Wall s0w3; s0w3.p0 = { 0,10};                          s0.walls.push_back(s0w3);

    // Sector 1: x ∈ [10,20], z ∈ [0,10], portals on both east and west walls.
    Sector s1;
    s1.floorHeight = 0.0f; s1.ceilHeight = 4.0f;
    Wall s1w0; s1w0.p0 = {10, 0};                          s1.walls.push_back(s1w0);
    Wall s1w1; s1w1.p0 = {20, 0}; s1w1.portalSectorId = 2; s1.walls.push_back(s1w1);
    Wall s1w2; s1w2.p0 = {20,10};                          s1.walls.push_back(s1w2);
    Wall s1w3; s1w3.p0 = {10,10}; s1w3.portalSectorId = 0; s1.walls.push_back(s1w3);

    // Sector 2: x ∈ [20,30], z ∈ [0,10], portal on west wall.
    Sector s2;
    s2.floorHeight = 0.0f; s2.ceilHeight = 4.0f;
    Wall s2w0; s2w0.p0 = {20, 0};                          s2.walls.push_back(s2w0);
    Wall s2w1; s2w1.p0 = {30, 0};                          s2.walls.push_back(s2w1);
    Wall s2w2; s2w2.p0 = {30,10};                          s2.walls.push_back(s2w2);
    Wall s2w3; s2w3.p0 = {20,10}; s2w3.portalSectorId = 1; s2.walls.push_back(s2w3);

    map.sectors.push_back(std::move(s0));
    map.sectors.push_back(std::move(s1));
    map.sectors.push_back(std::move(s2));
    return map;
}

/// Two-room map with a Blocking portal — agent cannot traverse it.
static WorldMapData makeBlockedPortalMap()
{
    WorldMapData map;

    Sector s0;
    s0.floorHeight = 0.0f; s0.ceilHeight = 4.0f;
    Wall s0w0; s0w0.p0 = {0, 0};                                                          s0.walls.push_back(s0w0);
    Wall s0w1; s0w1.p0 = {10,0}; s0w1.portalSectorId = 1;
              s0w1.flags = WallFlags::Blocking;                                            s0.walls.push_back(s0w1);
    Wall s0w2; s0w2.p0 = {10,10};                                                         s0.walls.push_back(s0w2);
    Wall s0w3; s0w3.p0 = { 0,10};                                                         s0.walls.push_back(s0w3);

    Sector s1;
    s1.floorHeight = 0.0f; s1.ceilHeight = 4.0f;
    Wall s1w0; s1w0.p0 = {10, 0};                                                         s1.walls.push_back(s1w0);
    Wall s1w1; s1w1.p0 = {20, 0};                                                         s1.walls.push_back(s1w1);
    Wall s1w2; s1w2.p0 = {20,10};                                                         s1.walls.push_back(s1w2);
    Wall s1w3; s1w3.p0 = {10,10}; s1w3.portalSectorId = 0;                                s1.walls.push_back(s1w3);

    map.sectors.push_back(std::move(s0));
    map.sectors.push_back(std::move(s1));
    return map;
}

// ─── Test fixture ─────────────────────────────────────────────────────────────

class PathfinderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_pathfinder = makePathfinder();
    }

    std::unique_ptr<IPathfinder> m_pathfinder;
};

// ─── Same-sector trivial path ──────────────────────────────────────────────────

TEST_F(PathfinderTest, SameSectorTrivialPath)
{
    auto worldMap = makeWorldMap(makeLinearThreeRoomMap());

    // Start and goal both inside sector 0.
    const auto result = m_pathfinder->findPath(*worldMap,
                                               glm::vec2{2.0f, 5.0f},
                                               glm::vec2{8.0f, 5.0f});

    ASSERT_TRUE(result.reachable);
    ASSERT_EQ(result.waypoints.size(), 1u);
    EXPECT_EQ(result.waypoints.back().sectorId, 0u);
    EXPECT_FLOAT_EQ(result.waypoints.back().position.x, 8.0f);
    EXPECT_FLOAT_EQ(result.waypoints.back().position.y, 5.0f);
}

// ─── Adjacent sector — one portal hop ──────────────────────────────────────────

TEST_F(PathfinderTest, AdjacentSectorPath)
{
    auto worldMap = makeWorldMap(makeLinearThreeRoomMap());

    // Start in s0, goal in s1.
    const auto result = m_pathfinder->findPath(*worldMap,
                                               glm::vec2{5.0f, 5.0f},
                                               glm::vec2{15.0f, 5.0f});

    ASSERT_TRUE(result.reachable);
    ASSERT_GE(result.waypoints.size(), 1u);

    // Last waypoint must be the exact goal in s1 (sector index 1).
    EXPECT_EQ(result.waypoints.back().sectorId, 1u);
    EXPECT_FLOAT_EQ(result.waypoints.back().position.x, 15.0f);
    EXPECT_FLOAT_EQ(result.waypoints.back().position.y, 5.0f);
}

// ─── Two portal hops (s0 → s1 → s2) ──────────────────────────────────────────

TEST_F(PathfinderTest, TwoHopPath)
{
    auto worldMap = makeWorldMap(makeLinearThreeRoomMap());

    // Start in s0, goal in s2.
    const auto result = m_pathfinder->findPath(*worldMap,
                                               glm::vec2{5.0f, 5.0f},
                                               glm::vec2{25.0f, 5.0f});

    ASSERT_TRUE(result.reachable);
    ASSERT_GE(result.waypoints.size(), 2u);

    // Path must visit s1 and end in s2 (with goal XZ).
    bool visitsS1 = false;
    for (const auto& wp : result.waypoints) {
        if (wp.sectorId == 1u) { visitsS1 = true; }
    }
    EXPECT_TRUE(visitsS1) << "path must visit the middle sector";

    EXPECT_EQ(result.waypoints.back().sectorId, 2u);
    EXPECT_FLOAT_EQ(result.waypoints.back().position.x, 25.0f);
    EXPECT_FLOAT_EQ(result.waypoints.back().position.y, 5.0f);
}

// ─── Portal waypoints land on portal midpoints ────────────────────────────────

TEST_F(PathfinderTest, IntermediateWaypointAtPortalMidpoint)
{
    auto worldMap = makeWorldMap(makeLinearThreeRoomMap());

    // s0 centroid ≈ (5,5), s1 centroid ≈ (15,5), s2 centroid ≈ (25,5).
    // Portal s0→s1: edge (10,0)→(10,10), midpoint = (10,5).
    // Portal s1→s2: edge (20,0)→(20,10), midpoint = (20,5).

    const auto result = m_pathfinder->findPath(*worldMap,
                                               glm::vec2{5.0f, 5.0f},
                                               glm::vec2{25.0f, 5.0f});

    ASSERT_TRUE(result.reachable);
    ASSERT_GE(result.waypoints.size(), 3u);

    // First waypoint should be the portal midpoint entering s1 at x=10.
    EXPECT_NEAR(result.waypoints[0].position.x, 10.0f, 0.01f);
    // Second waypoint should be the portal midpoint entering s2 at x=20.
    EXPECT_NEAR(result.waypoints[1].position.x, 20.0f, 0.01f);
}

// ─── Unreachable goal (start outside all sectors) ─────────────────────────────

TEST_F(PathfinderTest, StartOutsideSectorsIsUnreachable)
{
    auto worldMap = makeWorldMap(makeLinearThreeRoomMap());

    // Start is outside all three rooms.
    const auto result = m_pathfinder->findPath(*worldMap,
                                               glm::vec2{-100.0f, 5.0f},
                                               glm::vec2{  25.0f, 5.0f});

    EXPECT_FALSE(result.reachable);
    EXPECT_TRUE(result.waypoints.empty());
}

// ─── Unreachable goal (goal outside all sectors) ──────────────────────────────

TEST_F(PathfinderTest, GoalOutsideSectorsIsUnreachable)
{
    auto worldMap = makeWorldMap(makeLinearThreeRoomMap());

    const auto result = m_pathfinder->findPath(*worldMap,
                                               glm::vec2{5.0f,    5.0f},
                                               glm::vec2{9000.0f, 5.0f});

    EXPECT_FALSE(result.reachable);
    EXPECT_TRUE(result.waypoints.empty());
}

// ─── Blocked portal prevents traversal ────────────────────────────────────────

TEST_F(PathfinderTest, BlockedPortalIsUnreachable)
{
    // The only portal between s0 and s1 has WallFlags::Blocking set.
    // s1's portal back to s0 is still open, but s0 can't reach s1.
    auto worldMap = makeWorldMap(makeBlockedPortalMap());

    const auto result = m_pathfinder->findPath(*worldMap,
                                               glm::vec2{5.0f,  5.0f},   // s0
                                               glm::vec2{15.0f, 5.0f});  // s1

    EXPECT_FALSE(result.reachable);
}

// ─── Reachable is true for connected path ─────────────────────────────────────

TEST_F(PathfinderTest, ReachableFlagCorrectOnSuccess)
{
    auto worldMap = makeWorldMap(makeLinearThreeRoomMap());

    const auto result = m_pathfinder->findPath(*worldMap,
                                               glm::vec2{5.0f, 5.0f},
                                               glm::vec2{25.0f, 5.0f});

    EXPECT_TRUE(result.reachable);
    EXPECT_FALSE(result.waypoints.empty());
}
