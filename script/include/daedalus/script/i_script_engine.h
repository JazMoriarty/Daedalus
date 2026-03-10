// i_script_engine.h
// Pure interface for the Daedalus scripting subsystem.
//
// DaedalusScript wraps Lua 5.4 + sol2 behind this interface so that no Lua
// or sol2 types cross the module boundary. All higher-level code (app, game
// systems) depends only on this header.
//
// Responsibilities:
//   loadScript       — compile a Lua script file into an isolated environment
//   unloadScript     — release an environment and its script association
//   bindEntity       — expose an EntityId to a script as `self`
//   setGlobal        — inject a string value into a script environment
//   callFunction     — invoke a named Lua function in a script environment
//   update           — call update(dt) on all loaded scripts; poll for hot-reload
//
// Error handling: fallible operations return std::expected<T, ScriptErrorInfo>.
// Infallible operations (unloadScript, bindEntity, setGlobal, update) do not.

#pragma once

#include "daedalus/script/script_types.h"
#include "daedalus/core/ecs/world.h"
#include "daedalus/core/types.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace daedalus::script
{

// ─── IScriptEngine ────────────────────────────────────────────────────────────

class IScriptEngine
{
public:
    virtual ~IScriptEngine() = default;

    IScriptEngine(const IScriptEngine&)            = delete;
    IScriptEngine& operator=(const IScriptEngine&) = delete;

    // ─── Script lifecycle ─────────────────────────────────────────────────────

    /// Compile a Lua script file and create an isolated environment for it.
    ///
    /// Each call to loadScript() creates a fresh sol::environment with a
    /// private global table. The same file can be loaded multiple times to
    /// give multiple entities independent environments.
    ///
    /// Hot-reload: if the file's last-write-time changes between update() calls
    /// the environment is transparently re-compiled in place. Globals set via
    /// setGlobal() are NOT re-applied on hot-reload (reload resets them to nil).
    ///
    /// @param path  Absolute or relative path to the .lua source file.
    /// @return      A valid ScriptHandle, or ScriptError::FileNotFound /
    ///              ScriptError::CompileError on failure.
    [[nodiscard]] virtual std::expected<ScriptHandle, ScriptErrorInfo>
    loadScript(const std::filesystem::path& path) = 0;

    /// Release the Lua environment associated with the handle.
    ///
    /// No-op if handle is kInvalidScriptHandle or already unloaded.
    /// The script is removed from the update loop immediately.
    ///
    /// @param handle  Handle returned by loadScript().
    virtual void unloadScript(ScriptHandle handle) = 0;

    // ─── Per-script configuration ─────────────────────────────────────────────

    /// Expose an EntityId to a script as the global variable `self`.
    ///
    /// The id is set as a Lua integer in the script's environment so that
    /// ECS binding functions (daedalus.getPosition, etc.) can accept it.
    ///
    /// No-op if handle is invalid.
    ///
    /// @param handle  Handle returned by loadScript().
    /// @param id      ECS EntityId to bind.
    virtual void bindEntity(ScriptHandle handle, EntityId id) = 0;

    /// Set a named string global in a script's environment.
    ///
    /// Useful for injecting configuration values (e.g. asset paths, tags)
    /// that the script can read at runtime. The value is accessible in Lua
    /// as a plain string global with the given name.
    ///
    /// No-op if handle is invalid.
    ///
    /// @param handle  Handle returned by loadScript().
    /// @param name    Global variable name (must be a valid Lua identifier).
    /// @param value   String value to assign.
    virtual void setGlobal(ScriptHandle    handle,
                           const std::string& name,
                           const std::string& value) = 0;

    // ─── Invocation ───────────────────────────────────────────────────────────

    /// Call a named global function in a script's environment.
    ///
    /// The function is invoked via sol::protected_function; any Lua error
    /// is captured and returned as ScriptError::RuntimeError without
    /// propagating a C++ exception.
    ///
    /// @param handle  Handle returned by loadScript().
    /// @param name    Name of the Lua global function to call.
    /// @return        ScriptErrorInfo on runtime error or invalid handle.
    virtual std::expected<void, ScriptErrorInfo>
    callFunction(ScriptHandle handle, const std::string& name) = 0;

    // ─── Per-frame ────────────────────────────────────────────────────────────

    /// Advance all loaded scripts by `dt` seconds.
    ///
    /// For each script: if the file has been modified since its last load
    /// time, it is recompiled in place (hot-reload).  Then, if the script
    /// defines a global function named `update`, it is called with `dt`.
    ///
    /// Errors from update() or hot-reload are printed to stderr and do not
    /// propagate; a faulty script is silently skipped that frame.
    ///
    /// @param dt     Delta time in seconds since the last update() call.
    /// @param world  ECS world passed to registered binding functions.
    virtual void update(float dt, daedalus::World& world) = 0;

protected:
    IScriptEngine() = default;
};

// ─── Factory ──────────────────────────────────────────────────────────────────

/// Construct the sol2-backed scripting engine.
///
/// Initialises a single Lua 5.4 VM and registers all daedalus.* ECS
/// binding functions.
///
/// @return  A fully initialised IScriptEngine.
[[nodiscard]] std::unique_ptr<IScriptEngine> makeScriptEngine();

} // namespace daedalus::script
