// test_ecs_world.cpp
// Unit tests for daedalus::World — entity lifecycle, component CRUD,
// archetype iteration, and multi-component filtering.

#include "daedalus/core/ecs/world.h"

#include <gtest/gtest.h>

using namespace daedalus;

// ─── Test components ──────────────────────────────────────────────────────────

struct Position { float x = 0.0f; float y = 0.0f; };
struct Velocity { float vx = 0.0f; float vy = 0.0f; };
struct Tag      {};   // zero-size marker component

// ─── Entity lifecycle ─────────────────────────────────────────────────────────

TEST(EcsWorld, CreateEntityReturnsUniqueIds)
{
    World world;
    const EntityId a = world.createEntity();
    const EntityId b = world.createEntity();
    EXPECT_NE(a, b);
}

TEST(EcsWorld, NewEntityIsValid)
{
    World world;
    const EntityId e = world.createEntity();
    EXPECT_TRUE(world.isValid(e));
}

TEST(EcsWorld, DestroyedEntityIsInvalid)
{
    World world;
    const EntityId e = world.createEntity();
    world.destroyEntity(e);
    EXPECT_FALSE(world.isValid(e));
}

TEST(EcsWorld, InvalidHandleIsNotValid)
{
    const World world;
    EXPECT_FALSE(world.isValid(EntityId{ 0 }));
}

TEST(EcsWorld, EntityCountTracksCreatesAndDestroys)
{
    World world;
    EXPECT_EQ(world.entityCount(), 0u);

    const EntityId a = world.createEntity();
    const EntityId b = world.createEntity();
    EXPECT_EQ(world.entityCount(), 2u);

    world.destroyEntity(a);
    EXPECT_EQ(world.entityCount(), 1u);

    world.destroyEntity(b);
    EXPECT_EQ(world.entityCount(), 0u);
}

// ─── Component add / get / has ────────────────────────────────────────────────

TEST(EcsWorld, AddAndGetComponent)
{
    World world;
    const EntityId e = world.createEntity();
    world.addComponent(e, Position{ 3.0f, 7.0f });

    const Position& p = world.getComponent<Position>(e);
    EXPECT_FLOAT_EQ(p.x, 3.0f);
    EXPECT_FLOAT_EQ(p.y, 7.0f);
}

TEST(EcsWorld, HasComponentReturnsTrueAfterAdd)
{
    World world;
    const EntityId e = world.createEntity();
    EXPECT_FALSE(world.hasComponent<Position>(e));

    world.addComponent(e, Position{});
    EXPECT_TRUE(world.hasComponent<Position>(e));
}

TEST(EcsWorld, MutateComponentViaGetComponent)
{
    World world;
    const EntityId e = world.createEntity();
    world.addComponent(e, Position{ 1.0f, 2.0f });

    world.getComponent<Position>(e).x = 99.0f;
    EXPECT_FLOAT_EQ(world.getComponent<Position>(e).x, 99.0f);
}

TEST(EcsWorld, MultipleComponentsOnOneEntity)
{
    World world;
    const EntityId e = world.createEntity();
    world.addComponent(e, Position{ 1.0f, 2.0f });
    world.addComponent(e, Velocity{ 3.0f, 4.0f });

    EXPECT_TRUE(world.hasComponent<Position>(e));
    EXPECT_TRUE(world.hasComponent<Velocity>(e));
    EXPECT_FLOAT_EQ(world.getComponent<Velocity>(e).vx, 3.0f);
}

// ─── removeComponent ──────────────────────────────────────────────────────────

TEST(EcsWorld, RemoveComponentClearsHas)
{
    World world;
    const EntityId e = world.createEntity();
    world.addComponent(e, Position{});
    world.addComponent(e, Velocity{});

    world.removeComponent<Velocity>(e);

    EXPECT_TRUE (world.hasComponent<Position>(e));
    EXPECT_FALSE(world.hasComponent<Velocity>(e));
}

// ─── each<T> iteration ────────────────────────────────────────────────────────

TEST(EcsWorld, EachVisitsAllMatchingEntities)
{
    World world;
    const EntityId a = world.createEntity();
    const EntityId b = world.createEntity();
    const EntityId c = world.createEntity();   // no Position — must be skipped

    world.addComponent(a, Position{ 1.0f, 0.0f });
    world.addComponent(b, Position{ 2.0f, 0.0f });
    world.addComponent(c, Velocity{ 0.0f, 0.0f });

    float sum = 0.0f;
    world.each<Position>([&](EntityId, Position& p) { sum += p.x; });

    EXPECT_FLOAT_EQ(sum, 3.0f);  // 1 + 2
}

TEST(EcsWorld, EachAllowsMutation)
{
    World world;
    const EntityId e = world.createEntity();
    world.addComponent(e, Position{ 0.0f, 0.0f });
    world.addComponent(e, Velocity{ 5.0f, -3.0f });

    world.each<Position, Velocity>([](EntityId, Position& p, Velocity& v)
    {
        p.x += v.vx;
        p.y += v.vy;
    });

    const Position& p = world.getComponent<Position>(e);
    EXPECT_FLOAT_EQ(p.x,  5.0f);
    EXPECT_FLOAT_EQ(p.y, -3.0f);
}

TEST(EcsWorld, EachTwoComponentsFiltersCorrectly)
{
    World world;

    // Has both Position + Velocity — must be visited.
    const EntityId both = world.createEntity();
    world.addComponent(both, Position{ 1.0f, 0.0f });
    world.addComponent(both, Velocity{});

    // Has Position only — must be skipped.
    const EntityId posOnly = world.createEntity();
    world.addComponent(posOnly, Position{ 99.0f, 0.0f });

    int visited = 0;
    world.each<Position, Velocity>([&](EntityId, Position&, Velocity&) { ++visited; });

    EXPECT_EQ(visited, 1);
}

TEST(EcsWorld, EachOnEmptyWorldDoesNothing)
{
    World world;
    int visited = 0;
    world.each<Position>([&](EntityId, Position&) { ++visited; });
    EXPECT_EQ(visited, 0);
}

// ─── Stability across structural changes ──────────────────────────────────────

TEST(EcsWorld, RemainingEntityAccessibleAfterPeerDestroyed)
{
    World world;
    const EntityId a = world.createEntity();
    const EntityId b = world.createEntity();
    world.addComponent(a, Position{ 10.0f, 20.0f });
    world.addComponent(b, Position{ 30.0f, 40.0f });

    world.destroyEntity(a);

    ASSERT_TRUE(world.isValid(b));
    EXPECT_FLOAT_EQ(world.getComponent<Position>(b).x, 30.0f);
}

TEST(EcsWorld, RecycledSlotIsValid)
{
    World world;
    const EntityId a = world.createEntity();
    world.destroyEntity(a);
    const EntityId b = world.createEntity();
    EXPECT_TRUE(world.isValid(b));
    // The recycled handle for a must still be invalid.
    EXPECT_FALSE(world.isValid(a));
}
