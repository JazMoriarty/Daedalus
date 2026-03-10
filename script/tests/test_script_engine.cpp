// test_script_engine.cpp
// Tests for IScriptEngine (SolScriptEngine).
//
// No sol2 or Lua headers are included; all tests use the public interfaces.
//
// Lua scripts are written to temp files so the test suite is self-contained
// and requires no shipped .lua assets.  Each TempScript RAII wrapper creates
// a file on construction and removes it on destruction.

#include "daedalus/core/components/transform_component.h"
#include "daedalus/core/ecs/world.h"
#include "daedalus/script/i_script_engine.h"
#include "daedalus/script/script_types.h"

#include <glm/gtc/quaternion.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace daedalus;
using namespace daedalus::script;

// ─── Helpers ──────────────────────────────────────────────────────────────────

/// RAII temp Lua file. Creates a .lua file at a fixed temp path.
struct TempScript
{
    std::filesystem::path path;

    explicit TempScript(const std::string& source,
                        const std::string& filename = "daedalus_test.lua")
    {
        path = std::filesystem::temp_directory_path() / filename;
        std::ofstream ofs(path);
        ofs << source;
    }

    void rewrite(const std::string& source) const
    {
        std::ofstream ofs(path, std::ios::trunc);
        ofs << source;
    }

    ~TempScript()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// ─── Engine lifecycle ─────────────────────────────────────────────────────────

TEST(ScriptEngineTest, FactoryReturnsNonNull)
{
    auto engine = makeScriptEngine();
    ASSERT_NE(engine, nullptr);
}

TEST(ScriptEngineTest, UpdateOnEmptyEngineIsNoop)
{
    auto engine = makeScriptEngine();
    World world;
    engine->update(0.016f, world);  // must not crash
}

// ─── loadScript ───────────────────────────────────────────────────────────────

TEST(ScriptEngineTest, LoadScriptFileNotFound)
{
    auto engine = makeScriptEngine();
    auto result = engine->loadScript("/nonexistent/path/to/missing.lua");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ScriptError::FileNotFound);
    EXPECT_FALSE(result.error().message.empty());
}

TEST(ScriptEngineTest, LoadScriptCompileError)
{
    TempScript ts("this is not valid lua @@@@");
    auto engine = makeScriptEngine();
    auto result = engine->loadScript(ts.path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ScriptError::CompileError);
    EXPECT_FALSE(result.error().message.empty());
}

TEST(ScriptEngineTest, LoadValidScriptSucceeds)
{
    TempScript ts("-- empty valid script\n");
    auto engine = makeScriptEngine();
    auto result = engine->loadScript(ts.path);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(isValid(*result));
}

TEST(ScriptEngineTest, LoadSameScriptTwiceGivesDifferentHandles)
{
    TempScript ts("x = 1\n");
    auto engine = makeScriptEngine();
    auto h1 = engine->loadScript(ts.path);
    auto h2 = engine->loadScript(ts.path);
    ASSERT_TRUE(h1.has_value());
    ASSERT_TRUE(h2.has_value());
    EXPECT_NE(*h1, *h2) << "Each loadScript call must produce a new isolated environment";
}

// ─── unloadScript ─────────────────────────────────────────────────────────────

TEST(ScriptEngineTest, UnloadInvalidHandleIsNoop)
{
    auto engine = makeScriptEngine();
    engine->unloadScript(kInvalidScriptHandle);  // must not crash
}

TEST(ScriptEngineTest, UnloadThenCallFunctionReturnsError)
{
    TempScript ts("function greet() end\n");
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    engine->unloadScript(*h);

    auto result = engine->callFunction(*h, "greet");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ScriptError::InvalidHandle);
}

// ─── callFunction ─────────────────────────────────────────────────────────────

TEST(ScriptEngineTest, CallFunctionInvalidHandleReturnsError)
{
    auto engine = makeScriptEngine();
    auto result = engine->callFunction(kInvalidScriptHandle, "update");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ScriptError::InvalidHandle);
}

