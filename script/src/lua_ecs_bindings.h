// lua_ecs_bindings.h  (private — never included from script/include/)
// Registers the daedalus.* ECS binding functions into a sol::state.
//
// Exposed Lua API (accessed as daedalus.<name>):
//
//   daedalus.getPosition(entityId) → x, y, z
//     Returns the world-space position of the entity's TransformComponent.
//     Returns nil, nil, nil if the entity has no TransformComponent.
//
//   daedalus.setPosition(entityId, x, y, z)
//     Sets the world-space position of the entity's TransformComponent.
//     No-op if the entity has no TransformComponent.
//
//   daedalus.getYaw(entityId) → yaw (radians, float)
//     Returns the yaw angle extracted from the entity's rotation quaternion.
//     Returns 0.0 if the entity has no TransformComponent.
//
//   daedalus.setYaw(entityId, yaw)
//     Sets the entity's rotation quaternion to a pure Y-axis rotation by yaw.
//     No-op if the entity has no TransformComponent.
//
//   daedalus.isAlive(entityId) → bool
//     Returns true if the entity is currently alive in the active World.
//
//   daedalus.getScale(entityId) → x, y, z
//     Returns the scale of the entity's TransformComponent.
//     Returns 1, 1, 1 if the entity has no TransformComponent.
//
//   daedalus.setScale(entityId, x, y, z)
//     Sets the scale of the entity's TransformComponent.
//     No-op if the entity has no TransformComponent.
//
//   daedalus.getForward(entityId) → x, y, z
//     Returns the entity's forward direction (local +Z axis rotated by the
//     entity's orientation quaternion).  Normalised.
//     Returns 0, 0, 1 if the entity has no TransformComponent.
//
//   daedalus.distanceTo(entityId, x, y, z) → float
//     Returns the Euclidean distance from the entity's position to (x, y, z).
//     Returns 0.0 if the entity has no TransformComponent.
//
//   daedalus.log(message)
//     Writes message to stdout with a [Script] prefix for debugging.

#pragma once

#include <sol/sol.hpp>

namespace daedalus
{
    class World;
}

namespace daedalus::script
{

/// Register all daedalus.* ECS binding functions into `lua`.
///
/// `worldPtr` is a pointer to the current ECS World; it is written into the
/// Lua state once per update() call by the SolScriptEngine so that bindings
/// always target the correct world.
///
/// @param lua       The sol::state to register functions into.
/// @param worldPtr  Pointer to a (non-owning) raw pointer to the active World.
///                  SolScriptEngine updates *worldPtr before calling scripts.
void registerEcsBindings(sol::state& lua, daedalus::World** worldPtr);

} // namespace daedalus::script
