// test_portal_traversal.cpp
// Unit tests for the portal traversal algorithm.

#include "daedalus/world/i_portal_traversal.h"
#include "daedalus/world/i_world_map.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

using namespace daedalus;
using namespace daedalus::world;

// ─── Helper: build a 2-sector map ────────────────────────────────────────────
//
// Sector 0: x ∈ [-5, 5],  z ∈ [-5,  5]  (camera room)
// Sector 1: x ∈ [ 5, 13], z ∈ [-5,  5]  (adjacent room)
// Portal: sector-0 wall from (5,-5)→(5,5) → sector 1
//         sector-1 wall from (5, 5)→(5,-5) → sector 0
//
// From sector 0 looking towards +X, sector 1 is visible through the portal.
// From sector 1 looking towards -X, sector 0 is visible through the portal.

static WorldMapData makeTwoRoomMap()
{
    WorldMapData map;

    // Sector 0.
    Sector s0;
    s0.floorHeight = 0.0f; s0.ceilHeight = 4.0f;
    Wall s0w0; s0w0.p0 = {-5, -5};                         s0.walls.push_back(s0w0);
    Wall s0w1; s0w1.p0 = { 5, -5}; s0w1.portalSectorId=1u; s0.walls.push_back(s0w1);
    Wall s0w2; s0w2.p0 = { 5,  5};                         s0.walls.push_back(s0w2);
    Wall s0w3; s0w3.p0 = {-5,  5};                         s0.walls.push_back(s0w3);

    // Sector 1.
    Sector s1;
    s1.floorHeight = 0.0f; s1.ceilHeight = 4.0f;
    Wall s1w0; s1w0.p0 = { 5, -5};                         s1.walls.push_back(s1w0);
    Wall s1w1; s1w1.p0 = {13, -5};                         s1.walls.push_back(s1w1);
    Wall s1w2; s1w2.p0 = {13,  5};                         s1.walls.push_back(s1w2);
    Wall s1w3; s1w3.p0 = { 5,  5}; s1w3.portalSectorId=0u; s1.walls.push_back(s1w3);

    map.sectors.push_back(std::move(s0));
    map.sectors.push_back(std::move(s1));
    return map;
}

// ─── Helper: build a viewProj looking from eye toward target ──────────────────

static glm::mat4 makeViewProj(glm::vec3 eye, glm::vec3 target)
{
    const glm::mat4 view = glm::lookAtLH(eye, target, glm::vec3(0,1,0));
    const glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(90.0f), 16.0f/9.0f,
                                                   0.1f, 100.0f);
    return proj * view;
}

// ─── Tests ────────────────────────────────────────────────────────────────────

class PortalTraversalTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_map       = makeWorldMap(makeTwoRoomMap());
        m_traversal = makePortalTraversal();
    }

    std::unique_ptr<IWorldMap>        m_map;
    std::unique_ptr<IPortalTraversal> m_traversal;

    [[nodiscard]] bool visibleContains(const std::vector<VisibleSector>& vis,
                                        SectorId sid) const
    {
        return std::ranges::any_of(vis,
            [sid](const VisibleSector& vs) { return vs.sectorId == sid; });
    }
};

TEST_F(PortalTraversalTest, InvalidCameraReturnsEmpty)
{
    const auto vp = makeViewProj({0,2,0}, {1,2,0});
    const auto vis = m_traversal->traverse(*m_map, INVALID_SECTOR_ID, vp);
    EXPECT_TRUE(vis.empty());
}

TEST_F(PortalTraversalTest, CameraSectorAlwaysIncluded)
{
    // Camera in sector 0, looking in any direction.
    const auto vp = makeViewProj({0,2,0}, {-1,2,0});
    const auto vis = m_traversal->traverse(*m_map, 0u, vp);
    ASSERT_FALSE(vis.empty());
    EXPECT_EQ(vis[0].sectorId, 0u);  // own sector first
}

TEST_F(PortalTraversalTest, BothSectorsVisibleThroughPortal)
{
    // Camera at sector-0 centre, looking toward +X (through the portal).
    const auto vp = makeViewProj({0,2,0}, {1,2,0});
    const auto vis = m_traversal->traverse(*m_map, 0u, vp);

    EXPECT_TRUE(visibleContains(vis, 0u)) << "camera sector 0 should be visible";
    EXPECT_TRUE(visibleContains(vis, 1u)) << "sector 1 should be visible through portal";
}

TEST_F(PortalTraversalTest, OnlyCameraSectorVisibleWhenFacingAway)
{
    // Camera at sector-0 centre, looking toward -X (away from the portal at +X).
    const auto vp = makeViewProj({0,2,0}, {-1,2,0});
    const auto vis = m_traversal->traverse(*m_map, 0u, vp);

    EXPECT_TRUE(visibleContains(vis, 0u))  << "camera sector should be visible";
    EXPECT_FALSE(visibleContains(vis, 1u)) << "sector 1 should not be visible";
}

TEST_F(PortalTraversalTest, VisitedSetPreventsDuplicates)
{
    // Camera sees both sectors; verify neither appears twice.
    const auto vp = makeViewProj({0,2,0}, {1,2,0});
    const auto vis = m_traversal->traverse(*m_map, 0u, vp);

    std::vector<SectorId> ids;
    for (const auto& vs : vis) { ids.push_back(vs.sectorId); }
    std::ranges::sort(ids);
    const bool noDuplicates = std::ranges::adjacent_find(ids) == ids.end();
    EXPECT_TRUE(noDuplicates);
}

TEST_F(PortalTraversalTest, MaxDepthZeroReturnsOnlyCameraSector)
{
    const auto vp = makeViewProj({0,2,0}, {1,2,0});
    // maxDepth=0 means we process the camera sector but do not recurse.
    const auto vis = m_traversal->traverse(*m_map, 0u, vp, 0u);
    EXPECT_EQ(vis.size(), 1u);
    EXPECT_EQ(vis[0].sectorId, 0u);
}

TEST_F(PortalTraversalTest, FindSectorReturnsCorrectId)
{
    // Point in sector 0.
    EXPECT_EQ(m_map->findSector({0.0f, 0.0f}), 0u);
    // Point in sector 1 (x ∈ [5,13]).
    EXPECT_EQ(m_map->findSector({9.0f, 0.0f}), 1u);
    // Point outside all sectors.
    EXPECT_EQ(m_map->findSector({100.0f, 0.0f}), INVALID_SECTOR_ID);
}
