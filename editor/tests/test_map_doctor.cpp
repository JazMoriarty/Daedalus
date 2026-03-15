#include "document/map_doctor.h"

#include <gtest/gtest.h>

using namespace daedalus::editor;
using namespace daedalus::world;

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// Returns a valid axis-aligned square sector (4 CCW walls, no portals).
static Sector makeSquare(float size = 4.0f, glm::vec2 origin = {0.0f, 0.0f})
{
    Sector s;
    s.walls.push_back(Wall{.p0 = origin + glm::vec2{0,    0}});
    s.walls.push_back(Wall{.p0 = origin + glm::vec2{size, 0}});
    s.walls.push_back(Wall{.p0 = origin + glm::vec2{size, size}});
    s.walls.push_back(Wall{.p0 = origin + glm::vec2{0,    size}});
    return s;
}

static bool hasIssueContaining(const std::vector<MapIssue>& issues,
                                const std::string&           substr)
{
    for (const auto& issue : issues)
        if (issue.description.find(substr) != std::string::npos)
            return true;
    return false;
}

// ─── Clean map ────────────────────────────────────────────────────────────────

TEST(MapDoctor, EmptyMapHasNoIssues)
{
    WorldMapData map;
    EXPECT_TRUE(diagnose(map).empty());
}

TEST(MapDoctor, SingleValidSectorHasNoIssues)
{
    WorldMapData map;
    map.sectors.push_back(makeSquare());
    EXPECT_TRUE(diagnose(map).empty());
}

// ─── Too few walls ────────────────────────────────────────────────────────────

TEST(MapDoctor, TwoWallSectorReported)
{
    WorldMapData map;
    Sector s;
    s.walls.push_back(Wall{.p0 = {0, 0}});
    s.walls.push_back(Wall{.p0 = {1, 0}});
    map.sectors.push_back(s);

    const auto issues = diagnose(map);
    ASSERT_FALSE(issues.empty());
    EXPECT_TRUE(hasIssueContaining(issues, "fewer than 3"));
    ASSERT_TRUE(issues[0].jumpTo.has_value());
    EXPECT_EQ(issues[0].jumpTo->type, SelectionType::Sector);
    EXPECT_EQ(issues[0].jumpTo->sectors.front(), 0u);
}

TEST(MapDoctor, ZeroWallSectorReported)
{
    WorldMapData map;
    map.sectors.push_back(Sector{});   // no walls
    const auto issues = diagnose(map);
    EXPECT_TRUE(hasIssueContaining(issues, "fewer than 3"));
}

// ─── Zero-length wall ─────────────────────────────────────────────────────────

TEST(MapDoctor, ZeroLengthWallReported)
{
    WorldMapData map;
    Sector s = makeSquare();
    // Make wall 0 and wall 1 share the same p0 → wall 0 has zero length.
    s.walls[1].p0 = s.walls[0].p0;
    map.sectors.push_back(s);

    const auto issues = diagnose(map);
    EXPECT_TRUE(hasIssueContaining(issues, "zero-length"));
    // JumpTo should point at the wall.
    bool wallIssueFound = false;
    for (const auto& iss : issues)
        if (iss.description.find("zero-length") != std::string::npos &&
            iss.jumpTo.has_value() &&
            iss.jumpTo->type == SelectionType::Wall)
            wallIssueFound = true;
    EXPECT_TRUE(wallIssueFound);
}

// ─── Portal out of range ──────────────────────────────────────────────────────

TEST(MapDoctor, PortalToNonexistentSectorReported)
{
    WorldMapData map;
    Sector s = makeSquare();
    s.walls[0].portalSectorId = 99;  // Sector 99 does not exist.
    map.sectors.push_back(s);

    EXPECT_TRUE(hasIssueContaining(diagnose(map), "does not exist"));
}

// ─── Orphaned portal (no reverse link) ───────────────────────────────────────

TEST(MapDoctor, PortalWithNoReverseLinkReported)
{
    WorldMapData map;
    Sector s0 = makeSquare();
    Sector s1 = makeSquare(4.0f, {4.0f, 0.0f});
    // One-way link: s0→s1, but s1 has no wall pointing back.
    s0.walls[1].portalSectorId = 1;
    map.sectors.push_back(s0);
    map.sectors.push_back(s1);

    EXPECT_TRUE(hasIssueContaining(diagnose(map), "no reverse link"));
}

TEST(MapDoctor, ProperlyLinkedPortalsAreClean)
{
    WorldMapData map;
    Sector s0 = makeSquare();
    Sector s1 = makeSquare(4.0f, {4.0f, 0.0f});
    // Bi-directional portal (indices chosen to avoid other geometry issues).
    s0.walls[1].portalSectorId = 1;
    s1.walls[3].portalSectorId = 0;
    map.sectors.push_back(s0);
    map.sectors.push_back(s1);

    // No portal-related issues.
    EXPECT_FALSE(hasIssueContaining(diagnose(map), "portal"));
}

// ─── Self-intersecting ────────────────────────────────────────────────────────

TEST(MapDoctor, SelfIntersectingBowTieReported)
{
    WorldMapData map;
    Sector s;
    // Bowtie polygon — definitely self-intersecting.
    s.walls.push_back(Wall{.p0 = {0, 0}});
    s.walls.push_back(Wall{.p0 = {4, 4}});
    s.walls.push_back(Wall{.p0 = {4, 0}});
    s.walls.push_back(Wall{.p0 = {0, 4}});
    map.sectors.push_back(s);

    EXPECT_TRUE(hasIssueContaining(diagnose(map), "self-intersecting"));
}

