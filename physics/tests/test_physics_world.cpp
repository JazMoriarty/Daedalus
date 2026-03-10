// test_physics_world.cpp
// Smoke tests for the Jolt-backed IPhysicsWorld.
//
// These tests exercise the public interface only; no Jolt headers are included.
// They verify that:
//   - makePhysicsWorld() constructs without errors.
//   - loadLevel() builds static geometry from a WorldMapData without errors.
//   - addCharacter() succeeds and registers an entity.
//   - Characters fall under gravity and land on the floor slab.
//   - syncTransforms() updates the ECS TransformComponent.
//   - removeBody() is idempotent and does not crash.
//   - queryRay() returns nullopt when nothing is in the scene.
//   - queryRay() hits a wall after loadLevel().

#include "daedalus/physics/i_physics_world.h"
#include "daedalus/core/components/transform_component.h"
#include "daedalus/core/ecs/world.h"
#include "daedalus/world/map_data.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

using namespace daedalus;
using namespace daedalus::physics;
using namespace daedalus::world;

// ─── Helpers ──────────────────────────────────────────────────────────────────

/// A minimal single-sector map: 10×10 XZ, floor=0, ceil=4.
/// Walls are all solid (no portals).
static WorldMapData makeSingleRoomMap()
{
    WorldMapData map;
    map.name = "PhysicsTest";

    Sector s;
    s.floorHeight = 0.0f;
    s.ceilHeight  = 4.0f;
    Wall w0; w0.p0 = {-5.0f, -5.0f}; s.walls.push_back(w0);
    Wall w1; w1.p0 = { 5.0f, -5.0f}; s.walls.push_back(w1);
    Wall w2; w2.p0 = { 5.0f,  5.0f}; s.walls.push_back(w2);
    Wall w3; w3.p0 = {-5.0f,  5.0f}; s.walls.push_back(w3);
    map.sectors.push_back(std::move(s));

    return map;
}

static const CharacterDesc kDefaultChar{};   // default capsule + slope settings

// ─── Construction ─────────────────────────────────────────────────────────────

TEST(PhysicsWorldTest, ConstructionSucceeds)
{
    // makePhysicsWorld() must not throw or crash.
    auto pw = makePhysicsWorld();
    ASSERT_NE(pw, nullptr);
}

TEST(PhysicsWorldTest, MultipleWorldsCanBeCreated)
{
    auto pw1 = makePhysicsWorld();
    auto pw2 = makePhysicsWorld();
    ASSERT_NE(pw1, nullptr);
    ASSERT_NE(pw2, nullptr);
}

// ─── loadLevel ────────────────────────────────────────────────────────────────

TEST(PhysicsWorldTest, LoadLevelSingleRoomSucceeds)
{
    auto pw = makePhysicsWorld();
    const auto result = pw->loadLevel(makeSingleRoomMap());
    EXPECT_TRUE(result.has_value())
        << "loadLevel should succeed for a valid single-room map";
}

TEST(PhysicsWorldTest, LoadLevelCanBeCalledTwice)
{
    // Second call should clear previous geometry without crashing.
    auto pw = makePhysicsWorld();
    EXPECT_TRUE(pw->loadLevel(makeSingleRoomMap()).has_value());
    EXPECT_TRUE(pw->loadLevel(makeSingleRoomMap()).has_value());
}

TEST(PhysicsWorldTest, LoadLevelEmptyMapSucceeds)
{
    auto pw = makePhysicsWorld();
    WorldMapData empty;
    EXPECT_TRUE(pw->loadLevel(empty).has_value());
}

// ─── addCharacter ─────────────────────────────────────────────────────────────

TEST(PhysicsWorldTest, AddCharacterSucceeds)
{
    auto pw = makePhysicsWorld();
    ASSERT_TRUE(pw->loadLevel(makeSingleRoomMap()).has_value());

    World ecs;
    const EntityId player = ecs.createEntity();
    ecs.addComponent(player, TransformComponent{ .position = glm::vec3(0.0f, 2.0f, 0.0f) });

    const auto result = pw->addCharacter(player, kDefaultChar, glm::vec3(0.0f, 2.0f, 0.0f));
    EXPECT_TRUE(result.has_value()) << "addCharacter should succeed";
}

// ─── Gravity — character falls onto the floor ─────────────────────────────────

