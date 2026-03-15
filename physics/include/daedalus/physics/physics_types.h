// physics_types.h
// Plain data types shared across the DaedalusPhysics public interface.
//
// No Jolt headers are included here. Consumers of DaedalusPhysics see only
// engine-native types (GLM, daedalus::) — Jolt stays private to physics/src/.

#pragma once

#include "daedalus/core/types.h"

#include <expected>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace daedalus::physics
{

// ─── PhysicsError ─────────────────────────────────────────────────────────────

enum class PhysicsError : u32
{
    ShapeCreationFailed,  ///< Jolt could not create the requested collision shape.
    BodyCreationFailed,   ///< Jolt could not insert a body into the physics world.
    EntityNotFound,       ///< No physics body is registered for the given EntityId.
    InvalidDescriptor,    ///< The descriptor has inconsistent or degenerate values.
};

// ─── CollisionShape ───────────────────────────────────────────────────────────
// Matches the shape options exposed in the editor's Entity Property Inspector.

enum class CollisionShape : u32
{
    None,         ///< No collision; entity is non-physical.
    Box,          ///< Axis-aligned box. halfExtents in RigidBodyDesc.
    Capsule,      ///< Vertical capsule. radius + halfHeight in RigidBodyDesc.
    ConvexHull,   ///< Convex hull of supplied points (future; not yet implemented).
};

// ─── CharacterDesc ────────────────────────────────────────────────────────────
// Descriptor for a first-person or NPC character controller.
// The Jolt CharacterVirtual backing this uses a vertical capsule shape.

struct CharacterDesc
{
    /// Capsule cross-section radius (metres).
    float capsuleRadius     = 0.35f;

    /// Capsule half-height EXCLUDING hemisphere caps (metres).
    /// Total character height = 2 * (capsuleHalfHeight + capsuleRadius) = 1.8 m default.
    float capsuleHalfHeight = 0.55f;

    /// Eye height measured from the character's feet (metres).
    /// Used by syncTransforms to place the camera above the physics origin.
    float eyeHeightFromFeet = 1.65f;

    /// Maximum walkable slope (degrees). Steeper slopes are treated as walls.
    float maxSlopeAngle     = 45.0f;
};

/// Eye height default — exported so app code can use the same constant without
/// depending on a CharacterDesc instance.
inline constexpr float kDefaultEyeHeight = 1.65f;

// ─── RigidBodyDesc ────────────────────────────────────────────────────────────
// Descriptor for a dynamic or static rigid body entity.

struct RigidBodyDesc
{
    CollisionShape shape       = CollisionShape::Box;

    glm::vec3      halfExtents = glm::vec3(0.5f);  ///< Half-extents for Box shape.
    float          radius      = 0.5f;              ///< Radius for Capsule shape.
    float          halfHeight  = 0.5f;              ///< Half-height for Capsule shape.

    float          mass        = 1.0f;   ///< kg; ignored for static bodies.
    bool           isStatic    = false;  ///< True → immovable; false → dynamic.

    /// Initial orientation of the body at spawn (identity = no rotation).
    glm::quat      initialRot  = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

// ─── RayHit ───────────────────────────────────────────────────────────────────
// Result of a successful IPhysicsWorld::queryRay() call.

struct RayHit
{
    glm::vec3 position;         ///< World-space intersection point.
    glm::vec3 normal;           ///< Surface normal at the hit, pointing away from surface.
    float     distance = 0.0f;  ///< Distance from ray origin to hit (metres).
};

} // namespace daedalus::physics