TEST(MapDoctor, ConvexPolygonIsNotSelfIntersecting)
{
    WorldMapData map;
    map.sectors.push_back(makeSquare());
    EXPECT_FALSE(hasIssueContaining(diagnose(map), "self-intersecting"));
}

// ─── Winding order ──────────────────────────────────────────────────────────────

TEST(MapDoctor, ClockwiseWindingReported)
{
    WorldMapData map;
    Sector s;
    // CW square: same vertices as makeSquare but in reverse order.
    s.walls.push_back(Wall{.p0 = {0, 0}});
    s.walls.push_back(Wall{.p0 = {0, 4}});
    s.walls.push_back(Wall{.p0 = {4, 4}});
    s.walls.push_back(Wall{.p0 = {4, 0}});
    map.sectors.push_back(s);
    EXPECT_TRUE(hasIssueContaining(diagnose(map), "clockwise"));
}

TEST(MapDoctor, CCWWindingIsClean)
{
    WorldMapData map;
    map.sectors.push_back(makeSquare());
    EXPECT_FALSE(hasIssueContaining(diagnose(map), "clockwise"));
}

// ─── Portal geometry mismatch ───────────────────────────────────────────────────

TEST(MapDoctor, PortalGeomMismatchReported)
{
    WorldMapData map;
    Sector s0 = makeSquare(4.0f, {0.0f, 0.0f});
    Sector s1 = makeSquare(4.0f, {4.0f, 0.0f});
    // s0 wall 0 (bottom edge: 0,0→4,0) links to s1 — geometrically wrong.
    // s1 wall 3 (left edge: 4,4→4,0) links back — so the reverse link exists.
    s0.walls[0].portalSectorId = 1;
    s1.walls[3].portalSectorId = 0;
    map.sectors.push_back(s0);
    map.sectors.push_back(s1);
    EXPECT_TRUE(hasIssueContaining(diagnose(map), "geometrically"));
}

TEST(MapDoctor, PortalGeomAlignedIsClean)
{
    WorldMapData map;
    Sector s0 = makeSquare(4.0f, {0.0f, 0.0f});
    Sector s1 = makeSquare(4.0f, {4.0f, 0.0f});
    // Correct: s0 wall 1 (right edge: 4,0→4,4) ↔ s1 wall 3 (left edge: 4,4→4,0).
    s0.walls[1].portalSectorId = 1;
    s1.walls[3].portalSectorId = 0;
    map.sectors.push_back(s0);
    map.sectors.push_back(s1);
    EXPECT_FALSE(hasIssueContaining(diagnose(map), "geometrically"));
}

// ─── Floor/ceiling inversion ─────────────────────────────────────────────────────

TEST(MapDoctor, FloorAboveCeilingReported)
{
    WorldMapData map;
    Sector s = makeSquare();
    s.floorHeight = 4.0f;
    s.ceilHeight  = 0.0f;
    map.sectors.push_back(s);
    EXPECT_TRUE(hasIssueContaining(diagnose(map), "floor"));
}

TEST(MapDoctor, FloorEqualsCeilingReported)
{
    WorldMapData map;
    Sector s = makeSquare();
    s.floorHeight = 2.0f;
    s.ceilHeight  = 2.0f;
    map.sectors.push_back(s);
    EXPECT_TRUE(hasIssueContaining(diagnose(map), "floor"));
}

TEST(MapDoctor, ValidFloorCeilingIsClean)
{
    WorldMapData map;
    map.sectors.push_back(makeSquare());  // default: floorHeight=0, ceilHeight=4
    EXPECT_FALSE(hasIssueContaining(diagnose(map), "floor"));
}

// ─── Duplicate non-adjacent vertices ──────────────────────────────────────────────

TEST(MapDoctor, DuplicateNonAdjacentVertexReported)
{
    WorldMapData map;
    Sector s = makeSquare();
    // Wall 2's p0 set to same position as wall 0's p0 — non-adjacent duplicate.
    s.walls[2].p0 = s.walls[0].p0;
    map.sectors.push_back(s);
    EXPECT_TRUE(hasIssueContaining(diagnose(map), "duplicate"));
}

TEST(MapDoctor, UniqueVerticesAreClean)
{
    WorldMapData map;
    map.sectors.push_back(makeSquare());
    EXPECT_FALSE(hasIssueContaining(diagnose(map), "duplicate"));
}

// ─── Sector overlap ────────────────────────────────────────────────────────────

TEST(MapDoctor, OverlappingSectorsReported)
{
    WorldMapData map;
    map.sectors.push_back(makeSquare(4.0f, {0.0f, 0.0f}));
    // Second square offset by (2,2) — overlaps first in the (2,2)→(4,4) region.
    map.sectors.push_back(makeSquare(4.0f, {2.0f, 2.0f}));
    EXPECT_TRUE(hasIssueContaining(diagnose(map), "overlap"));
}

TEST(MapDoctor, AdjacentSectorsDoNotOverlap)
{
    WorldMapData map;
    Sector s0 = makeSquare(4.0f, {0.0f, 0.0f});
    Sector s1 = makeSquare(4.0f, {4.0f, 0.0f});
    s0.walls[1].portalSectorId = 1;
    s1.walls[3].portalSectorId = 0;
    map.sectors.push_back(s0);
    map.sectors.push_back(s1);
    EXPECT_FALSE(hasIssueContaining(diagnose(map), "overlap"));
}
