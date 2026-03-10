// rigid_body_component.h
// ECS component that tags an entity as a physics rigid body.
//
// The component stores the shape/mass descriptor that was used to register
// the entity with IPhysicsWorld::addRigidBody(). The physics system owns the
// actual Jolt body; this component is the authoritative source for configuration
// and is useful for serialisation, editor display, and respawn logic.

#pragma once

#include "daedalus/physics/physics_types.h"

namespace daedalus
{

// ─── RigidBodyComponent ───────────────────────────────────────────────────────

struct RigidBodyComponent
{
    physics::RigidBodyDesc desc;
};

} // namespace daedalus
