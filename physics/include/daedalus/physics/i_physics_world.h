// i_physics_world.h
// Pure interface for the Daedalus physics simulation.
//
// DaedalusPhysics wraps Jolt Physics behind this interface so that no Jolt
// types cross the module boundary. All higher-level code (app, game systems)
// depends only on this header.
//
// Responsibilities:
//   loadLevel  — build static collision geometry from WorldMapData
//   addCharacter / addRigidBody — register entities as physics actors
//   step       — advance the simulation by dt seconds
//   syncTransforms — write physics-computed positions back to ECS transforms
//   queryRay   — single ray cast query for hit-scanning and AI LOS
//
// Error handling: fallible operations return std::expected<void, PhysicsError>.
// Infallible operations (removeBody, setCharacterInput, step, sync) do not.

#pragma once

#include "daedalus/physics/physics_types.h"
#include "daedalus/world/map_data.h"
#include "daedalus/core/ecs/world.h"
#include "daedalus/core/types.h"

#include <expected>
#include <memory>
#include <optional>

#include <glm/glm.hpp>

namespace daedalus::physics
{

// ─── IPhysicsWorld ────────────────────────────────────────────────────────────

class IPhysicsWorld
{
public:
    virtual ~IPhysicsWorld() = default;

    IPhysicsWorld(const IPhysicsWorld&)            = delete;
    IPhysicsWorld& operator=(const IPhysicsWorld&) = delete;

    // ─── Level geometry ───────────────────────────────────────────────────────

    /// Build static collision bodies from all solid walls, floors, and ceilings
    /// in the given map data. Portal walls (open passages) are skipped; portal
    /// walls flagged WallFlags::Blocking receive a body (e.g. locked doors).
    ///
    /// Any previously loaded level geometry is removed first. Safe to call again
    /// on map transitions.
    ///
    /// @param map  The world map whose geometry defines the collision world.
    [[nodiscard]] virtual std::expected<void, PhysicsError>
    loadLevel(const world::WorldMapData& map) = 0;

    // ─── Entity registration ──────────────────────────────────────────────────

    /// Register a character controller backed by a Jolt CharacterVirtual.
    ///
    /// After registration, the entity's TransformComponent.position receives
    /// the character's foot position every syncTransforms() call.
    ///
    /// @param id              ECS entity owning this character.
    /// @param desc            Capsule geometry and slope parameters.
    /// @param initialFeetPos  World-space position of the character's feet at spawn.
    [[nodiscard]] virtual std::expected<void, PhysicsError>
    addCharacter(EntityId id, const CharacterDesc& desc, glm::vec3 initialFeetPos) = 0;

    /// Register a rigid body (dynamic or static) for the given entity.
    ///
    /// @param id          ECS entity owning this body.
    /// @param desc        Shape, mass, and motion type.
    /// @param initialPos  World-space centre position of the body at spawn.
    [[nodiscard]] virtual std::expected<void, PhysicsError>
    addRigidBody(EntityId id, const RigidBodyDesc& desc, glm::vec3 initialPos) = 0;

    /// Remove and destroy any physics body or character registered for `id`.
    /// No-op if no body is registered for `id`.
    virtual void removeBody(EntityId id) = 0;

    // ─── Per-frame control ────────────────────────────────────────────────────

    /// Set the desired horizontal movement velocity for a character this frame.
    ///
    /// The physics world applies gravity internally and combines it with this
    /// horizontal input before calling CharacterVirtual::Update each step.
    /// The velocity is consumed after the next step() call.
    ///
    /// @param id       EntityId of a character registered via addCharacter().
    /// @param velocity Desired horizontal velocity in world-space XZ (Y ignored).
    ///                 Magnitude should be in m/s.
    virtual void setCharacterInput(EntityId id, glm::vec3 velocity) = 0;

    /// Advance the physics simulation by `dt` seconds.
    /// Updates all character controllers and active rigid bodies.
    virtual void step(float dt) = 0;

    /// Write physics-computed positions and orientations back to ECS.
    ///
    /// For each registered character: sets TransformComponent.position to the
    /// character's foot position (GetPosition() − capsule half-extent).
    /// For each registered rigid body: sets TransformComponent.position and
    /// .rotation from the body's current Jolt transform.
    ///
    /// @param ecsWorld  The ECS World whose TransformComponents are updated.
    ///                  The caller must ensure all registered entities are alive.
    virtual void syncTransforms(daedalus::World& ecsWorld) = 0;

    // ─── Queries ──────────────────────────────────────────────────────────────

    /// Cast a ray against all registered physics bodies.
    ///
    /// @param origin   Ray start in world space.
    /// @param dir      Normalised direction.
    /// @param maxDist  Maximum ray length (metres).
    /// @return         Hit information, or std::nullopt if nothing was hit.
    [[nodiscard]] virtual std::optional<RayHit>
    queryRay(glm::vec3 origin, glm::vec3 dir, float maxDist) const = 0;

protected:
    IPhysicsWorld() = default;
};

// ─── Factory ──────────────────────────────────────────────────────────────────

/// Construct the Jolt-backed physics world implementation.
/// Jolt global state (allocator, factory, type registry) is initialised once
/// on the first call and cleaned up when the last physics world is destroyed.
[[nodiscard]] std::unique_ptr<IPhysicsWorld> makePhysicsWorld();

} // namespace daedalus::physics
