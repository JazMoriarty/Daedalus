// jolt_physics_world.cpp
// Jolt Physics-backed implementation of IPhysicsWorld.
//
// Jolt types must NEVER appear in public headers. All Jolt usage is confined
// to this translation unit and jolt_physics_world.h (also private).

#include "jolt_physics_world.h"
#include "daedalus/core/components/transform_component.h"

JPH_SUPPRESS_WARNING_PUSH
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
JPH_SUPPRESS_WARNING_POP

#include <cmath>
#include <limits>

namespace daedalus::physics
{

// ─── Constants ────────────────────────────────────────────────────────────────

static constexpr JPH::uint kMaxBodies             = 2048u;
static constexpr JPH::uint kNumBodyMutexes        = 0u;       // 0 = auto-select
static constexpr JPH::uint kMaxBodyPairs          = 2048u;
static constexpr JPH::uint kMaxContactConstraints = 2048u;
static constexpr JPH::uint kTempAllocBytes        = 10u * 1024u * 1024u;  // 10 MB
static constexpr int       kNumPhysicsThreads     = 2;
static constexpr float     kWallHalfThick         = 0.05f;    // box Z half-extent
static constexpr float     kSlabHalfThick         = 0.05f;    // floor slab Y half-extent
static constexpr float     kGravity               = 9.81f;

// ─── Jolt one-time global RAII ────────────────────────────────────────────────
// RegisterDefaultAllocator / Factory / RegisterTypes must each be called
// exactly once before any Jolt object is constructed.

struct JoltGlobalInit
{
    JoltGlobalInit()
    {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
    ~JoltGlobalInit()
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};

// ─── Conversion helpers ───────────────────────────────────────────────────────

static inline JPH::Vec3  toJPH (glm::vec3 v) noexcept { return { v.x, v.y, v.z }; }
static inline JPH::RVec3 toJPHR(glm::vec3 v) noexcept { return { v.x, v.y, v.z }; }
static inline glm::vec3 fromJPH(JPH::Vec3  v) noexcept
{
    return { v.GetX(), v.GetY(), v.GetZ() };
}

// ─── JoltPhysicsWorld — constructor / destructor ──────────────────────────────

JoltPhysicsWorld::JoltPhysicsWorld()
    : m_tempAllocator(kTempAllocBytes)
    , m_jobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, kNumPhysicsThreads)
{
    m_physicsSystem.Init(
        kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
        m_bpLayerInterface, m_obpFilter, m_olpFilter
    );
}

JoltPhysicsWorld::~JoltPhysicsWorld()
{
    clearLevelBodies();

    // Remove all entity-owned rigid bodies from the physics system.
    JPH::BodyInterface& bi = m_physicsSystem.GetBodyInterface();
    for (const auto& [entityId, bodyId] : m_rigidBodies)
    {
        bi.RemoveBody(bodyId);
        bi.DestroyBody(bodyId);
    }
    m_rigidBodies.clear();

    // CharacterVirtual is JPH::RefTarget; clearing the map drops all ref counts.
    m_characters.clear();
}

// ─── loadLevel ────────────────────────────────────────────────────────────────

std::expected<void, PhysicsError>
JoltPhysicsWorld::loadLevel(const world::WorldMapData& map)
{
    clearLevelBodies();  // remove any previously loaded level geometry

    JPH::BodyInterface& bi = m_physicsSystem.GetBodyInterface();

    for (const world::Sector& sector : map.sectors)
    {
        const int n = static_cast<int>(sector.walls.size());
        if (n < 3) { continue; }

        const float floorY = sector.floorHeight;
        const float ceilY  = sector.ceilHeight;
        const float wallH  = ceilY - floorY;
        const float midY   = (floorY + ceilY) * 0.5f;

        // ── Wall collision boxes ───────────────────────────────────────────────
        for (int i = 0; i < n; ++i)
        {
            const world::Wall& wall = sector.walls[i];

            // Skip passable portals; keep solid walls and explicitly Blocking ones.
            const bool solidWall =
                (wall.portalSectorId == world::INVALID_SECTOR_ID);
            const bool blocking  =
                world::hasFlag(wall.flags, world::WallFlags::Blocking);
            if (!solidWall && !blocking) { continue; }

            // Wall edge in XZ: Wall::p0 is glm::vec2(worldX, worldZ).
            const glm::vec2 p0    = wall.p0;
            const glm::vec2 p1    = sector.walls[(i + 1) % n].p0;
            const glm::vec2 delta = p1 - p0;
            const float     len   = glm::length(delta);
            if (len < 1.0e-4f) { continue; }

            // Box half-extents: X = half wall length, Y = half wall height, Z = depth.
            JPH::Ref<JPH::Shape> shape = new JPH::BoxShape(
                JPH::Vec3(len * 0.5f, wallH * 0.5f, kWallHalfThick)
            );

            // Midpoint of the edge in world space.
            const float midX = (p0.x + p1.x) * 0.5f;
            const float midZ = (p0.y + p1.y) * 0.5f;  // p0.y is world Z

            // Rotate box X-axis to align with the wall direction.
            // delta.y here is the world ΔZ component.
            const float     angle = std::atan2(-delta.y, delta.x);
            const JPH::Quat rot   = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), angle);

            JPH::BodyCreationSettings bcs(
                shape,
                JPH::RVec3(midX, midY, midZ),
                rot,
                JPH::EMotionType::Static,
                Layers::NON_MOVING
            );

            const JPH::BodyID bid =
                bi.CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
            if (!bid.IsInvalid()) { m_levelBodyIds.push_back(bid); }
        }

