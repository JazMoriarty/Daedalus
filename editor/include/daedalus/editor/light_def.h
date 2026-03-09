// light_def.h
// Editor-side light descriptor.  Not stored in WorldMapData — lives on
// EditMapDocument directly.  At export time, LightDef entries are converted
// to ECS entities with the appropriate light components by the game layer.

#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace daedalus::editor
{

enum class LightType : uint32_t
{
    Point = 0,  ///< Omnidirectional point light.
    Spot  = 1,  ///< Cone spotlight.
};

/// A single editor-placed light.
/// Position uses world-space coordinates (X = east, Y = up, Z = north).
struct LightDef
{
    LightType type      = LightType::Point;
    glm::vec3 position  = glm::vec3(0.0f, 2.0f, 0.0f);  ///< Default: 2 m above floor.
    glm::vec3 color     = glm::vec3(1.0f, 0.94f, 0.82f); ///< Warm white.
    float     radius    = 10.0f;   ///< Influence radius / point falloff (metres).
    float     intensity = 2.0f;    ///< Light intensity multiplier.

    // ── Spot-specific fields (ignored when type == Point) ──────────────────
    glm::vec3 direction      = glm::vec3(0.0f, -1.0f, 0.0f);  ///< Normalised cone axis (world space).
    float     innerConeAngle = glm::radians(15.0f);             ///< Inner half-angle (radians) — full brightness inside.
    float     outerConeAngle = glm::radians(30.0f);             ///< Outer half-angle (radians) — falloff to zero at edge.
    float     range          = 20.0f;                           ///< Cone influence range (metres).
};

} // namespace daedalus::editor
