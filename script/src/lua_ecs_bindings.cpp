// lua_ecs_bindings.cpp
// Implements the daedalus.* Lua ECS binding functions.
//
// All functions are registered on the "daedalus" Lua table. Each function
// receives an EntityId (integer) from Lua, looks up the component in the
// active World (stored via worldPtr), and reads or writes it.
//
// Error handling: if the entity doesn't have the expected component the
// function is a no-op / returns nil rather than throwing a Lua error, so
// scripts can call these unconditionally and guard on nil returns.

#include "lua_ecs_bindings.h"
#include "daedalus/core/components/transform_component.h"
#include "daedalus/core/ecs/world.h"

#include <cmath>
#include <cstdio>
#include <sol/sol.hpp>

// GLM for quaternion → yaw extraction and yaw → quaternion construction.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace daedalus::script
{

void registerEcsBindings(sol::state& lua, daedalus::World** worldPtr)
{
    // Create (or reuse) the "daedalus" table in the Lua global environment.
    sol::table tbl = lua.create_named_table("daedalus");

    // ── daedalus.getPosition(entityId) → x, y, z ─────────────────────────────

    tbl.set_function("getPosition",
        [worldPtr](lua_Integer entityId) -> std::tuple<float, float, float>
        {
            if (!*worldPtr) { return { 0.0f, 0.0f, 0.0f }; }
            daedalus::World& world = **worldPtr;

            const auto id = static_cast<EntityId>(entityId);
            if (!world.hasComponent<daedalus::TransformComponent>(id))
            {
                return { 0.0f, 0.0f, 0.0f };
            }

            const auto& tc = world.getComponent<daedalus::TransformComponent>(id);
            return { tc.position.x, tc.position.y, tc.position.z };
        }
    );

    // ── daedalus.setPosition(entityId, x, y, z) ──────────────────────────────

    tbl.set_function("setPosition",
        [worldPtr](lua_Integer entityId, float x, float y, float z)
        {
            if (!*worldPtr) { return; }
            daedalus::World& world = **worldPtr;

            const auto id = static_cast<EntityId>(entityId);
            if (!world.hasComponent<daedalus::TransformComponent>(id)) { return; }

            world.getComponent<daedalus::TransformComponent>(id).position =
                glm::vec3(x, y, z);
        }
    );

    // ── daedalus.getYaw(entityId) → yaw ──────────────────────────────────────
    // Extracts the yaw (rotation around world Y) from the quaternion.
    // yaw = atan2(2*(w*y + x*z), 1 - 2*(y*y + z*z))

    tbl.set_function("getYaw",
        [worldPtr](lua_Integer entityId) -> float
        {
            if (!*worldPtr) { return 0.0f; }
            daedalus::World& world = **worldPtr;

            const auto id = static_cast<EntityId>(entityId);
            if (!world.hasComponent<daedalus::TransformComponent>(id))
            {
                return 0.0f;
            }

            const auto& q = world.getComponent<daedalus::TransformComponent>(id).rotation;
            // Extract yaw: rotation around Y axis.
            const float sinY = 2.0f * (q.w * q.y + q.x * q.z);
            const float cosY = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
            return std::atan2(sinY, cosY);
        }
    );

    // ── daedalus.setYaw(entityId, yaw) ────────────────────────────────────────
    // Sets the quaternion to a pure Y-axis rotation (preserves pitch/roll = 0).

    tbl.set_function("setYaw",
        [worldPtr](lua_Integer entityId, float yaw)
        {
            if (!*worldPtr) { return; }
            daedalus::World& world = **worldPtr;

            const auto id = static_cast<EntityId>(entityId);
            if (!world.hasComponent<daedalus::TransformComponent>(id)) { return; }

            world.getComponent<daedalus::TransformComponent>(id).rotation =
                glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        }
    );

    // ── daedalus.isAlive(entityId) → bool ───────────────────────────────────────────────

    tbl.set_function("isAlive",
        [worldPtr](lua_Integer entityId) -> bool
        {
            if (!*worldPtr) { return false; }
            return (*worldPtr)->isValid(static_cast<EntityId>(entityId));
        }
    );

    // ── daedalus.getScale(entityId) → x, y, z ─────────────────────────────────────────

    tbl.set_function("getScale",
        [worldPtr](lua_Integer entityId) -> std::tuple<float, float, float>
        {
            if (!*worldPtr) { return { 1.0f, 1.0f, 1.0f }; }
            daedalus::World& world = **worldPtr;

            const auto id = static_cast<EntityId>(entityId);
            if (!world.hasComponent<daedalus::TransformComponent>(id))
                return { 1.0f, 1.0f, 1.0f };

            const auto& s = world.getComponent<daedalus::TransformComponent>(id).scale;
            return { s.x, s.y, s.z };
        }
    );

    // ── daedalus.setScale(entityId, x, y, z) ────────────────────────────────────────

    tbl.set_function("setScale",
        [worldPtr](lua_Integer entityId, float x, float y, float z)
        {
            if (!*worldPtr) { return; }
            daedalus::World& world = **worldPtr;

            const auto id = static_cast<EntityId>(entityId);
            if (!world.hasComponent<daedalus::TransformComponent>(id)) { return; }

            world.getComponent<daedalus::TransformComponent>(id).scale =
                glm::vec3(x, y, z);
        }
    );

    // ── daedalus.getForward(entityId) → x, y, z ─────────────────────────────────────
    // Local +Z axis (0,0,1) rotated by the entity's orientation quaternion.

    tbl.set_function("getForward",
        [worldPtr](lua_Integer entityId) -> std::tuple<float, float, float>
        {
            if (!*worldPtr) { return { 0.0f, 0.0f, 1.0f }; }
            daedalus::World& world = **worldPtr;

            const auto id = static_cast<EntityId>(entityId);
            if (!world.hasComponent<daedalus::TransformComponent>(id))
                return { 0.0f, 0.0f, 1.0f };

            const auto& tc = world.getComponent<daedalus::TransformComponent>(id);
            const glm::vec3 fwd = tc.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            return { fwd.x, fwd.y, fwd.z };
        }
    );

    // ── daedalus.distanceTo(entityId, x, y, z) → float ─────────────────────────────

    tbl.set_function("distanceTo",
        [worldPtr](lua_Integer entityId, float x, float y, float z) -> float
        {
            if (!*worldPtr) { return 0.0f; }
            daedalus::World& world = **worldPtr;

            const auto id = static_cast<EntityId>(entityId);
            if (!world.hasComponent<daedalus::TransformComponent>(id))
                return 0.0f;

            const auto& tc = world.getComponent<daedalus::TransformComponent>(id);
            return glm::distance(tc.position, glm::vec3(x, y, z));
        }
    );

    // ── daedalus.log(message) ───────────────────────────────────────────────────────────────────

    tbl.set_function("log",
        [](const std::string& message)
        {
            std::printf("[Script] %s\n", message.c_str());
        }
    );
}

} // namespace daedalus::script