        // ── Floor slab (axis-aligned box from sector AABB footprint) ──────────
        {
            float minX =  std::numeric_limits<float>::max();
            float maxX = -std::numeric_limits<float>::max();
            float minZ =  std::numeric_limits<float>::max();
            float maxZ = -std::numeric_limits<float>::max();

            for (const world::Wall& w : sector.walls)
            {
                minX = std::min(minX, w.p0.x);
                maxX = std::max(maxX, w.p0.x);
                minZ = std::min(minZ, w.p0.y);
                maxZ = std::max(maxZ, w.p0.y);
            }

            const float hx = (maxX - minX) * 0.5f;
            const float hz = (maxZ - minZ) * 0.5f;

            if (hx > 1.0e-4f && hz > 1.0e-4f)
            {
                JPH::Ref<JPH::Shape> slab =
                    new JPH::BoxShape(JPH::Vec3(hx, kSlabHalfThick, hz));

                JPH::BodyCreationSettings bcs(
                    slab,
                    JPH::RVec3((minX + maxX) * 0.5f,
                               floorY - kSlabHalfThick,
                               (minZ + maxZ) * 0.5f),
                    JPH::Quat::sIdentity(),
                    JPH::EMotionType::Static,
                    Layers::NON_MOVING
                );

                const JPH::BodyID bid =
                    bi.CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
                if (!bid.IsInvalid()) { m_levelBodyIds.push_back(bid); }
            }
        }
    }

    // Rebuild the broad-phase BVH. Must be called after inserting static bodies
    // and before any character::Update or ray-cast queries.
    m_physicsSystem.OptimizeBroadPhase();
    return {};
}

// ─── addCharacter ─────────────────────────────────────────────────────────────

std::expected<void, PhysicsError>
JoltPhysicsWorld::addCharacter(EntityId id, const CharacterDesc& desc,
                               glm::vec3 initialFeetPos)
{
    const float halfHeight = desc.capsuleHalfHeight;
    const float radius     = desc.capsuleRadius;

    // Vertical capsule: half-height excludes the hemisphere end-caps.
    JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(halfHeight, radius);

    // RotatedTranslatedShape shifts the inner capsule upward so the outer
    // shape's origin sits at the character's feet. GetPosition() then returns
    // the feet position directly, with no offset arithmetic at read-back time.
    JPH::RotatedTranslatedShapeSettings rtSettings(
        JPH::Vec3(0.0f, halfHeight + radius, 0.0f),  // lift capsule centre above feet
        JPH::Quat::sIdentity(),
        capsule
    );
    const JPH::ShapeSettings::ShapeResult result = rtSettings.Create();
    if (result.HasError()) { return std::unexpected(PhysicsError::ShapeCreationFailed); }

    JPH::CharacterVirtualSettings cvs;
    cvs.mShape         = result.Get();
    cvs.mUp            = JPH::Vec3::sAxisY();
    cvs.mMaxSlopeAngle = JPH::DegreesToRadians(desc.maxSlopeAngle);

    JPH::Ref<JPH::CharacterVirtual> cv = new JPH::CharacterVirtual(
        &cvs,
        toJPHR(initialFeetPos),
        JPH::Quat::sIdentity(),
        /*inUserData=*/0,
        &m_physicsSystem
    );

    m_characters[id] = CharacterState{
        .character         = std::move(cv),
        .verticalVelocity  = 0.0f,
        .capsuleHalfHeight = halfHeight,
        .capsuleRadius     = radius,
    };
    return {};
}

