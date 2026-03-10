// entity_def.h
// Editor-side entity descriptor.  Not stored in WorldMapData — lives on
// EditMapDocument directly.  At export time, EntityDef entries are converted
// to ECS entities with the appropriate render components by the game layer.

#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace daedalus::editor
{

// ─── EntityAlignment ─────────────────────────────────────────────────────────
/// How a placed entity snaps to geometry at placement and export time.

enum class EntityAlignment : uint32_t
{
    Floor   = 0,  ///< Bottom of entity sits on the sector floor (default).
    Ceiling = 1,  ///< Top of entity hangs from the sector ceiling.
    Wall    = 2,  ///< Entity face-aligns to the nearest wall normal.
    Free    = 3,  ///< No automatic snapping; position is exactly as authored.
};

// ─── EntityVisualType ─────────────────────────────────────────────────────────

enum class EntityVisualType : uint32_t
{
    BillboardCutout  = 0,  ///< Alpha-cutout billboard (writes to G-buffer via discard).
    BillboardBlended = 1,  ///< Alpha-blended billboard (transparent forward pass).
    AnimatedBillboard = 2, ///< Billboard with sprite-sheet frame animation.
    VoxelObject      = 3,  ///< MagicaVoxel .vox greedy-meshed object.
    StaticMesh       = 4,  ///< GLTF static mesh (first primitive).
    Decal            = 5,  ///< Projected deferred decal (OBB).
    ParticleEmitter  = 6,  ///< GPU particle emitter.
    RotatedSpriteSet = 7,  ///< Directional sprite: frame column selected by viewing angle (Build-style).
};

// ─── RotatedSpriteSettings ─────────────────────────────────────────────────────────
// Directional billboard: the atlas column is chosen from the horizontal angle
// between the viewer and the entity (Build engine convention).
// Rows advance independently as animation frames.

struct RotatedSpriteSettings
{
    uint32_t directionCount = 8;    ///< Number of view directions. Must be 8 or 16.
    uint32_t animRows       = 1;    ///< Number of animation rows in the atlas.
    uint32_t animCols       = 8;    ///< Number of columns per row (== directionCount).
    float    frameRate      = 8.0f; ///< Animation playback speed (frames per second).
};

/// Map a horizontal viewing angle (radians, any value) to a direction column
/// index in the range [0, directionCount).  Angle 0 is the entity's front face.
/// Pure function — safe to call from unit tests without GPU context.
[[nodiscard]] inline uint32_t rotatedSpriteFrameIndex(
    float    viewAngleRad,
    uint32_t directionCount) noexcept
{
    if (directionCount == 0u) return 0u;
    // Normalise angle to [0, 2π).
    constexpr float k_2pi = 6.2831853f;
    float a = viewAngleRad - k_2pi * std::floor(viewAngleRad / k_2pi);
    // Each sector spans 2π/N radians; offset by half a sector so the front
    // face is centred on column 0.
    const float sectorSize = k_2pi / static_cast<float>(directionCount);
    a += sectorSize * 0.5f;
    a -= k_2pi * std::floor(a / k_2pi);
    return static_cast<uint32_t>(a / sectorSize) % directionCount;
}

// ─── AnimSettings ──────────────────────────────────────────────────────────
// Sprite-sheet animation parameters (AnimatedBillboard only).

struct AnimSettings
{
    uint32_t frameCount = 1;      ///< Total number of frames in the atlas.
    uint32_t cols       = 1;      ///< Number of columns in the atlas grid.
    uint32_t rows       = 1;      ///< Number of rows in the atlas grid.
    float    frameRate  = 8.0f;   ///< Playback speed (frames per second).
};

// ─── DecalMaterialParams ─────────────────────────────────────────────────────
// Decal surface material parameters (Decal only).

struct DecalMaterialParams
{
    std::string normalPath;            ///< Optional tangent-space normal map path.
    float       roughness  = 0.5f;    ///< Roughness written into G-buffer RT1.b.
    float       metalness  = 0.0f;    ///< Metalness written into G-buffer RT1.a.
    float       opacity    = 1.0f;    ///< Global fade multiplier (0 = invisible, 1 = full).
};

// ─── ParticleEmitterParams ───────────────────────────────────────────────────
// Per-emitter authoring parameters (ParticleEmitter only).

struct ParticleEmitterParams
{
    float     emissionRate  = 10.0f;                          ///< Particles per second.
    glm::vec3 emitDir       = glm::vec3(0.0f, 1.0f, 0.0f);  ///< Central spawn direction (normalised).
    float     coneHalfAngle = glm::radians(15.0f);            ///< Velocity cone half-angle (radians).
    float     speedMin      = 1.0f;   ///< Minimum initial speed (m/s).
    float     speedMax      = 3.0f;   ///< Maximum initial speed (m/s).
    float     lifetimeMin   = 1.0f;   ///< Minimum particle lifetime (s).
    float     lifetimeMax   = 3.0f;   ///< Maximum particle lifetime (s).
    glm::vec4 colorStart    = glm::vec4(1.0f);                          ///< RGBA tint at birth.
    glm::vec4 colorEnd      = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);      ///< RGBA tint at death.
    float     sizeStart     = 0.1f;   ///< Billboard half-size at birth (metres).
    float     sizeEnd       = 0.05f;  ///< Billboard half-size at death (metres).
    float     drag          = 0.0f;   ///< Linear velocity damping coefficient (0 = none).
    glm::vec3 gravity       = glm::vec3(0.0f, -9.81f, 0.0f); ///< Per-emitter gravity vector.
};

