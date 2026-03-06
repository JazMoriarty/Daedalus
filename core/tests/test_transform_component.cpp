// test_transform_component.cpp
// Unit tests for TransformComponent::toMatrix().

#include "daedalus/core/components/transform_component.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

using namespace daedalus;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static constexpr float k_eps = 1e-5f;

static void expectMat4Near(const glm::mat4& a, const glm::mat4& b, float eps = k_eps)
{
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            EXPECT_NEAR(a[col][row], b[col][row], eps)
                << "at [col=" << col << "][row=" << row << "]";
}

// ─── Identity ─────────────────────────────────────────────────────────────────

TEST(TransformComponent, DefaultConstructedIsIdentity)
{
    const TransformComponent t;
    expectMat4Near(t.toMatrix(), glm::mat4(1.0f));
}

// ─── Translation ──────────────────────────────────────────────────────────────

TEST(TransformComponent, TranslationOnlyMovesOrigin)
{
    TransformComponent t;
    t.position = glm::vec3(3.0f, -7.0f, 2.5f);

    const glm::mat4 m = t.toMatrix();

    // Translation must appear in column 3.
    EXPECT_NEAR(m[3][0],  3.0f, k_eps);
    EXPECT_NEAR(m[3][1], -7.0f, k_eps);
    EXPECT_NEAR(m[3][2],  2.5f, k_eps);
    EXPECT_NEAR(m[3][3],  1.0f, k_eps);

    // Upper-left 3×3 must remain identity (no rotation/scale).
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 3; ++row)
            EXPECT_NEAR(m[col][row], (col == row) ? 1.0f : 0.0f, k_eps)
                << "at [col=" << col << "][row=" << row << "]";
}

// ─── Scale ────────────────────────────────────────────────────────────────────

TEST(TransformComponent, UniformScaleAppliesToDiagonal)
{
    TransformComponent t;
    t.scale = glm::vec3(4.0f);

    const glm::mat4 m = t.toMatrix();

    EXPECT_NEAR(m[0][0], 4.0f, k_eps);
    EXPECT_NEAR(m[1][1], 4.0f, k_eps);
    EXPECT_NEAR(m[2][2], 4.0f, k_eps);
    EXPECT_NEAR(m[3][3], 1.0f, k_eps);
}

TEST(TransformComponent, NonUniformScale)
{
    TransformComponent t;
    t.scale = glm::vec3(2.0f, 3.0f, 0.5f);

    const glm::mat4 m = t.toMatrix();

    EXPECT_NEAR(m[0][0], 2.0f, k_eps);
    EXPECT_NEAR(m[1][1], 3.0f, k_eps);
    EXPECT_NEAR(m[2][2], 0.5f, k_eps);
}

// ─── Rotation ─────────────────────────────────────────────────────────────────

TEST(TransformComponent, Rotation90DegYTransformsXToNegZ)
{
    TransformComponent t;
    // 90° around Y: +X → -Z, +Z → +X  (left-hand Y-up convention)
    t.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::mat4 m  = t.toMatrix();
    const glm::vec4 px = m * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f); // direction vector

    EXPECT_NEAR(px.x,  0.0f, k_eps);
    EXPECT_NEAR(px.y,  0.0f, k_eps);
    EXPECT_NEAR(px.z, -1.0f, k_eps);
}

TEST(TransformComponent, Rotation90DegYTransformsZToPosX)
{
    TransformComponent t;
    t.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::mat4 m  = t.toMatrix();
    const glm::vec4 pz = m * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);

    EXPECT_NEAR(pz.x, 1.0f, k_eps);
    EXPECT_NEAR(pz.y, 0.0f, k_eps);
    EXPECT_NEAR(pz.z, 0.0f, k_eps);
}

TEST(TransformComponent, RotationPreservesLength)
{
    TransformComponent t;
    t.rotation = glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));

    const glm::mat4  m  = t.toMatrix();
    const glm::vec3  v  = { 3.0f, -1.0f, 2.0f };
    const glm::vec4  vr = m * glm::vec4(v, 0.0f);

    EXPECT_NEAR(glm::length(glm::vec3(vr)), glm::length(v), k_eps);
}

// ─── Combined TRS ─────────────────────────────────────────────────────────────

TEST(TransformComponent, TranslateAndScaleCombine)
{
    // Scale by 2, then translate by (1, 0, 0).
    // A local point (1, 0, 0) should land at (2 + 1, 0, 0) = (3, 0, 0).
    TransformComponent t;
    t.position = glm::vec3(1.0f, 0.0f, 0.0f);
    t.scale    = glm::vec3(2.0f);

    const glm::mat4 m  = t.toMatrix();
    const glm::vec4 pt = m * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

    EXPECT_NEAR(pt.x, 3.0f, k_eps);
    EXPECT_NEAR(pt.y, 0.0f, k_eps);
    EXPECT_NEAR(pt.z, 0.0f, k_eps);
}

TEST(TransformComponent, TRSOrderIsTranslateRotateScale)
{
    // Verify the TRS decomposition: translate * rotate * scale.
    TransformComponent t;
    t.position = glm::vec3(5.0f, 0.0f, 0.0f);
    t.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    t.scale    = glm::vec3(2.0f);

    // Expected: T * R * S applied to (1, 0, 0, 1)
    //   scale:      (2, 0, 0, 1)
    //   rotate 90Y: (0, 0, -2, 1)
    //   translate:  (5, 0, -2, 1)
    const glm::mat4 m  = t.toMatrix();
    const glm::vec4 pt = m * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

    EXPECT_NEAR(pt.x, 5.0f, k_eps);
    EXPECT_NEAR(pt.y, 0.0f, k_eps);
    EXPECT_NEAR(pt.z, -2.0f, 1e-4f);  // slightly looser: float sin/cos accumulation
}