// ─── addRigidBody ─────────────────────────────────────────────────────────────

std::expected<void, PhysicsError>
JoltPhysicsWorld::addRigidBody(EntityId id, const RigidBodyDesc& desc,
                               glm::vec3 initialPos)
{
    JPH::Ref<JPH::Shape> shape;
    switch (desc.shape)
    {
    case CollisionShape::Box:
        shape = new JPH::BoxShape(toJPH(desc.halfExtents));
        break;
    case CollisionShape::Capsule:
        shape = new JPH::CapsuleShape(desc.halfHeight, desc.radius);
        break;
    default:
        return std::unexpected(PhysicsError::InvalidDescriptor);
    }

    const bool dynamic = !desc.isStatic;

    // Convert GLM quaternion (w,x,y,z constructor) to Jolt quaternion (x,y,z,w).
    const JPH::Quat initialRot(
        desc.initialRot.x, desc.initialRot.y,
        desc.initialRot.z, desc.initialRot.w);

    JPH::BodyCreationSettings bcs(
        shape,
        toJPHR(initialPos),
        initialRot,
        dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
        dynamic ? Layers::MOVING : Layers::NON_MOVING
    );

    if (dynamic)
    {
        // Ask Jolt to calculate inertia tensor from the shape, but use our mass.
        bcs.mOverrideMassProperties =
            JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = desc.mass;
    }

    JPH::BodyInterface& bi = m_physicsSystem.GetBodyInterface();
    const JPH::BodyID bodyId = bi.CreateAndAddBody(
        bcs,
        dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate
    );
    if (bodyId.IsInvalid()) { return std::unexpected(PhysicsError::BodyCreationFailed); }

    m_rigidBodies[id] = bodyId;
    return {};
}

// ─── removeBody ───────────────────────────────────────────────────────────────

void JoltPhysicsWorld::removeBody(EntityId id)
{
    if (const auto it = m_characters.find(id); it != m_characters.end())
    {
        m_characters.erase(it);
        m_characterInputs.erase(id);
        return;
    }
    if (const auto it = m_rigidBodies.find(id); it != m_rigidBodies.end())
    {
        JPH::BodyInterface& bi = m_physicsSystem.GetBodyInterface();
        bi.RemoveBody(it->second);
        bi.DestroyBody(it->second);
        m_rigidBodies.erase(it);
    }
}

// ─── setCharacterInput ────────────────────────────────────────────────────────

void JoltPhysicsWorld::setCharacterInput(EntityId id, glm::vec3 velocity)
{
    m_characterInputs[id] = velocity;
}

// ─── step ─────────────────────────────────────────────────────────────────────

void JoltPhysicsWorld::step(float dt)
{
    const JPH::Vec3 gravity(0.0f, -kGravity, 0.0f);

    // Grab default collision filters once per step.
    const auto& bpFilter = m_physicsSystem.GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
    const auto& olFilter = m_physicsSystem.GetDefaultLayerFilter(Layers::MOVING);
    const JPH::BodyFilter  bodyFilter;   // default: collide with all bodies
    const JPH::ShapeFilter shapeFilter;  // default: collide with all sub-shapes

    for (auto& [id, state] : m_characters)
    {
        // ── Gravity accumulation ──────────────────────────────────────────────
        // IsSupported() returns true when the character stands on solid ground
        // (ground state != InAir / NotSupported).
        if (state.character->IsSupported()) {
            state.verticalVelocity = 0.0f;
        } else {
            state.verticalVelocity -= kGravity * dt;
        }

        // ── Merge horizontal input + accumulated vertical velocity ─────────────
        glm::vec3 hInput(0.0f);
        if (const auto it = m_characterInputs.find(id); it != m_characterInputs.end()) {
            hInput = it->second;
        }
        state.character->SetLinearVelocity(
            JPH::Vec3(hInput.x, state.verticalVelocity, hInput.z)
        );

        // ── Advance character, resolving contacts against the physics world ───
        state.character->Update(
            dt, gravity, bpFilter, olFilter, bodyFilter, shapeFilter,
            m_tempAllocator
        );
    }

    // Inputs are consumed once per step.
    m_characterInputs.clear();

    // Step all dynamic rigid bodies and contact resolution.
    constexpr int kCollisionSteps = 1;
    m_physicsSystem.Update(dt, kCollisionSteps, &m_tempAllocator, &m_jobSystem);
}

