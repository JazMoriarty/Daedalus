// sol_script_engine.cpp
// sol2-backed IScriptEngine implementation.
//
// One sol::state owns the Lua 5.4 VM for the whole engine lifetime.
// Each loadScript() creates a fresh sol::environment (a private global table)
// so scripts cannot read each other's globals.
//
// Hot-reload: update() checks last_write_time for every registered script each
// frame. If the file has been modified, compileInto() replaces the environment
// contents in place. The per-entity bindings (self, etc.) set before the reload
// are reset — the caller may re-apply them via bindEntity()/setGlobal() if
// needed.

#include "sol_script_engine.h"
#include "lua_ecs_bindings.h"

#include <cstdio>
#include <vector>

namespace daedalus::script
{

// ─── Constructor ──────────────────────────────────────────────────────────────

SolScriptEngine::SolScriptEngine()
{
    // Open standard libraries (base, math, string, table, io, etc.).
    m_lua.open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table,
        sol::lib::io,
        sol::lib::os,
        sol::lib::package
    );

    // Register daedalus.* ECS bindings. The worldPtr indirection allows
    // update() to set m_currentWorld once per frame without re-registering.
    registerEcsBindings(m_lua, &m_currentWorld);
}

// ─── compileInto (private) ────────────────────────────────────────────────────

std::expected<void, ScriptErrorInfo>
SolScriptEngine::compileInto(const std::filesystem::path& path,
                              sol::environment&             env)
{
    // Load the file as a Lua chunk, then execute it inside the environment.
    auto loadResult = m_lua.load_file(path.string());
    if (!loadResult.valid())
    {
        const sol::error err = loadResult;
        return std::unexpected(ScriptErrorInfo{
            ScriptError::CompileError,
            err.what()
        });
    }

    sol::protected_function chunk = loadResult;
    sol::set_environment(env, chunk);     // free function — not a member

    const sol::protected_function_result execResult = chunk();
    if (!execResult.valid())
    {
        const sol::error err = execResult;
        return std::unexpected(ScriptErrorInfo{
            ScriptError::RuntimeError,
            err.what()
        });
    }

    return {};
}

// ─── find (private) ───────────────────────────────────────────────────────────

SolScriptEngine::ScriptRecord* SolScriptEngine::find(ScriptHandle handle)
{
    const u32 id = static_cast<u32>(handle);
    const auto it = m_scripts.find(id);
    return (it != m_scripts.end()) ? &it->second : nullptr;
}

const SolScriptEngine::ScriptRecord* SolScriptEngine::find(ScriptHandle handle) const
{
    const u32 id = static_cast<u32>(handle);
    const auto it = m_scripts.find(id);
    return (it != m_scripts.end()) ? &it->second : nullptr;
}

// ─── loadScript ───────────────────────────────────────────────────────────────

std::expected<ScriptHandle, ScriptErrorInfo>
SolScriptEngine::loadScript(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        return std::unexpected(ScriptErrorInfo{
            ScriptError::FileNotFound,
            path.string() + ": file not found"
        });
    }

    // Create an isolated environment parented to the global table so standard
    // library functions (math, print, …) are accessible by default.
    sol::environment env(m_lua, sol::create, m_lua.globals());

    auto compResult = compileInto(path, env);
    if (!compResult.has_value())
    {
        return std::unexpected(compResult.error());
    }

    const std::filesystem::file_time_type mtime =
        std::filesystem::last_write_time(path);

    const u32 id = m_nextHandleId++;
    m_scripts.emplace(id, ScriptRecord{ path, std::move(env), mtime });

    return static_cast<ScriptHandle>(id);
}

// ─── unloadScript ─────────────────────────────────────────────────────────────

void SolScriptEngine::unloadScript(ScriptHandle handle)
{
    const u32 id = static_cast<u32>(handle);
    m_scripts.erase(id);
}

// ─── bindEntity ───────────────────────────────────────────────────────────────

void SolScriptEngine::bindEntity(ScriptHandle handle, EntityId id)
{
    ScriptRecord* rec = find(handle);
    if (!rec) { return; }

    // Expose the entity id as `self` in the script's environment (Lua integer).
    rec->env["self"] = static_cast<lua_Integer>(id);
}

// ─── setGlobal ────────────────────────────────────────────────────────────────

void SolScriptEngine::setGlobal(ScriptHandle       handle,
                                  const std::string& name,
                                  const std::string& value)
{
    ScriptRecord* rec = find(handle);
    if (!rec) { return; }

    rec->env[name] = value;
}

// ─── callFunction ─────────────────────────────────────────────────────────────

std::expected<void, ScriptErrorInfo>
SolScriptEngine::callFunction(ScriptHandle handle, const std::string& name)
{
    ScriptRecord* rec = find(handle);
    if (!rec)
    {
        return std::unexpected(ScriptErrorInfo{
            ScriptError::InvalidHandle,
            "callFunction: invalid ScriptHandle"
        });
    }

    sol::protected_function fn = rec->env[name];
    if (!fn.valid())
    {
        return std::unexpected(ScriptErrorInfo{
            ScriptError::RuntimeError,
            "callFunction: function '" + name + "' not found in script"
        });
    }

    const sol::protected_function_result result = fn();
    if (!result.valid())
    {
        const sol::error err = result;
        return std::unexpected(ScriptErrorInfo{
            ScriptError::RuntimeError,
            err.what()
        });
    }

    return {};
}

// ─── update ───────────────────────────────────────────────────────────────────

void SolScriptEngine::update(float dt, daedalus::World& world)
{
    // Point the ECS binding indirection at this frame's world.
    m_currentWorld = &world;

    for (auto& [id, rec] : m_scripts)
    {
        // ── Hot-reload: check if the file has changed ──────────────────────
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(rec.path, ec);

        if (!ec && mtime != rec.lastWriteTime)
        {
            // Recreate a fresh environment and recompile.
            sol::environment newEnv(m_lua, sol::create, m_lua.globals());
            auto res = compileInto(rec.path, newEnv);

            if (res.has_value())
            {
                rec.env           = std::move(newEnv);
                rec.lastWriteTime = mtime;
                std::printf("[Script] Hot-reloaded: %s\n", rec.path.string().c_str());
            }
            else
            {
                std::fprintf(stderr,
                    "[Script] Hot-reload compile error in %s: %s\n",
                    rec.path.string().c_str(),
                    res.error().message.c_str());
                // Keep the old environment running — don't update lastWriteTime
                // so we retry on the next file modification.
            }
        }

        // ── Call update(dt) if the script defines it ───────────────────────
        sol::protected_function fn = rec.env["update"];
        if (!fn.valid()) { continue; }

        const sol::protected_function_result result = fn(dt);
        if (!result.valid())
        {
            const sol::error err = result;
            std::fprintf(stderr,
                "[Script] Runtime error in %s: %s\n",
                rec.path.string().c_str(),
                err.what());
            // Continue with other scripts; do not propagate.
        }
    }

    // Clear the world pointer after the frame to prevent dangling access.
    m_currentWorld = nullptr;
}

// ─── Factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<IScriptEngine> makeScriptEngine()
{
    return std::make_unique<SolScriptEngine>();
}

} // namespace daedalus::script
