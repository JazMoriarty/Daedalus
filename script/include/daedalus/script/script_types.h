// script_types.h
// Plain data types shared across the DaedalusScript public interface.
//
// No sol2 or Lua types appear here. Consumers of DaedalusScript see only
// engine-native types — Lua internals stay private to script/src/.

#pragma once

#include "daedalus/core/types.h"

#include <string>

namespace daedalus::script
{

// ─── ScriptError ──────────────────────────────────────────────────────────────

enum class ScriptError : u32
{
    FileNotFound,   ///< The script file does not exist or cannot be opened.
    CompileError,   ///< The Lua source failed to compile (syntax error).
    RuntimeError,   ///< A Lua runtime error occurred during execution.
    InvalidHandle,  ///< The supplied ScriptHandle is not registered.
};

// ─── ScriptErrorInfo ──────────────────────────────────────────────────────────
// Pairs an error code with a human-readable diagnostic message from the Lua VM.

struct ScriptErrorInfo
{
    ScriptError code    = ScriptError::FileNotFound;
    std::string message;  ///< Lua error string, or empty if not applicable.
};

// ─── ScriptHandle ─────────────────────────────────────────────────────────────
// Opaque handle returned by IScriptEngine::loadScript().
// Associates a script file with a per-entity Lua environment.
// Value 0 is always invalid.

enum class ScriptHandle : u32 {};

inline constexpr ScriptHandle kInvalidScriptHandle { 0u };

[[nodiscard]] inline bool isValid(ScriptHandle h) noexcept
{
    return static_cast<u32>(h) != 0u;
}

} // namespace daedalus::script
