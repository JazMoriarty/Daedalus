// test_decal_render_system.cpp
// Unit tests for render::decalRenderSystem (decal_render_system.h).
//
// The system is pure CPU logic — it queries the ECS world, computes a model
// matrix, inverts it, and appends DecalDraw entries to SceneView::decalDraws.
// No GPU objects are created; null / sentinel ITexture* pointers are used as
// stand-ins for texture handles.

#include "daedalus/render/systems/decal_render_system.h"

#include "daedalus/core/ecs/world.h"
#include "daedalus/core/components/transform_component.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace daedalus;
using namespace daedalus::render;

static constexpr float k_eps = 1e-4f;

// Non-null sentinel ITexture* values used in place of real GPU textures.
// Unit tests never dereference these — they only need them to be non-null so
// the system does NOT apply the "skip if albedoTexture == nullptr" guard.
static rhi::ITexture* const kFakeAlbedo =
    reinterpret_cast<rhi::ITexture*>(static_cast<uintptr_t>(0xAAAA));
static rhi::ITexture* const kFakeNormal =
    reinterpret_cast<rhi::ITexture*>(static_cast<uintptr_t>(0xBBBB));

// ─── EmptyWorld ──────────────────────────────────────────────────────────────

TEST(DecalRenderSystem, EmptyWorldProducesNoDraws)
{
    World world;
    SceneView scene;

    decalRenderSystem(world, scene);

    EXPECT_TRUE(scene.decalDraws.empty());
}

// ─── Single decal entity ─────────────────────────────────────────────────────

TEST(DecalRenderSystem, SingleDecalProducesOneEntry)
{
    World world;

    TransformComponent t;
    t.position = glm::vec3(1.0f, 0.0f, 0.0f);
    t.scale    = glm::vec3(2.0f);

    DecalComponent d;
    d.albedoTexture = kFakeAlbedo;

    EntityId e = world.createEntity();
    world.addComponent(e, t);
    world.addComponent(e, d);

    SceneView scene;
    decalRenderSystem(world, scene);

    ASSERT_EQ(scene.decalDraws.size(), 1u);
}

// ─── Multiple entities ───────────────────────────────────────────────────────

TEST(DecalRenderSystem, ThreeDecalsProduceThreeEntries)
{
    World world;

    for (int i = 0; i < 3; ++i)
    {
        TransformComponent t;
        t.position = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);

        DecalComponent d;
        d.albedoTexture = kFakeAlbedo;

        EntityId e = world.createEntity();
        world.addComponent(e, t);
        world.addComponent(e, d);
    }

    SceneView scene;
    decalRenderSystem(world, scene);

    EXPECT_EQ(scene.decalDraws.size(), 3u);
}

// ─── Null albedo guard ───────────────────────────────────────────────────────

TEST(DecalRenderSystem, NullAlbedoEntityIsSkipped)
{
    World world;

    // Entity A: albedoTexture = null → SKIP.
    {
        TransformComponent t;
        DecalComponent     d;  // albedoTexture defaults to nullptr
        EntityId e = world.createEntity();
        world.addComponent(e, t);
        world.addComponent(e, d);
    }
    // Entity B: albedoTexture = kFakeAlbedo → INCLUDE.
    {
        TransformComponent t;
        DecalComponent     d;
        d.albedoTexture = kFakeAlbedo;
        EntityId e = world.createEntity();
        world.addComponent(e, t);
        world.addComponent(e, d);
    }

    SceneView scene;
    decalRenderSystem(world, scene);

    // Only entity B should contribute a draw.
    ASSERT_EQ(scene.decalDraws.size(), 1u);
    EXPECT_EQ(scene.decalDraws[0].albedoTexture, kFakeAlbedo);
}

TEST(DecalRenderSystem, AllNullAlbedoProducesNoDraws)
{
    World world;

    for (int i = 0; i < 4; ++i)
    {
        TransformComponent t;
        DecalComponent     d;   // albedoTexture = nullptr (default)
        EntityId e = world.createEntity();
        world.addComponent(e, t);
        world.addComponent(e, d);
    }

    SceneView scene;
    decalRenderSystem(world, scene);

    EXPECT_TRUE(scene.decalDraws.empty());
}

// ─── Model matrix ────────────────────────────────────────────────────────────

TEST(DecalRenderSystem, ModelMatrixMatchesTransformToMatrix)
{
    World world;

    TransformComponent t;
    t.position = glm::vec3(3.0f, 1.5f, -2.0f);
    t.scale    = glm::vec3(2.0f, 0.1f, 2.0f);
    // Default identity quaternion rotation.

    DecalComponent d;
    d.albedoTexture = kFakeAlbedo;

    EntityId e = world.createEntity();
    world.addComponent(e, t);
    world.addComponent(e, d);

    SceneView scene;
    decalRenderSystem(world, scene);

    ASSERT_EQ(scene.decalDraws.size(), 1u);

    const glm::mat4 expected = t.toMatrix();
    const glm::mat4 actual   = scene.decalDraws[0].modelMatrix;

    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            EXPECT_NEAR(actual[col][row], expected[col][row], k_eps)
                << "mismatch at column " << col << " row " << row;
}

