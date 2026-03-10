// sol_script_engine.h  (private — never included from script/include/)
// sol2-backed implementation of IScriptEngine.
//
// All sol2 and Lua types are confined to this header and its corresponding
// .cpp.  No Lua symbols cross the module boundary.

#pragma once

#include "daedalus/script/i_script_engine.h"

#include <sol/sol.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace daedalus::script
{

// ─── SolScriptEngine ──────────────────────────────────────────────────────────

class SolScriptEngine final : public IScriptEngine
{
public:
    SolScriptEngine();
    ~SolScriptEngine() override = default;

    // ─── IScriptEngine ────────────────────────────────────────────────────────

    [[nodiscard]] std::expected<ScriptHandle, ScriptErrorInfo>
    loadScript(const std::filesystem::path& path) override;

    void unloadScript(ScriptHandle handle) override;

    void bindEntity(ScriptHandle handle, EntityId id) override;

    void setGlobal(ScriptHandle       handle,
                   const std::string& name,
                   const std::string& value) override;

    [[nodiscard]] std::expected<void, ScriptErrorInfo>
    callFunction(ScriptHandle handle, const std::string& name) override;

    void update(float dt, daedalus::World& world) override;

private:
    // ── Lua VM ────────────────────────────────────────────────────────────────

    sol::state m_lua;

    // Pointer-to-pointer used by ECS bindings so they always target the
    // current World without reregistering functions each frame.
    daedalus::World* m_currentWorld = nullptr;

    // ── Handle counter ────────────────────────────────────────────────────────

    u32 m_nextHandleId = 1u;

    // ── Per-script record ─────────────────────────────────────────────────────

    struct ScriptRecord
    {
        std::filesystem::path   path;
        sol::environment        env;
        std::filesystem::file_time_type lastWriteTime;
    };

    std::unordered_map<u32, ScriptRecord> m_scripts;

    // ── Private helpers ───────────────────────────────────────────────────────

    /// Compile and execute the Lua source file into `env`.
    ///
    /// @param path  Source file path.
    /// @param env   Target isolated environment.
    /// @return      ScriptErrorInfo on compile/runtime error.
    [[nodiscard]] std::expected<void, ScriptErrorInfo>
    compileInto(const std::filesystem::path& path, sol::environment& env);

    /// Look up a record by handle. Returns nullptr if not found.
    [[nodiscard]] ScriptRecord* find(ScriptHandle handle);
    [[nodiscard]] const ScriptRecord* find(ScriptHandle handle) const;
};

} // namespace daedalus::script
