// test_transparent_pass.cpp
// Unit tests for render::sortTransparentDraws (scene_view.h).
//
// sortTransparentDraws is pure CPU logic — no GPU required — and must have
// unit test coverage per the spec §Testing rule:
//   "Pure logic (math, algorithms, data structures) must have unit tests."

#include <gtest/gtest.h>
#include "daedalus/render/scene_view.h"

#include <glm/glm.hpp>
#include <algorithm>

using namespace daedalus::render;

namespace
{

/// Helper: create a MeshDraw positioned at (x, y, z) with identity matrices.
MeshDraw makeDrawAt(float x, float y, float z)
{
    MeshDraw d;
    d.modelMatrix    = glm::mat4(1.0f);
    d.modelMatrix[3] = glm::vec4(x, y, z, 1.0f);  // translation column
    d.prevModel      = d.modelMatrix;
    return d;
}

/// Helper: extract world-space position from a draw's modelMatrix column 3.
glm::vec3 posOf(const MeshDraw& d)
{
    return glm::vec3(d.modelMatrix[3]);
}

} // namespace

// ─── Tests ────────────────────────────────────────────────────────────────────

TEST(SortTransparentDraws, EmptyListNoOp)
{
    SceneView scene;
    scene.cameraPos = glm::vec3(0.0f);
    // Must not crash or throw on empty list.
    EXPECT_NO_FATAL_FAILURE(sortTransparentDraws(scene));
    EXPECT_TRUE(scene.transparentDraws.empty());
}

TEST(SortTransparentDraws, SingleElementUnchanged)
{
    SceneView scene;
    scene.cameraPos = glm::vec3(0.0f);
    scene.transparentDraws.push_back(makeDrawAt(3.0f, 0.0f, 0.0f));

    sortTransparentDraws(scene);

    ASSERT_EQ(scene.transparentDraws.size(), 1u);
    EXPECT_NEAR(posOf(scene.transparentDraws[0]).x, 3.0f, 1e-5f);
}

TEST(SortTransparentDraws, ThreeDrawsBackToFront)
{
    // Camera at origin.  Draws at distances 1, 3, 2 from camera (along X).
    // Expected sorted order (back-to-front): distance 3, then 2, then 1.
    SceneView scene;
    scene.cameraPos = glm::vec3(0.0f);
    scene.transparentDraws.push_back(makeDrawAt(1.0f, 0.0f, 0.0f));  // dist² = 1
    scene.transparentDraws.push_back(makeDrawAt(3.0f, 0.0f, 0.0f));  // dist² = 9
    scene.transparentDraws.push_back(makeDrawAt(2.0f, 0.0f, 0.0f));  // dist² = 4

    sortTransparentDraws(scene);

    ASSERT_EQ(scene.transparentDraws.size(), 3u);
    // Farthest first.
    EXPECT_NEAR(posOf(scene.transparentDraws[0]).x, 3.0f, 1e-5f);
    EXPECT_NEAR(posOf(scene.transparentDraws[1]).x, 2.0f, 1e-5f);
    EXPECT_NEAR(posOf(scene.transparentDraws[2]).x, 1.0f, 1e-5f);
}

TEST(SortTransparentDraws, CameraOffOriginCorrectDistance)
{
    // Camera at (5, 0, 0).  Draw A at (3, 0, 0): dist² = 4.
    //                        Draw B at (0, 0, 0): dist² = 25.
    // B is farther → B should be first after sort.
    SceneView scene;
    scene.cameraPos = glm::vec3(5.0f, 0.0f, 0.0f);
    scene.transparentDraws.push_back(makeDrawAt(3.0f, 0.0f, 0.0f));  // closer
    scene.transparentDraws.push_back(makeDrawAt(0.0f, 0.0f, 0.0f));  // farther

    sortTransparentDraws(scene);

    ASSERT_EQ(scene.transparentDraws.size(), 2u);
    EXPECT_NEAR(posOf(scene.transparentDraws[0]).x, 0.0f, 1e-5f);  // farther first
    EXPECT_NEAR(posOf(scene.transparentDraws[1]).x, 3.0f, 1e-5f);  // closer second
}

TEST(SortTransparentDraws, AllEquidistantNoCorruption)
{
    // All draws equidistant from camera: sort must not crash or corrupt data.
    SceneView scene;
    scene.cameraPos = glm::vec3(0.0f);
    constexpr int N = 5;
    for (int i = 0; i < N; ++i)
    {
        // All at distance² = 4 (on a unit circle, scaled to radius 2).
        const float angle = static_cast<float>(i) * 0.5f;
        scene.transparentDraws.push_back(
            makeDrawAt(2.0f * std::cos(angle), 0.0f, 2.0f * std::sin(angle)));
    }

    EXPECT_NO_FATAL_FAILURE(sortTransparentDraws(scene));
    ASSERT_EQ(scene.transparentDraws.size(), static_cast<size_t>(N));

    // Verify all elements are still present (none lost, none duplicated).
    // Each must have distance² ≈ 4 from origin.
    for (const auto& d : scene.transparentDraws)
    {
        const glm::vec3 p   = posOf(d);
        const float     dsq = glm::dot(p, p);
        EXPECT_NEAR(dsq, 4.0f, 1e-4f);
    }
}

TEST(SortTransparentDraws, FiveDrawsAlreadySorted)
{
    // Draws already in back-to-front order: sort must not disturb them.
    SceneView scene;
    scene.cameraPos = glm::vec3(0.0f);
    for (int i = 5; i >= 1; --i)
    {
        scene.transparentDraws.push_back(
            makeDrawAt(static_cast<float>(i), 0.0f, 0.0f));
    }
    // After push: positions 5,4,3,2,1 — already back-to-front.

    sortTransparentDraws(scene);

    ASSERT_EQ(scene.transparentDraws.size(), 5u);
    for (int i = 0; i < 5; ++i)
    {
        const float expected = static_cast<float>(5 - i);
        EXPECT_NEAR(posOf(scene.transparentDraws[i]).x, expected, 1e-5f);
    }
}