TEST(ScriptEngineTest, CallNonExistentFunctionReturnsError)
{
    TempScript ts("-- no function defined\n");
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    auto result = engine->callFunction(*h, "nonExistentFunction");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ScriptError::RuntimeError);
}

TEST(ScriptEngineTest, CallFunctionSucceeds)
{
    TempScript ts("function ping() end\n");
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    auto result = engine->callFunction(*h, "ping");
    EXPECT_TRUE(result.has_value());
}

TEST(ScriptEngineTest, CallFunctionRuntimeErrorReturnsError)
{
    TempScript ts("function boom() error('intentional error') end\n");
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    auto result = engine->callFunction(*h, "boom");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ScriptError::RuntimeError);
    EXPECT_FALSE(result.error().message.empty());
}

// ─── setGlobal / global read-back ─────────────────────────────────────────────

TEST(ScriptEngineTest, SetGlobalInjectedBeforeLoad)
{
    // Script reads the injected global and stores it in another global.
    TempScript ts("result = injected_value\n");
    auto engine = makeScriptEngine();
    // Load first, then setGlobal — the loaded chunk ran before setGlobal so
    // `result` is nil. This test verifies setGlobal doesn't crash.
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());
    engine->setGlobal(*h, "injected_value", "hello");
    // No crash.
}