// ─── Inverse model matrix ────────────────────────────────────────────────────

TEST(DecalRenderSystem, InvModelTimesModelIsIdentity)
{
    World world;

    TransformComponent t;
    t.position = glm::vec3(-1.0f, 0.5f, 4.0f);
    t.scale    = glm::vec3(1.5f, 0.2f, 1.5f);

    DecalComponent d;
    d.albedoTexture = kFakeAlbedo;

    EntityId e = world.createEntity();
    world.addComponent(e, t);
    world.addComponent(e, d);

    SceneView scene;
    decalRenderSystem(world, scene);

    ASSERT_EQ(scene.decalDraws.size(), 1u);

    const glm::mat4 product = scene.decalDraws[0].invModelMatrix
                            * scene.decalDraws[0].modelMatrix;
    const glm::mat4 ident   = glm::mat4(1.0f);

    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            EXPECT_NEAR(product[col][row], ident[col][row], k_eps)
                << "invModel * model ≠ identity at column " << col << " row " << row;
}

TEST(DecalRenderSystem, ModelTimesInvModelIsIdentity)
{
    // Verify the other multiplication order as well (M * M⁻¹ = I).
    World world;

    TransformComponent t;
    t.position = glm::vec3(2.0f, 0.0f, -3.0f);
    t.scale    = glm::vec3(3.0f, 0.15f, 3.0f);

    DecalComponent d;
    d.albedoTexture = kFakeAlbedo;

    EntityId e = world.createEntity();
    world.addComponent(e, t);
    world.addComponent(e, d);

    SceneView scene;
    decalRenderSystem(world, scene);

    ASSERT_EQ(scene.decalDraws.size(), 1u);

    const glm::mat4 product = scene.decalDraws[0].modelMatrix
                            * scene.decalDraws[0].invModelMatrix;
    const glm::mat4 ident   = glm::mat4(1.0f);

    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            EXPECT_NEAR(product[col][row], ident[col][row], k_eps)
                << "model * invModel ≠ identity at column " << col << " row " << row;
}

// ─── Scalar + texture fields ─────────────────────────────────────────────────

TEST(DecalRenderSystem, ScalarsAndTexturePointersTransferred)
{
    World world;

    TransformComponent t;

    DecalComponent d;
    d.albedoTexture = kFakeAlbedo;
    d.normalTexture = kFakeNormal;
    d.roughness     = 0.75f;
    d.metalness     = 0.25f;
    d.opacity       = 0.60f;

    EntityId e = world.createEntity();
    world.addComponent(e, t);
    world.addComponent(e, d);

    SceneView scene;
    decalRenderSystem(world, scene);

    ASSERT_EQ(scene.decalDraws.size(), 1u);
    const DecalDraw& draw = scene.decalDraws[0];

    EXPECT_EQ(draw.albedoTexture, kFakeAlbedo);
    EXPECT_EQ(draw.normalTexture, kFakeNormal);
    EXPECT_NEAR(draw.roughness, 0.75f, k_eps);
    EXPECT_NEAR(draw.metalness, 0.25f, k_eps);
    EXPECT_NEAR(draw.opacity,   0.60f, k_eps);
}

TEST(DecalRenderSystem, NullNormalTextureTransferredAsNull)
{
    World world;

    TransformComponent t;
    DecalComponent     d;
    d.albedoTexture = kFakeAlbedo;
    d.normalTexture = nullptr;  // no normal map

    EntityId e = world.createEntity();
    world.addComponent(e, t);
    world.addComponent(e, d);

    SceneView scene;
    decalRenderSystem(world, scene);

    ASSERT_EQ(scene.decalDraws.size(), 1u);
    EXPECT_EQ(scene.decalDraws[0].normalTexture, nullptr);
}

// ─── Accumulation across calls ───────────────────────────────────────────────

TEST(DecalRenderSystem, RepeatedCallsAccumulateDraws)
{
    // Calling the system twice appends to the existing decalDraws list.
    // This mirrors how meshRenderSystem accumulates across calls.
    World world;

    TransformComponent t;
    DecalComponent     d;
    d.albedoTexture = kFakeAlbedo;

    EntityId e = world.createEntity();
    world.addComponent(e, t);
    world.addComponent(e, d);

    SceneView scene;
    decalRenderSystem(world, scene);  // appends 1
    decalRenderSystem(world, scene);  // appends 1 more

    EXPECT_EQ(scene.decalDraws.size(), 2u);
}
