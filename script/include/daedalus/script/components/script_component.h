// script_component.h
// ECS component that associates a Lua script with an entity.
//
// The ScriptComponent is data-only; it carries the path to the script file
// and a map of named string variables to inject into the script environment
// at load time.  No sol2 or Lua types appear here, keeping the component safe
// to serialise from the .dlevel format and usable outside DaedalusScript.
//
// Lifecycle:
//   DaedalusApp reads ScriptComponents from the level pack, calls
//   IScriptEngine::loadScript(scriptPath), IScriptEngine::bindEntity(handle, id),
//   and IScriptEngine::setGlobal(handle, k, v) for each entry in exposedVars.
//   The handle is tracked externally; this component holds no runtime state.

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace daedalus::script
{

// ─── ScriptComponent ──────────────────────────────────────────────────────────

struct ScriptComponent
{
    /// Path to the Lua source file (.lua).  Resolved at runtime by the app.
    std::filesystem::path scriptPath;

    /// Named string values injected into the script's global environment
    /// before the first update() call.  Keys must be valid Lua identifiers.
    std::unordered_map<std::string, std::string> exposedVars;
};

} // namespace daedalus::script
