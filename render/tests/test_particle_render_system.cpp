// test_particle_render_system.cpp
// Unit tests for render::particleRenderSystem (particle_render_system.h).
//
// The system is pure CPU logic: it queries the ECS world and packs
// ParticleEmitterDraw entries into SceneView::particleEmitters.
// No GPU objects are created; fake non-null sentinel pointers stand in for
// ITexture* handles, and ParticlePool instances are constructed directly
// (without createParticlePool) so their IBuffer fields remain null.

#include "daedalus/render/systems/particle_render_system.h"

#include "daedalus/core/ecs/world.h"
#include "daedalus/core/components/transform_component.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

using namespace daedalus;
using namespace daedalus::render;

static constexpr float k_eps = 1e-5f;

// ─── Sentinel texture pointer ────────────────────────────────────────────────
// A non-null fake pointer that satisfies the null-guard inside the system
// without requiring a real GPU texture allocation.

static rhi::ITexture* const kFakeAtlas =
    reinterpret_cast<rhi::ITexture*>(static_cast<uintptr_t>(0xDEAD));

// ─── Helper: make a minimal ParticlePool with only maxParticles set ──────────

static std::unique_ptr<ParticlePool> makeFakePool(u32 maxParticles = 256u)
{
    auto pool          = std::make_unique<ParticlePool>();
    pool->maxParticles = maxParticles;
    // All IBuffer unique_ptrs stay null — the system only reads plain POD fields.
    return pool;
}

// ─── EmptyWorld ──────────────────────────────────────────────────────────────

TEST(ParticleRenderSystem, EmptyWorldProducesNoDraws)
{
    World     world;
    SceneView scene;

    particleRenderSystem(world, scene, /*dt=*/0.016f, /*frameIndex=*/0u);

    EXPECT_TRUE(scene.particleEmitters.empty());
}

// ─── Null pool guard ─────────────────────────────────────────────────────────

TEST(ParticleRenderSystem, NullPoolIsSkipped)
{
    World world;

    TransformComponent t;
    ParticleEmitterComponent e;
    e.pool         = nullptr;        // null pool → skip
    e.atlasTexture = kFakeAtlas;

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    SceneView scene;
    particleRenderSystem(world, scene, 0.016f, 0u);

    EXPECT_TRUE(scene.particleEmitters.empty());
}

// ─── Null atlas guard ────────────────────────────────────────────────────────

TEST(ParticleRenderSystem, NullAtlasIsSkipped)
{
    World world;

    auto pool = makeFakePool();

    TransformComponent t;
    ParticleEmitterComponent e;
    e.pool         = pool.get();
    e.atlasTexture = nullptr;        // null atlas → skip

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    SceneView scene;
    particleRenderSystem(world, scene, 0.016f, 0u);

    EXPECT_TRUE(scene.particleEmitters.empty());
}

// ─── Single valid emitter ────────────────────────────────────────────────────

TEST(ParticleRenderSystem, SingleValidEmitterProducesOneDraw)
{
    World world;

    auto pool = makeFakePool(512u);

    TransformComponent t;
    t.position = glm::vec3(1.0f, 2.0f, 3.0f);

    ParticleEmitterComponent e;
    e.pool         = pool.get();
    e.atlasTexture = kFakeAtlas;
    e.emissionRate = 100.0f;

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    SceneView scene;
    particleRenderSystem(world, scene, /*dt=*/0.016f, /*frameIndex=*/7u);

    ASSERT_EQ(scene.particleEmitters.size(), 1u);
}

// ─── Pool and texture pointers forwarded ─────────────────────────────────────

TEST(ParticleRenderSystem, PoolAndAtlasPointersForwarded)
{
    World world;

    auto pool = makeFakePool(128u);

    TransformComponent t;
    ParticleEmitterComponent e;
    e.pool         = pool.get();
    e.atlasTexture = kFakeAtlas;

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    SceneView scene;
    particleRenderSystem(world, scene, 0.016f, 0u);

    ASSERT_EQ(scene.particleEmitters.size(), 1u);
    EXPECT_EQ(scene.particleEmitters[0].pool,         pool.get());
    EXPECT_EQ(scene.particleEmitters[0].atlasTexture, kFakeAtlas);
}

// ─── Constants: emitter position from TransformComponent ─────────────────────

TEST(ParticleRenderSystem, EmitterPosMatchesTransformPosition)
{
    World world;

    auto pool = makeFakePool();

    TransformComponent t;
    t.position = glm::vec3(-3.5f, 0.1f, 7.2f);

    ParticleEmitterComponent e;
    e.pool         = pool.get();
    e.atlasTexture = kFakeAtlas;

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    SceneView scene;
    particleRenderSystem(world, scene, 0.016f, 0u);

    ASSERT_EQ(scene.particleEmitters.size(), 1u);
    const auto& c = scene.particleEmitters[0].constants;
    EXPECT_NEAR(c.emitterPos.x, -3.5f, k_eps);
    EXPECT_NEAR(c.emitterPos.y,  0.1f, k_eps);
    EXPECT_NEAR(c.emitterPos.z,  7.2f, k_eps);
}