// ─── EntityPhysicsProps ─────────────────────────────────────────────────────────
/// Physics simulation parameters.  Runtime integration provided by Phase 2 (Jolt).

enum class CollisionShape : uint32_t
{
    Box     = 0,  ///< Axis-aligned bounding box derived from scale.
    Sphere  = 1,  ///< Sphere with radius = max(scale) / 2.
    Capsule = 2,  ///< Capsule aligned to Y axis.
};

struct EntityPhysicsProps
{
    CollisionShape shape    = CollisionShape::Box;  ///< Collision geometry.
    bool           isStatic = true;                 ///< Static bodies do not respond to forces.
    float          mass     = 1.0f;                 ///< Dynamic body mass (kg).
};

// ─── EntityScriptProps ─────────────────────────────────────────────────────────
/// Script component parameters.  Runtime integration provided by Phase 2 (Lua).

struct EntityScriptProps
{
    std::string scriptPath;  ///< Path to the Lua behaviour script for this entity.

    /// Named string values injected as Lua globals before the first update() call.
    /// Keys must be valid Lua identifiers.
    std::unordered_map<std::string, std::string> exposedVars;
};

// ─── EntityAudioProps ──────────────────────────────────────────────────────────
/// Audio emitter parameters.  Runtime integration provided by Phase 2 (miniaudio).

struct EntityAudioProps
{
    std::string soundPath;                  ///< Path to the sound asset (ogg/wav).
    float       volume        = 1.0f;       ///< Per-emitter volume multiplier [0, 1].
    float       falloffRadius = 10.0f;      ///< Distance at which volume reaches zero (metres).
    bool        loop          = false;      ///< Whether the sound loops continuously.
    bool        autoPlay      = true;       ///< Start playing on first AudioSystem::update().
};

// ─── EntityDef ───────────────────────────────────────────────────────────────
// A single editor-placed entity.
// Position uses world-space coordinates (X = east, Y = up, Z = north).

struct EntityDef
{
    EntityVisualType visualType = EntityVisualType::BillboardCutout;

    // ── Transform ─────────────────────────────────────────────────────────
    glm::vec3 position = glm::vec3(0.0f);  ///< World-space position.
    float     yaw      = 0.0f;             ///< Y-axis rotation (radians).
    float     pitch    = 0.0f;             ///< X-axis rotation (radians).
    float     roll     = 0.0f;             ///< Z-axis rotation (radians).
    glm::vec3 scale    = glm::vec3(1.0f);  ///< Non-uniform scale.

    // ── Identity ───────────────────────────────────────────────────────────
    std::string     entityName;                                            ///< Optional human-readable label.
    EntityAlignment alignmentMode = EntityAlignment::Floor;               ///< Geometry snap behaviour.

    // ── Common visual ──────────────────────────────────────────────────────
    std::string assetPath;                        ///< Primary asset path (texture, .vox, or .gltf).
    glm::vec4   tint       = glm::vec4(1.0f);    ///< RGBA tint multiplier.
    uint32_t    layerIndex = 0;                   ///< Editor layer this entity belongs to.

    // ── Type-specific parameters ───────────────────────────────────────────────────
    AnimSettings          anim;          ///< Animation settings (AnimatedBillboard only).
    RotatedSpriteSettings rotatedSprite; ///< Directional sprite settings (RotatedSpriteSet only).
    DecalMaterialParams   decalMat;      ///< Decal material params (Decal only).
    ParticleEmitterParams particle;      ///< Particle emitter params (ParticleEmitter only).

    // ── Component stubs (data only; runtime wired in Phase 2) ─────────────────
    EntityPhysicsProps physics;  ///< Physics simulation parameters.
    EntityScriptProps  script;   ///< Lua behaviour script parameters.
    EntityAudioProps   audio;    ///< Audio emitter parameters.
};

} // namespace daedalus::editor