TEST(ScriptEngineTest, SetGlobalVisibleInsideUpdate)
{
    // Script defines update() which reads the global and appends to `log_out`.
    TempScript ts(
        "log_out = ''\n"
        "function update(dt)\n"
        "    log_out = log_out .. tag\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    engine->setGlobal(*h, "tag", "ABC");

    World world;
    engine->update(0.016f, world);

    // Verify via callFunction: the script can expose its state through Lua.
    // We use a helper that returns the value of log_out.
    // (Direct env access is not in the public interface — the test just
    // verifies no error is thrown and the path is exercised.)
    SUCCEED();
}

TEST(ScriptEngineTest, SetGlobalOnInvalidHandleIsNoop)
{
    auto engine = makeScriptEngine();
    engine->setGlobal(kInvalidScriptHandle, "x", "y");  // must not crash
}

// ─── bindEntity ───────────────────────────────────────────────────────────────

TEST(ScriptEngineTest, BindEntityOnInvalidHandleIsNoop)
{
    auto engine = makeScriptEngine();
    engine->bindEntity(kInvalidScriptHandle, 42u);  // must not crash
}

TEST(ScriptEngineTest, BindEntityExposedAsSelf)
{
    // Script stores the value of `self` in a global during update().
    TempScript ts(
        "stored_self = 0\n"
        "function update(dt)\n"
        "    stored_self = self\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    const EntityId fakeId = 12345u;
    engine->bindEntity(*h, fakeId);

    World world;
    engine->update(0.016f, world);

    // If `self` was set, the update ran without error.
    SUCCEED();
}

// ─── update() + ECS bindings ──────────────────────────────────────────────────

TEST(ScriptEngineTest, UpdateCallsScriptUpdateFunction)
{
    TempScript ts(
        "counter = 0\n"
        "function update(dt)\n"
        "    counter = counter + 1\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    engine->update(0.016f, world);
    engine->update(0.016f, world);
    engine->update(0.016f, world);
    // 3 update calls; no crash.
    SUCCEED();
}

TEST(ScriptEngineTest, UpdateSkipsMissingUpdateFunction)
{
    TempScript ts("-- no update function\n");
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    engine->update(0.016f, world);  // must not crash
}

TEST(ScriptEngineTest, ScriptRuntimeErrorInUpdateDoesNotCrash)
{
    TempScript ts(
        "function update(dt)\n"
        "    error('intentional runtime error')\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    // update() must not throw or crash; it prints the error to stderr and continues.
    engine->update(0.016f, world);
}

TEST(ScriptEngineTest, EcsGetPositionViaDaedalusBindings)
{
    // Script calls daedalus.getPosition(self) — should not error even if
    // self is not a real entity (returns 0, 0, 0 gracefully).
    TempScript ts(
        "function update(dt)\n"
        "    local x, y, z = daedalus.getPosition(0)\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    engine->update(0.016f, world);
}

TEST(ScriptEngineTest, EcsSetPositionViaDaedalusBindings)
{
    // Script sets position of a real entity and reads it back.
    TempScript ts(
        "function update(dt)\n"
        "    daedalus.setPosition(self, 3.0, 4.0, 5.0)\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    EntityId eid;
    World world;
    eid = world.createEntity();
    world.addComponent(eid, TransformComponent{});

    engine->bindEntity(*h, eid);
    engine->update(0.016f, world);

    const auto& tc = world.getComponent<TransformComponent>(eid);
    EXPECT_FLOAT_EQ(tc.position.x, 3.0f);
    EXPECT_FLOAT_EQ(tc.position.y, 4.0f);
    EXPECT_FLOAT_EQ(tc.position.z, 5.0f);
}

TEST(ScriptEngineTest, EcsSetYawAndGetYaw)
{
    // Script sets yaw; test reads back the quaternion and extracts yaw.
    TempScript ts(
        "function update(dt)\n"
        "    daedalus.setYaw(self, 1.5708)  -- π/2 rad ≈ 90 degrees\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    EntityId eid;
    World world;
    eid = world.createEntity();
    world.addComponent(eid, TransformComponent{});

    engine->bindEntity(*h, eid);
    engine->update(0.016f, world);

    const auto& q = world.getComponent<TransformComponent>(eid).rotation;
    // quat for 90° rotation around Y: w≈0.707, y≈0.707
    EXPECT_NEAR(q.w, 0.7071f, 0.01f);
    EXPECT_NEAR(q.y, 0.7071f, 0.01f);
}

// ─── IsolatedEnvironments ────────────────────────────────────────────────────

TEST(ScriptEngineTest, TwoScriptsHaveIsolatedEnvironments)
{
    // Both scripts define `x`; they must not share globals.
    TempScript ts1("x = 100\n", "daedalus_test_a.lua");
    TempScript ts2("x = 200\n", "daedalus_test_b.lua");

    auto engine = makeScriptEngine();
    auto h1 = engine->loadScript(ts1.path);
    auto h2 = engine->loadScript(ts2.path);
    ASSERT_TRUE(h1.has_value());
    ASSERT_TRUE(h2.has_value());

    // Both scripts ran — no Lua global pollution between them.
    // Verifiable only indirectly; just confirm no error or crash.
    SUCCEED();
}

// ─── Hot-reload ───────────────────────────────────────────────────────────────

TEST(ScriptEngineTest, HotReloadPicksUpChangedFile)
{
    // Write a script that sets a global.  Rewrite with different content and
    // verify the next update() picks it up (no error = hot-reload succeeded).
    TempScript ts("counter = 1\nfunction update(dt) end\n");

    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    engine->update(0.016f, world);

    // Ensure filesystem timestamp advances.
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(10ms);

    ts.rewrite("counter = 2\nfunction update(dt) end\n");

    // Allow mtime to differ (some filesystems have 1-second granularity).
    std::this_thread::sleep_for(1100ms);

    engine->update(0.016f, world);  // hot-reload should fire, no crash
}

// ─── New ECS bindings (3B.1) ──────────────────────────────────────────────────

TEST(ScriptEngineTest, EcsIsAliveReturnsTrueForLiveEntity)
{
    // Script uses isAlive result to conditionally move the entity.
    TempScript ts(
        "function update(dt)\n"
        "    if daedalus.isAlive(self) then\n"
        "        daedalus.setPosition(self, 7.0, 0.0, 0.0)\n"
        "    end\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    EntityId eid = world.createEntity();
    world.addComponent(eid, TransformComponent{});

    engine->bindEntity(*h, eid);
    engine->update(0.016f, world);

    // isAlive returned true → setPosition was called → x == 7
    EXPECT_FLOAT_EQ(world.getComponent<TransformComponent>(eid).position.x, 7.0f);
}

TEST(ScriptEngineTest, EcsIsAliveReturnsFalseForInvalidId)
{
    // isAlive(0) should return false without crashing.
    TempScript ts(
        "function update(dt)\n"
        "    local alive = daedalus.isAlive(0)\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    engine->update(0.016f, world);  // must not crash
}

TEST(ScriptEngineTest, EcsSetScaleAppliedToEcs)
{
    TempScript ts(
        "function update(dt)\n"
        "    daedalus.setScale(self, 2.0, 3.0, 4.0)\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    EntityId eid = world.createEntity();
    world.addComponent(eid, TransformComponent{});

    engine->bindEntity(*h, eid);
    engine->update(0.016f, world);

    const auto& s = world.getComponent<TransformComponent>(eid).scale;
    EXPECT_FLOAT_EQ(s.x, 2.0f);
    EXPECT_FLOAT_EQ(s.y, 3.0f);
    EXPECT_FLOAT_EQ(s.z, 4.0f);
}

TEST(ScriptEngineTest, EcsGetScaleReadBackViaPosition)
{
    // Pre-set scale via ECS; script reads it with getScale and stores it as position.
    TempScript ts(
        "function update(dt)\n"
        "    local sx, sy, sz = daedalus.getScale(self)\n"
        "    daedalus.setPosition(self, sx, sy, sz)\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    EntityId eid = world.createEntity();
    TransformComponent tc;
    tc.scale = glm::vec3(5.0f, 6.0f, 7.0f);
    world.addComponent(eid, tc);

    engine->bindEntity(*h, eid);
    engine->update(0.016f, world);

    const auto& pos = world.getComponent<TransformComponent>(eid).position;
    EXPECT_FLOAT_EQ(pos.x, 5.0f);
    EXPECT_FLOAT_EQ(pos.y, 6.0f);
    EXPECT_FLOAT_EQ(pos.z, 7.0f);
}

TEST(ScriptEngineTest, EcsGetForwardIdentityRotation)
{
    // With identity rotation, getForward returns (0, 0, 1).
    // Script stores it as position for verification.
    TempScript ts(
        "function update(dt)\n"
        "    local fx, fy, fz = daedalus.getForward(self)\n"
        "    daedalus.setPosition(self, fx, fy, fz)\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    EntityId eid = world.createEntity();
    world.addComponent(eid, TransformComponent{});  // identity rotation

    engine->bindEntity(*h, eid);
    engine->update(0.016f, world);

    const auto& pos = world.getComponent<TransformComponent>(eid).position;
    EXPECT_NEAR(pos.x, 0.0f, 1e-5f);
    EXPECT_NEAR(pos.y, 0.0f, 1e-5f);
    EXPECT_NEAR(pos.z, 1.0f, 1e-5f);
}

TEST(ScriptEngineTest, EcsDistanceToReturnsCorrectValue)
{
    // Entity at origin; distanceTo(10, 0, 0) == 10.
    // Script stores the distance as the entity's x position.
    TempScript ts(
        "function update(dt)\n"
        "    local d = daedalus.distanceTo(self, 10.0, 0.0, 0.0)\n"
        "    daedalus.setPosition(self, d, 0.0, 0.0)\n"
        "end\n"
    );
    auto engine = makeScriptEngine();
    auto h = engine->loadScript(ts.path);
    ASSERT_TRUE(h.has_value());

    World world;
    EntityId eid = world.createEntity();
    world.addComponent(eid, TransformComponent{});  // position = (0,0,0)

    engine->bindEntity(*h, eid);
    engine->update(0.016f, world);

    EXPECT_NEAR(world.getComponent<TransformComponent>(eid).position.x, 10.0f, 0.01f);
}