// ─── syncTransforms ───────────────────────────────────────────────────────────

void JoltPhysicsWorld::syncTransforms(daedalus::World& ecsWorld)
{
    // ── Characters — write feet position ──────────────────────────────────────
    // Because the character shape is a RotatedTranslatedShape with its origin at
    // the feet, GetPosition() directly gives the feet world-space position.
    for (const auto& [id, state] : m_characters)
    {
        if (!ecsWorld.isValid(id)) { continue; }
        if (!ecsWorld.hasComponent<daedalus::TransformComponent>(id)) { continue; }

        const JPH::RVec3 pos = state.character->GetPosition();
        ecsWorld.getComponent<daedalus::TransformComponent>(id).position = glm::vec3(
            static_cast<float>(pos.GetX()),
            static_cast<float>(pos.GetY()),
            static_cast<float>(pos.GetZ())
        );
    }

    // ── Rigid bodies — write position and orientation ─────────────────────────
    JPH::BodyInterface& bi = m_physicsSystem.GetBodyInterface();
    for (const auto& [id, bodyId] : m_rigidBodies)
    {
        if (!ecsWorld.isValid(id)) { continue; }
        if (!ecsWorld.hasComponent<daedalus::TransformComponent>(id)) { continue; }

        const JPH::RVec3 pos = bi.GetPosition(bodyId);
        const JPH::Quat  rot = bi.GetRotation(bodyId);

        auto& tc = ecsWorld.getComponent<daedalus::TransformComponent>(id);
        tc.position = glm::vec3(
            static_cast<float>(pos.GetX()),
            static_cast<float>(pos.GetY()),
            static_cast<float>(pos.GetZ())
        );
        // glm::quat(w, x, y, z) constructor ordering (note: not XYZW like members).
        tc.rotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
    }
}

// ─── queryRay ─────────────────────────────────────────────────────────────────

std::optional<RayHit>
JoltPhysicsWorld::queryRay(glm::vec3 origin, glm::vec3 dir, float maxDist) const
{
    // RRayCast: origin in double/single-precision world space; direction scaled
    // by maxDist so mFraction ∈ [0,1] maps to actual distance.
    JPH::RRayCast ray{
        toJPHR(origin),
        JPH::Vec3(dir.x * maxDist, dir.y * maxDist, dir.z * maxDist)
    };

    JPH::RayCastResult hit;
    if (!m_physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit)) {
        return std::nullopt;
    }

    const float dist      = hit.mFraction * maxDist;
    const glm::vec3 hitPos = origin + dir * dist;

    // Retrieve the surface normal using a read lock to safely access the body.
    glm::vec3 normal(0.0f, 1.0f, 0.0f);
    {
        JPH::BodyLockRead lock(m_physicsSystem.GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Vec3 n = lock.GetBody().GetWorldSpaceSurfaceNormal(
                hit.mSubShapeID2,
                ray.GetPointOnRay(hit.mFraction)
            );
            normal = fromJPH(n);
        }
    }

    return RayHit{ hitPos, normal, dist };
}

// ─── clearLevelBodies (private) ───────────────────────────────────────────────

void JoltPhysicsWorld::clearLevelBodies()
{
    JPH::BodyInterface& bi = m_physicsSystem.GetBodyInterface();
    for (const JPH::BodyID& id : m_levelBodyIds)
    {
        bi.RemoveBody(id);
        bi.DestroyBody(id);
    }
    m_levelBodyIds.clear();
}

// ─── Factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<IPhysicsWorld> makePhysicsWorld()
{
    // JoltGlobalInit is a Meyer's singleton; constructed on first call, destroyed
    // at program exit — after all physics worlds have been destroyed by callers.
    static JoltGlobalInit joltGlobalInit;
    return std::make_unique<JoltPhysicsWorld>();
}

} // namespace daedalus::physics
