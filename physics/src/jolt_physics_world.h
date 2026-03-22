// jolt_physics_world.h  (private — never included from physics/include/)
// Jolt-backed implementation of IPhysicsWorld.
//
// All Jolt types are confined to this translation unit. The public interface
// (i_physics_world.h) is the only symbol that crosses the module boundary.

#pragma once

#include "daedalus/physics/i_physics_world.h"

// Jolt.h must be the very first Jolt include in every TU that uses Jolt.
#include <Jolt/Jolt.h>

JPH_SUPPRESS_WARNING_PUSH

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

JPH_SUPPRESS_WARNING_POP

#include <memory>
#include <unordered_map>
#include <vector>

namespace daedalus::physics
{

// ─── Object layer indices ─────────────────────────────────────────────────────
// Two object layers: static world geometry and dynamic/moving bodies.
// CharacterVirtual is not a body — it interacts with both layers.

namespace Layers
{
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING     = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING{0};
    static constexpr JPH::BroadPhaseLayer MOVING    {1};
    static constexpr JPH::uint            NUM_LAYERS = 2;
}

// ─── Broad-phase layer interface ──────────────────────────────────────────────

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        m_objectToBP[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        m_objectToBP[Layers::MOVING]     = BroadPhaseLayers::MOVING;
    }

    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return m_objectToBP[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer))
        {
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NON_MOVING):
            return "NON_MOVING";
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::MOVING):
            return "MOVING";
        default:
            return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer m_objectToBP[Layers::NUM_LAYERS];
};

// ─── Object vs broad-phase filter ────────────────────────────────────────────

class OBPLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer inObj,
                       JPH::BroadPhaseLayer inBP) const override
    {
        switch (inObj)
        {
        case Layers::NON_MOVING:
            return inBP == BroadPhaseLayers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            return false;
        }
    }
};

// ─── Object layer pair filter ─────────────────────────────────────────────────

class OLPFilterImpl final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer inA, JPH::ObjectLayer inB) const override
    {
        switch (inA)
        {
        case Layers::NON_MOVING:
            return inB == Layers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            return false;
        }
    }
};

// ─── Per-character runtime state ──────────────────────────────────────────────

struct CharacterState
{
    JPH::Ref<JPH::CharacterVirtual> character;
    float verticalVelocity  = 0.0f;  ///< Accumulated by gravity; reset on landing.
    float capsuleHalfHeight = 0.55f; ///< Stored for feet-position calculation.
    float capsuleRadius     = 0.35f;
};

// ─── JoltPhysicsWorld ─────────────────────────────────────────────────────────

class JoltPhysicsWorld final : public IPhysicsWorld
{
public:
    JoltPhysicsWorld();
    ~JoltPhysicsWorld() override;

    // IPhysicsWorld
    std::expected<void, PhysicsError>
        loadLevel(const world::WorldMapData& map)              override;

    std::expected<void, PhysicsError>
        addCharacter(EntityId id, const CharacterDesc& desc,
                     glm::vec3 initialFeetPos)                 override;

    std::expected<void, PhysicsError>
        addRigidBody(EntityId id, const RigidBodyDesc& desc,
                     glm::vec3 initialPos)                     override;

    void removeBody(EntityId id)                               override;
    void setCharacterInput(EntityId id, glm::vec3 velocity)   override;
    void step(float dt)                                        override;
    void syncTransforms(daedalus::World& ecsWorld)             override;

    std::optional<RayHit>
        queryRay(glm::vec3 origin, glm::vec3 dir,
                 float maxDist) const                          override;

private:
    // ── Jolt subsystems (declaration order == destruction order reversed)
    // Destruction order from last to first declared:
    //   m_physicsSystem → m_olpFilter → m_obpFilter → m_bpLayerInterface
    //   → m_jobSystem → m_tempAllocator
    // PhysicsSystem is destroyed first (after we've cleared bodies manually in
    // the dtor), then the supporting subsystems.
    JPH::TempAllocatorImpl    m_tempAllocator;
    JPH::JobSystemThreadPool  m_jobSystem;
    BPLayerInterfaceImpl      m_bpLayerInterface;
    OBPLayerFilterImpl        m_obpFilter;
    OLPFilterImpl             m_olpFilter;
    JPH::PhysicsSystem        m_physicsSystem;

    // ── Tracked Jolt body IDs for level geometry (removed on loadLevel / dtor)
    std::vector<JPH::BodyID>  m_levelBodyIds;

    // ── Tracked rigid-body entities
    std::unordered_map<EntityId, JPH::BodyID> m_rigidBodies;

    // ── Tracked character entities
    std::unordered_map<EntityId, CharacterState> m_characters;

    // ── Pending per-frame character inputs (consumed during step)
    std::unordered_map<EntityId, glm::vec3> m_characterInputs;

    // ── Helpers
    void clearLevelBodies();
};

} // namespace daedalus::physics
