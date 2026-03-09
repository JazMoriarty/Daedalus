// test_rotated_sprite.cpp
// Unit tests for the rotatedSpriteFrameIndex pure function.
// No GPU or document context required.

#include "daedalus/editor/entity_def.h"

#include <gtest/gtest.h>
#include <cmath>

using namespace daedalus::editor;

// ─── 8-direction tests ────────────────────────────────────────────────────────

TEST(RotatedSpriteFrameIndex, ZeroAngle8Dir_ReturnsFront)
{
    // Angle 0 → column 0 (entity faces viewer head-on)
    EXPECT_EQ(rotatedSpriteFrameIndex(0.0f, 8u), 0u);
}

TEST(RotatedSpriteFrameIndex, QuarterTurn8Dir)
{
    // π/2 (90°) → column 2
    EXPECT_EQ(rotatedSpriteFrameIndex(static_cast<float>(M_PI) / 2.0f, 8u), 2u);
}

TEST(RotatedSpriteFrameIndex, HalfTurn8Dir_ReturnsBack)
{
    // π (180°) → column 4 (back of entity)
    EXPECT_EQ(rotatedSpriteFrameIndex(static_cast<float>(M_PI), 8u), 4u);
}

TEST(RotatedSpriteFrameIndex, AlmostFullTurn8Dir_ReturnsLastColumn)
{
    // Column 7 is centred at 7π/4 (315°); test a value clearly within its range.
    EXPECT_EQ(rotatedSpriteFrameIndex(7.0f * static_cast<float>(M_PI) / 4.0f, 8u), 7u);
}

TEST(RotatedSpriteFrameIndex, NegativeAngle8Dir_WrapsCorrectly)
{
    // -π/2 is equivalent to 3π/2 (270°) → column 6
    EXPECT_EQ(rotatedSpriteFrameIndex(-static_cast<float>(M_PI) / 2.0f, 8u), 6u);
}

TEST(RotatedSpriteFrameIndex, AngleBeyondTwoPi8Dir_WrapsCorrectly)
{
    // 2π + 0 (== 0) → column 0
    constexpr float k_2pi = 6.2831853f;
    EXPECT_EQ(rotatedSpriteFrameIndex(k_2pi + 0.001f, 8u), 0u);
}

// ─── 16-direction tests ───────────────────────────────────────────────────────

TEST(RotatedSpriteFrameIndex, ZeroAngle16Dir_ReturnsFront)
{
    EXPECT_EQ(rotatedSpriteFrameIndex(0.0f, 16u), 0u);
}

TEST(RotatedSpriteFrameIndex, HalfTurn16Dir_ReturnsBack)
{
    // π → column 8
    EXPECT_EQ(rotatedSpriteFrameIndex(static_cast<float>(M_PI), 16u), 8u);
}

TEST(RotatedSpriteFrameIndex, QuarterTurn16Dir)
{
    // π/2 → column 4
    EXPECT_EQ(rotatedSpriteFrameIndex(static_cast<float>(M_PI) / 2.0f, 16u), 4u);
}

// ─── Edge / guard cases ───────────────────────────────────────────────────────

TEST(RotatedSpriteFrameIndex, ZeroDirectionCount_ReturnsZero)
{
    EXPECT_EQ(rotatedSpriteFrameIndex(1.57f, 0u), 0u);
}

TEST(RotatedSpriteFrameIndex, OneDirectionCount_AlwaysReturnsZero)
{
    EXPECT_EQ(rotatedSpriteFrameIndex(0.0f,                         1u), 0u);
    EXPECT_EQ(rotatedSpriteFrameIndex(static_cast<float>(M_PI),     1u), 0u);
    EXPECT_EQ(rotatedSpriteFrameIndex(2.0f * static_cast<float>(M_PI), 1u), 0u);
}

TEST(RotatedSpriteFrameIndex, ResultAlwaysInRange8Dir)
{
    for (int i = 0; i < 360; ++i)
    {
        const float angle = static_cast<float>(i) * (6.2831853f / 360.0f);
        const uint32_t col = rotatedSpriteFrameIndex(angle, 8u);
        EXPECT_LT(col, 8u) << "angle=" << angle;
    }
}

TEST(RotatedSpriteFrameIndex, ResultAlwaysInRange16Dir)
{
    for (int i = 0; i < 360; ++i)
    {
        const float angle = static_cast<float>(i) * (6.2831853f / 360.0f);
        const uint32_t col = rotatedSpriteFrameIndex(angle, 16u);
        EXPECT_LT(col, 16u) << "angle=" << angle;
    }
}