// ─── Accumulator: fractional credit carries across frames ────────────────────
// Verifies that at 120 Hz a 60 Hz emitter still emits the correct rate by
// accumulating 0.5 credits per frame and spawning on every other frame.

TEST(ParticleRenderSystem, AccumulatorCarriesFractionBetweenFrames)
{
    World world;

    auto pool = makeFakePool(256u);

    TransformComponent t;
    ParticleEmitterComponent e;
    e.pool         = pool.get();
    e.atlasTexture = kFakeAtlas;
    e.emissionRate = 60.0f;          // 60 particles/s
    // emissionAccumulator starts at 0.0 (default)

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    constexpr float dt = 1.0f / 120.0f;  // 120 Hz: 60 * dt = 0.5 < 1.0

    // Frame 1: accumulates 0.5, nothing to spawn yet.
    {
        SceneView scene;
        particleRenderSystem(world, scene, dt, 0u);
        ASSERT_EQ(scene.particleEmitters.size(), 1u);
        EXPECT_EQ(scene.particleEmitters[0].constants.spawnThisFrame, 0u)
            << "Frame 1: credit=0.5, no spawn expected";
    }

    // Frame 2: accumulates another 0.5 (total 1.0) → spawn 1, remainder 0.
    {
        SceneView scene;
        particleRenderSystem(world, scene, dt, 1u);
        ASSERT_EQ(scene.particleEmitters.size(), 1u);
        EXPECT_EQ(scene.particleEmitters[0].constants.spawnThisFrame, 1u)
            << "Frame 2: credit=1.0, one spawn expected";
    }

    // Frame 3: back to 0.5 credit, no spawn.
    {
        SceneView scene;
        particleRenderSystem(world, scene, dt, 2u);
        ASSERT_EQ(scene.particleEmitters.size(), 1u);
        EXPECT_EQ(scene.particleEmitters[0].constants.spawnThisFrame, 0u)
            << "Frame 3: credit=0.5, no spawn expected";
    }

    // Frame 4: back to 1.0 credit → spawn 1 again.
    {
        SceneView scene;
        particleRenderSystem(world, scene, dt, 3u);
        ASSERT_EQ(scene.particleEmitters.size(), 1u);
        EXPECT_EQ(scene.particleEmitters[0].constants.spawnThisFrame, 1u)
            << "Frame 4: credit=1.0, one spawn expected";
    }
}

// ─── Constants: emissionRate and spawnThisFrame ───────────────────────────────

TEST(ParticleRenderSystem, SpawnThisFrameIsFloorOfEmissionRateTimesDt)
{
    World world;

    auto pool = makeFakePool(1024u);

    TransformComponent t;
    ParticleEmitterComponent e;
    e.pool         = pool.get();
    e.atlasTexture = kFakeAtlas;
    e.emissionRate = 300.0f;       // 300 particles/s

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    constexpr float dt          = 1.0f / 30.0f;   // 33.33 ms
    const u32       expected    = static_cast<u32>(std::floor(300.0f * dt));  // 10

    SceneView scene;
    particleRenderSystem(world, scene, dt, 0u);

    ASSERT_EQ(scene.particleEmitters.size(), 1u);
    EXPECT_EQ(scene.particleEmitters[0].constants.spawnThisFrame, expected);
}

TEST(ParticleRenderSystem, SpawnClampsToPoolCapacity)
{
    // Emission rate so high that spawnThisFrame would exceed maxParticles.
    World world;

    const u32 maxP = 16u;
    auto pool = makeFakePool(maxP);

    TransformComponent t;
    ParticleEmitterComponent e;
    e.pool         = pool.get();
    e.atlasTexture = kFakeAtlas;
    e.emissionRate = 100000.0f;   // vastly exceeds pool capacity

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    SceneView scene;
    particleRenderSystem(world, scene, /*dt=*/1.0f, 0u);  // dt=1 → 100000 raw

    ASSERT_EQ(scene.particleEmitters.size(), 1u);
    EXPECT_LE(scene.particleEmitters[0].constants.spawnThisFrame, maxP);
}

// ─── Constants: frameIndex forwarded ─────────────────────────────────────────

TEST(ParticleRenderSystem, FrameIndexForwardedToConstants)
{
    World world;

    auto pool = makeFakePool();

    TransformComponent t;
    ParticleEmitterComponent e;
    e.pool         = pool.get();
    e.atlasTexture = kFakeAtlas;

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    SceneView scene;
    particleRenderSystem(world, scene, 0.016f, /*frameIndex=*/42u);

    ASSERT_EQ(scene.particleEmitters.size(), 1u);
    EXPECT_EQ(scene.particleEmitters[0].constants.frameIndex, 42u);
}

// ─── Constants: aliveListFlip from pool ──────────────────────────────────────

TEST(ParticleRenderSystem, AliveListFlipCopiedFromPool)
{
    World world;

    auto pool           = makeFakePool();
    pool->aliveListFlip = 1u;   // simulate "B is current read list"

    TransformComponent t;
    ParticleEmitterComponent e;
    e.pool         = pool.get();
    e.atlasTexture = kFakeAtlas;

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    SceneView scene;
    particleRenderSystem(world, scene, 0.016f, 0u);

    ASSERT_EQ(scene.particleEmitters.size(), 1u);
    EXPECT_EQ(scene.particleEmitters[0].constants.aliveListFlip, 1u);
}

