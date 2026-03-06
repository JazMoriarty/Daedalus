// test_sprite_animation_system.cpp
// Unit tests for render::spriteAnimationSystem (sprite_animation_system.h).
//
// spriteAnimationSystem is pure CPU logic — no GPU required — and must have
// unit test coverage per the spec §Testing rule:
//   "Pure logic (math, algorithms, data structures) must have unit tests."
//
// Test strategy:
//   Each test constructs a minimal ECS World with one entity carrying a
//   BillboardSpriteComponent and an AnimationStateComponent, runs the system
//   for one or more ticks, and verifies the resulting state on both components.

#include <gtest/gtest.h>

#include "daedalus/core/ecs/world.h"
#include "daedalus/core/components/transform_component.h"
#include "daedalus/render/components/billboard_sprite_component.h"
#include "daedalus/render/components/animation_state_component.h"
#include "daedalus/render/systems/sprite_animation_system.h"

#include <cmath>

using namespace daedalus;
using namespace daedalus::render;

namespace
{

/// Add a minimal animated billboard entity to the world and return its ID.
EntityId addAnimEntity(World& world,
                       u32  frameCount, u32 rowCount, u32 currentRow,
                       f32  fps,        bool loop)
{
    EntityId e = world.createEntity();
    world.addComponent(e, TransformComponent{});

    BillboardSpriteComponent sprite;
    world.addComponent(e, std::move(sprite));

    AnimationStateComponent anim;
    anim.frameCount  = frameCount;
    anim.rowCount    = rowCount;
    anim.currentRow  = currentRow;
    anim.currentFrame = 0;
    anim.fps         = fps;
    anim.accum       = 0.0f;
    anim.loop        = loop;
    world.addComponent(e, std::move(anim));

    return e;
}

} // namespace

// ─── Tests ────────────────────────────────────────────────────────────────────

// A tick smaller than one frame duration must NOT advance currentFrame.
TEST(SpriteAnimationSystem, ZeroDeltaTimeNoAdvance)
{
    World world;
    EntityId e = addAnimEntity(world, 4, 1, 0, 12.0f, true);

    spriteAnimationSystem(world, 0.0f);

    const auto& anim = world.getComponent<AnimationStateComponent>(e);
    EXPECT_EQ(anim.currentFrame, 0u);
    EXPECT_FLOAT_EQ(anim.accum, 0.0f);
}

// A tick that advances exactly one frame must increment currentFrame by one.
TEST(SpriteAnimationSystem, SingleFrameAdvance)
{
    World world;
    // 4 frames, 4 fps → frame duration = 0.25 s
    EntityId e = addAnimEntity(world, 4, 1, 0, 4.0f, true);

    spriteAnimationSystem(world, 0.25f);

    const auto& anim = world.getComponent<AnimationStateComponent>(e);
    EXPECT_EQ(anim.currentFrame, 1u);
    // Accumulator should have 0 s remaining after exactly one frame.
    EXPECT_NEAR(anim.accum, 0.0f, 1e-5f);
}

// After the last frame the sequence must wrap back to frame 0 when loop=true.
TEST(SpriteAnimationSystem, LoopWrapsToZero)
{
    World world;
    // 4 frames, 4 fps. Start at frame 3 (the last one).
    EntityId e = addAnimEntity(world, 4, 1, 0, 4.0f, true);
    world.getComponent<AnimationStateComponent>(e).currentFrame = 3;

    // Advance by one frame duration → should wrap to frame 0.
    spriteAnimationSystem(world, 0.25f);

    const auto& anim = world.getComponent<AnimationStateComponent>(e);
    EXPECT_EQ(anim.currentFrame, 0u);
}

// After the last frame the sequence must clamp at the last frame when loop=false.
TEST(SpriteAnimationSystem, NonLoopClampsAtLastFrame)
{
    World world;
    // 4 frames, 4 fps. Start at frame 3 (the last one).
    EntityId e = addAnimEntity(world, 4, 1, 0, 4.0f, false);
    world.getComponent<AnimationStateComponent>(e).currentFrame = 3;

    // Advance by one full frame duration → must stay at frame 3.
    spriteAnimationSystem(world, 0.25f);

    const auto& anim = world.getComponent<AnimationStateComponent>(e);
    EXPECT_EQ(anim.currentFrame, 3u);
}

