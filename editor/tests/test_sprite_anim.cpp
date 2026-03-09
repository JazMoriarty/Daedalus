// test_sprite_anim.cpp
// Unit tests for the spriteFrameIndex() pure helper.

#include "panels/sprite_anim_math.h"

#include <gtest/gtest.h>

using namespace daedalus::editor;

// ─── Degenerate / guard cases ─────────────────────────────────────────────────

TEST(SpriteAnimMath, ZeroFpsReturnsFrame0)
{
    EXPECT_EQ(spriteFrameIndex(10.0f, 0.0f, 8u), 0u);
}

TEST(SpriteAnimMath, NegativeFpsReturnsFrame0)
{
    EXPECT_EQ(spriteFrameIndex(10.0f, -24.0f, 8u), 0u);
}

TEST(SpriteAnimMath, ZeroFrameCountReturnsFrame0)
{
    EXPECT_EQ(spriteFrameIndex(10.0f, 24.0f, 0u), 0u);
}

TEST(SpriteAnimMath, ZeroElapsedReturnsFrame0)
{
    EXPECT_EQ(spriteFrameIndex(0.0f, 24.0f, 8u), 0u);
}

// ─── Exact frame boundaries ───────────────────────────────────────────────────

TEST(SpriteAnimMath, AtExactFrameBoundary)
{
    // At exactly 1/24 s we should be on frame 1, not frame 0.
    EXPECT_EQ(spriteFrameIndex(1.0f / 24.0f, 24.0f, 8u), 1u);
}

TEST(SpriteAnimMath, JustBeforeNextFrame)
{
    // Slightly before the 2nd frame boundary — still frame 1.
    const float t = 1.0f / 24.0f + 0.5f / 24.0f;
    EXPECT_EQ(spriteFrameIndex(t, 24.0f, 8u), 1u);
}

// ─── Wrap-around ──────────────────────────────────────────────────────────────

TEST(SpriteAnimMath, WrapsAfterLastFrame)
{
    // 8 frames at 24 fps — after 8/24 s the counter resets to 0.
    EXPECT_EQ(spriteFrameIndex(8.0f / 24.0f, 24.0f, 8u), 0u);
}

TEST(SpriteAnimMath, SecondCycleMatchesFirst)
{
    // Frame 3 in cycle 0 should equal frame 3 in cycle 1.
    const float t0 =  3.0f / 24.0f;
    const float t1 = 11.0f / 24.0f;  // 3 + 8 frames later
    EXPECT_EQ(spriteFrameIndex(t0, 24.0f, 8u),
              spriteFrameIndex(t1, 24.0f, 8u));
}

TEST(SpriteAnimMath, LargeElapsedWrapsCorrectly)
{
    // Many full cycles accumulated — just needs correct modulo.
    // 1000 frames / 24 fps = frame (1000 % 8) = 0
    const float t = 1000.0f / 24.0f;
    EXPECT_EQ(spriteFrameIndex(t, 24.0f, 8u), 0u);
}

// ─── Single-frame sheet ───────────────────────────────────────────────────────

TEST(SpriteAnimMath, SingleFrameAlwaysReturns0)
{
    EXPECT_EQ(spriteFrameIndex(  0.0f, 24.0f, 1u), 0u);
    EXPECT_EQ(spriteFrameIndex( 99.9f, 24.0f, 1u), 0u);
}

// ─── Non-power-of-two frame count ─────────────────────────────────────────────

TEST(SpriteAnimMath, SevenFrameSheet)
{
    // At fps=7, each second advances 7 frames == one full cycle → frame 0.
    EXPECT_EQ(spriteFrameIndex(1.0f, 7.0f, 7u), 0u);
    // Half a second in → frame 3.
    EXPECT_EQ(spriteFrameIndex(0.5f, 7.0f, 7u), 3u);
}
