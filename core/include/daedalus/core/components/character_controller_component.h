// character_controller_component.h
// ECS component that tags an entity as a physics character controller.
//
// The component stores the capsule/slope descriptor that was used to register
// the entity with IPhysicsWorld::addCharacter(). The physics system owns the
// actual CharacterVirtual; this component is the authoritative source for
// configuration and is useful for serialisation and editor display.

#pragma once

#include "daedalus/physics/physics_types.h"

namespace daedalus
{

// ─── CharacterControllerComponent ────────────────────────────────────────────

struct CharacterControllerComponent
{
    physics::CharacterDesc desc;
};

} // namespace daedalus