// uvScale must equal (1/frameCount, 1/rowCount).
TEST(SpriteAnimationSystem, UVScaleCorrect)
{
    World world;
    EntityId e = addAnimEntity(world, 4, 2, 0, 12.0f, true);

    spriteAnimationSystem(world, 0.0f);

    const auto& sprite = world.getComponent<BillboardSpriteComponent>(e);
    EXPECT_NEAR(sprite.uvScale.x, 0.25f, 1e-5f);  // 1/4
    EXPECT_NEAR(sprite.uvScale.y, 0.5f,  1e-5f);  // 1/2
}

// uvOffset must equal (currentFrame/frameCount, currentRow/rowCount).
TEST(SpriteAnimationSystem, UVOffsetCorrect)
{
    World world;
    // 4 cols, 2 rows. Start at frame 2, row 1.
    EntityId e = addAnimEntity(world, 4, 2, 1, 12.0f, true);
    world.getComponent<AnimationStateComponent>(e).currentFrame = 2;

    spriteAnimationSystem(world, 0.0f);

    const auto& sprite = world.getComponent<BillboardSpriteComponent>(e);
    EXPECT_NEAR(sprite.uvOffset.x, 0.5f,  1e-5f);  // 2/4
    EXPECT_NEAR(sprite.uvOffset.y, 0.5f,  1e-5f);  // 1/2
}

// Changing currentRow must change uvOffset.y without affecting uvOffset.x.
TEST(SpriteAnimationSystem, RowSelectionChangesUVOffsetY)
{
    World world;
    // 4 cols, 4 rows. Frame 0, row 3.
    EntityId e = addAnimEntity(world, 4, 4, 3, 12.0f, true);

    spriteAnimationSystem(world, 0.0f);

    const auto& sprite = world.getComponent<BillboardSpriteComponent>(e);
    // frame 0 → uvOffset.x = 0/4 = 0
    EXPECT_NEAR(sprite.uvOffset.x, 0.0f,  1e-5f);
    // row 3, 4 rows → uvOffset.y = 3/4 = 0.75
    EXPECT_NEAR(sprite.uvOffset.y, 0.75f, 1e-5f);
}

// A very large deltaTime must advance multiple frames correctly and not crash.
TEST(SpriteAnimationSystem, LargeDeltaTimeAdvancesMultipleFrames)
{
    World world;
    // 4 frames, 4 fps. 2 seconds → should advance 8 frames → 8 mod 4 = 0 (loop).
    EntityId e = addAnimEntity(world, 4, 1, 0, 4.0f, true);

    spriteAnimationSystem(world, 2.0f);

    const auto& anim = world.getComponent<AnimationStateComponent>(e);
    EXPECT_EQ(anim.currentFrame, 0u);  // 8 mod 4 = 0
}

// Entities without an AnimationStateComponent must not be touched by the system.
TEST(SpriteAnimationSystem, EntityWithoutAnimStateLeavesUVDefaults)
{
    World world;
    EntityId e = world.createEntity();
    world.addComponent(e, TransformComponent{});

    BillboardSpriteComponent sprite;
    // Defaults: uvOffset=(0,0), uvScale=(1,1)
    world.addComponent(e, std::move(sprite));

    spriteAnimationSystem(world, 1.0f);  // no AnimationStateComponent on this entity

    const auto& s = world.getComponent<BillboardSpriteComponent>(e);
    EXPECT_NEAR(s.uvOffset.x, 0.0f, 1e-5f);
    EXPECT_NEAR(s.uvOffset.y, 0.0f, 1e-5f);
    EXPECT_NEAR(s.uvScale.x,  1.0f, 1e-5f);
    EXPECT_NEAR(s.uvScale.y,  1.0f, 1e-5f);
}