TEST(PhysicsWorldTest, CharacterFallsToFloor)
{
    auto pw = makePhysicsWorld();
    ASSERT_TRUE(pw->loadLevel(makeSingleRoomMap()).has_value());

    World ecs;
    const EntityId player = ecs.createEntity();
    // Feet start at y=2.0 (2 m above the floor slab top at y≈0).
    ecs.addComponent(player, TransformComponent{ .position = glm::vec3(0.0f, 2.0f, 0.0f) });
    ASSERT_TRUE(pw->addCharacter(player, kDefaultChar, glm::vec3(0.0f, 2.0f, 0.0f)).has_value());

    // Step 2 seconds at 60 Hz.  The character should land and come to rest.
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i)
    {
        pw->step(dt);
        pw->syncTransforms(ecs);
    }

    const auto& tc = ecs.getComponent<TransformComponent>(player);
    // Feet should be at approximately y=0 (top of floor slab).
    EXPECT_NEAR(tc.position.y, 0.0f, 0.15f)
        << "character feet should settle on the floor after 2 s";
}

TEST(PhysicsWorldTest, CharacterDoesNotFallWithZeroGravitySteps)
{
    auto pw = makePhysicsWorld();
    ASSERT_TRUE(pw->loadLevel(makeSingleRoomMap()).has_value());

    World ecs;
    const EntityId player = ecs.createEntity();
    ecs.addComponent(player, TransformComponent{ .position = glm::vec3(0.0f, 2.0f, 0.0f) });
    ASSERT_TRUE(pw->addCharacter(player, kDefaultChar, glm::vec3(0.0f, 2.0f, 0.0f)).has_value());

    // A single tiny step should not move the character much.
    pw->step(1.0e-6f);
    pw->syncTransforms(ecs);

    const auto& tc = ecs.getComponent<TransformComponent>(player);
    EXPECT_GT(tc.position.y, 1.5f) << "character should not have fallen far yet";
}

// ─── syncTransforms ───────────────────────────────────────────────────────────

TEST(PhysicsWorldTest, SyncTransformsUpdatesPosition)
{
    auto pw = makePhysicsWorld();
    ASSERT_TRUE(pw->loadLevel(makeSingleRoomMap()).has_value());

    World ecs;
    const EntityId player = ecs.createEntity();
    ecs.addComponent(player, TransformComponent{ .position = glm::vec3(0.0f, 5.0f, 0.0f) });
    ASSERT_TRUE(pw->addCharacter(player, kDefaultChar, glm::vec3(0.0f, 5.0f, 0.0f)).has_value());

    const float initialY = ecs.getComponent<TransformComponent>(player).position.y;

    // After one step, gravity should have moved the character downward.
    pw->step(1.0f / 60.0f);
    pw->syncTransforms(ecs);

    const float newY = ecs.getComponent<TransformComponent>(player).position.y;
    EXPECT_LT(newY, initialY) << "syncTransforms should write updated physics position";
}

// ─── removeBody ───────────────────────────────────────────────────────────────

TEST(PhysicsWorldTest, RemoveBodyIsIdempotent)
{
    auto pw = makePhysicsWorld();
    ASSERT_TRUE(pw->loadLevel(makeSingleRoomMap()).has_value());

    World ecs;
    const EntityId player = ecs.createEntity();
    ecs.addComponent(player, TransformComponent{});
    ASSERT_TRUE(pw->addCharacter(player, kDefaultChar, glm::vec3(0.0f, 1.0f, 0.0f)).has_value());

    pw->removeBody(player);           // first call: removes the character
    pw->removeBody(player);           // second call: no-op, should not crash
    pw->removeBody(player + 9999u);   // unknown entity: no-op
}

// ─── queryRay ─────────────────────────────────────────────────────────────────

TEST(PhysicsWorldTest, QueryRayMissesEmptyWorld)
{
    // No level loaded: ray should find nothing.
    auto pw = makePhysicsWorld();
    const auto hit = pw->queryRay(glm::vec3(0.0f, 1.0f, 0.0f),
                                  glm::vec3(0.0f, -1.0f, 0.0f),
                                  100.0f);
    EXPECT_FALSE(hit.has_value());
}

TEST(PhysicsWorldTest, QueryRayHitsFloorSlab)
{
    // Floor slab created by loadLevel sits at y≈0 (top face).
    auto pw = makePhysicsWorld();
    ASSERT_TRUE(pw->loadLevel(makeSingleRoomMap()).has_value());

    // Cast a ray straight down from above the centre of the room.
    const auto hit = pw->queryRay(glm::vec3(0.0f, 3.0f, 0.0f),
                                  glm::vec3(0.0f, -1.0f, 0.0f),
                                  10.0f);

    ASSERT_TRUE(hit.has_value()) << "downward ray should hit the floor slab";
    EXPECT_NEAR(hit->position.y, 0.0f, 0.15f) << "hit should be near y=0 (floor surface)";
    EXPECT_GT(hit->distance, 0.0f);
}