// ─── Multiple emitters ───────────────────────────────────────────────────────

TEST(ParticleRenderSystem, MultipleEmittersProduceMatchingCount)
{
    World world;

    constexpr int kCount = 5;
    std::vector<std::unique_ptr<ParticlePool>> pools;

    for (int i = 0; i < kCount; ++i)
    {
        pools.push_back(makeFakePool(64u));

        TransformComponent t;
        t.position = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);

        ParticleEmitterComponent e;
        e.pool         = pools.back().get();
        e.atlasTexture = kFakeAtlas;

        EntityId eid = world.createEntity();
        world.addComponent(eid, t);
        world.addComponent(eid, std::move(e));
    }

    SceneView scene;
    particleRenderSystem(world, scene, 0.016f, 0u);

    EXPECT_EQ(scene.particleEmitters.size(), static_cast<size_t>(kCount));
}

// ─── Mix of null-pool and valid emitters ─────────────────────────────────────

TEST(ParticleRenderSystem, OnlyValidEmittersContributeToDraw)
{
    World world;

    auto goodPool = makeFakePool(128u);

    // Entity A: null pool → skip
    {
        TransformComponent t;
        ParticleEmitterComponent e;
        e.pool         = nullptr;
        e.atlasTexture = kFakeAtlas;
        EntityId eid = world.createEntity();
        world.addComponent(eid, t);
        world.addComponent(eid, std::move(e));
    }
    // Entity B: valid → include
    {
        TransformComponent t;
        ParticleEmitterComponent e;
        e.pool         = goodPool.get();
        e.atlasTexture = kFakeAtlas;
        EntityId eid = world.createEntity();
        world.addComponent(eid, t);
        world.addComponent(eid, std::move(e));
    }
    // Entity C: null atlas → skip
    {
        TransformComponent t;
        ParticleEmitterComponent e;
        e.pool         = goodPool.get();
        e.atlasTexture = nullptr;
        EntityId eid = world.createEntity();
        world.addComponent(eid, t);
        world.addComponent(eid, std::move(e));
    }

    SceneView scene;
    particleRenderSystem(world, scene, 0.016f, 0u);

    // Only entity B is valid.
    ASSERT_EQ(scene.particleEmitters.size(), 1u);
    EXPECT_EQ(scene.particleEmitters[0].pool, goodPool.get());
}

// ─── Emitter component scalars forwarded to constants ────────────────────────

TEST(ParticleRenderSystem, EmitterComponentScalarsForwarded)
{
    World world;

    auto pool = makeFakePool(256u);

    TransformComponent t;

    ParticleEmitterComponent e;
    e.pool             = pool.get();
    e.atlasTexture     = kFakeAtlas;
    e.emissionRate     = 75.0f;
    e.speedMin         = 1.2f;
    e.speedMax         = 3.4f;
    e.lifetimeMin      = 0.5f;
    e.lifetimeMax      = 2.0f;
    e.emissiveScale    = 5.5f;
    e.drag             = 0.8f;
    e.turbulenceScale  = 1.1f;
    e.velocityStretch  = 0.05f;
    e.softRange        = 0.3f;
    e.colorStart       = glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);
    e.colorEnd         = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    e.sizeStart        = 0.1f;
    e.sizeEnd          = 0.05f;

    EntityId eid = world.createEntity();
    world.addComponent(eid, t);
    world.addComponent(eid, std::move(e));

    SceneView scene;
    particleRenderSystem(world, scene, 0.016f, 0u);

    ASSERT_EQ(scene.particleEmitters.size(), 1u);
    const auto& c = scene.particleEmitters[0].constants;

    EXPECT_NEAR(c.emissionRate,    75.0f, k_eps);
    EXPECT_NEAR(c.speedMin,         1.2f, k_eps);
    EXPECT_NEAR(c.speedMax,         3.4f, k_eps);
    EXPECT_NEAR(c.lifetimeMin,      0.5f, k_eps);
    EXPECT_NEAR(c.lifetimeMax,      2.0f, k_eps);
    EXPECT_NEAR(c.emissiveScale,    5.5f, k_eps);
    EXPECT_NEAR(c.drag,             0.8f, k_eps);
    EXPECT_NEAR(c.turbulenceScale,  1.1f, k_eps);
    EXPECT_NEAR(c.velocityStretch,  0.05f, k_eps);
    EXPECT_NEAR(c.softRange,        0.3f, k_eps);
    EXPECT_NEAR(c.sizeStart,        0.1f, k_eps);
    EXPECT_NEAR(c.sizeEnd,          0.05f, k_eps);
    EXPECT_NEAR(c.colorStart.r,     1.0f, k_eps);
    EXPECT_NEAR(c.colorEnd.a,       0.0f, k_eps);
    EXPECT_EQ  (c.maxParticles,     256u);
}
