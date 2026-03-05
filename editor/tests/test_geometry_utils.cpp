#include "tools/geometry_utils.h"

#include <gtest/gtest.h>
#include <cmath>

using namespace daedalus::editor::geometry;

// ─── segmentsIntersect ────────────────────────────────────────────────────────

TEST(SegmentsIntersect, CrossingSegments)
{
    // Classic X cross.
    EXPECT_TRUE(segmentsIntersect({-1, 0}, {1, 0},
                                   {0, -1}, {0, 1}));
}

TEST(SegmentsIntersect, ParallelSegments)
{
    EXPECT_FALSE(segmentsIntersect({0, 0}, {2, 0},
                                    {0, 1}, {2, 1}));
}

TEST(SegmentsIntersect, SharedEndpoint)
{
    // Endpoint touch is NOT an intersection (shares vertex).
    EXPECT_FALSE(segmentsIntersect({0, 0}, {1, 0},
                                    {1, 0}, {1, 1}));
}

TEST(SegmentsIntersect, TShapeNoProperCross)
{
    // T-shape: one segment's endpoint lies on the other — not a proper crossing.
    EXPECT_FALSE(segmentsIntersect({0, 0}, {2, 0},
                                    {1, 0}, {1, 1}));
}

TEST(SegmentsIntersect, NonIntersectingDiagonals)
{
    EXPECT_FALSE(segmentsIntersect({0, 0}, {1, 1},
                                    {2, 0}, {3, 1}));
}

// ─── isSelfIntersecting ───────────────────────────────────────────────────────

TEST(IsSelfIntersecting, Triangle)
{
    // A triangle can never self-intersect.
    EXPECT_FALSE(isSelfIntersecting({{0,0},{2,0},{1,2}}));
}

TEST(IsSelfIntersecting, ConvexQuad)
{
    EXPECT_FALSE(isSelfIntersecting({{0,0},{4,0},{4,4},{0,4}}));
}

TEST(IsSelfIntersecting, BowTie)
{
    // Bowtie — definitely self-intersecting.
    EXPECT_TRUE(isSelfIntersecting({{0,0},{4,4},{4,0},{0,4}}));
}

TEST(IsSelfIntersecting, LShape)
{
    // Non-convex L — not self-intersecting.
    EXPECT_FALSE(isSelfIntersecting(
        {{0,0},{2,0},{2,1},{1,1},{1,2},{0,2}}));
}

// ─── signedArea ───────────────────────────────────────────────────────────────

TEST(SignedArea, CCWSquare)
{
    // CCW square: signed area should be positive and equal to 4.
    const float area = signedArea({{0,0},{2,0},{2,2},{0,2}});
    EXPECT_NEAR(area, 4.0f, 1e-4f);
}

TEST(SignedArea, CWSquare)
{
    // CW square: signed area should be negative.
    const float area = signedArea({{0,0},{0,2},{2,2},{2,0}});
    EXPECT_NEAR(area, -4.0f, 1e-4f);
}

TEST(SignedArea, Triangle)
{
    // Right triangle with legs of length 3 and 4: area = 6.
    const float area = signedArea({{0,0},{3,0},{0,4}});
    EXPECT_NEAR(std::abs(area), 6.0f, 1e-4f);
}

TEST(SignedArea, Degenerate)
{
    EXPECT_NEAR(signedArea({}),        0.0f, 1e-6f);
    EXPECT_NEAR(signedArea({{1,1}}),   0.0f, 1e-6f);
    EXPECT_NEAR(signedArea({{0,0},{1,1}}), 0.0f, 1e-6f);
}

// ─── pointInPolygon ───────────────────────────────────────────────────────────

TEST(PointInPolygon, InsideSquare)
{
    const std::vector<glm::vec2> sq = {{0,0},{4,0},{4,4},{0,4}};
    EXPECT_TRUE (pointInPolygon({2,  2},  sq));
    EXPECT_FALSE(pointInPolygon({5,  2},  sq));
    EXPECT_FALSE(pointInPolygon({-1, 2},  sq));
    EXPECT_FALSE(pointInPolygon({2, -1},  sq));
    EXPECT_FALSE(pointInPolygon({2,  5},  sq));
}

TEST(PointInPolygon, InsideTriangle)
{
    const std::vector<glm::vec2> tri = {{0,0},{6,0},{3,6}};
    EXPECT_TRUE (pointInPolygon({3, 2}, tri));
    EXPECT_FALSE(pointInPolygon({0, 6}, tri));
}

// ─── pointToSegmentDistSq ─────────────────────────────────────────────────────

TEST(PointToSegmentDistSq, ProjectsToMidpoint)
{
    // Point directly above midpoint of horizontal segment.
    const float d = pointToSegmentDistSq({2, 3}, {0, 0}, {4, 0});
    EXPECT_NEAR(d, 9.0f, 1e-4f);  // sqrt(9) = 3 units above
}

TEST(PointToSegmentDistSq, ClampedToEndpointA)
{
    // Point behind start: nearest is endpoint a.
    const float d = pointToSegmentDistSq({-1, 0}, {0, 0}, {4, 0});
    EXPECT_NEAR(d, 1.0f, 1e-4f);
}

TEST(PointToSegmentDistSq, ClampedToEndpointB)
{
    // Point beyond end: nearest is endpoint b.
    const float d = pointToSegmentDistSq({6, 0}, {0, 0}, {4, 0});
    EXPECT_NEAR(d, 4.0f, 1e-4f);
}

TEST(PointToSegmentDistSq, PointOnSegment)
{
    const float d = pointToSegmentDistSq({2, 0}, {0, 0}, {4, 0});
    EXPECT_NEAR(d, 0.0f, 1e-6f);
}

TEST(PointToSegmentDistSq, DegenerateSegment)
{
    // Degenerate: a == b — behaves as distance to point a.
    const float d = pointToSegmentDistSq({3, 4}, {0, 0}, {0, 0});
    EXPECT_NEAR(d, 25.0f, 1e-4f);
}
