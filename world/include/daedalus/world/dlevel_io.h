// dlevel_io.h
// Serialisation / deserialisation for the .dlevel compiled level pack.
//
// .dlevel is the only level format loaded by DaedalusApp at runtime.
// It bundles all data the runtime needs in one binary file:
//   - Sector geometry and material UUID references (WorldMapData)
//   - Optional player start position and yaw
//   - Light definitions (point and spot)
//   - Sun directional light settings
//   - Compiled binary textures keyed by UUID (RGBA8Unorm)
//
// The editor's source formats (.emap, .dmap) are never read by DaedalusApp.
// The compile step (F5 in DaedalusEdit or explicit export) produces a .dlevel
// from editor document data and asset root textures.
//
// Error handling: all operations return std::expected — errors are never
// silently swallowed. See DlevelError for typed failure reasons.

#pragma once

#include "daedalus/world/map_data.h"
#include "daedalus/core/types.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace daedalus::world
{

// ─── LevelLightType ───────────────────────────────────────────────────────────

enum class LevelLightType : u32
{
    Point = 0,  ///< Omnidirectional point light.
    Spot  = 1,  ///< Cone spotlight.
};

// ─── LevelLight ───────────────────────────────────────────────────────────────
// A single runtime light entry.
// Spot-specific fields (direction, cone angles, range) are ignored when
// type == LevelLightType::Point.

struct LevelLight
{
    LevelLightType type          = LevelLightType::Point;
    glm::vec3      position      = glm::vec3(0.0f, 2.0f, 0.0f);
    glm::vec3      color         = glm::vec3(1.0f, 0.94f, 0.82f);
    float          radius        = 10.0f;    ///< Point falloff radius (metres).
    float          intensity     = 2.0f;

    // Spot-only fields (ignored for Point):
    glm::vec3 direction      = glm::vec3(0.0f, -1.0f, 0.0f);  ///< Normalised cone axis.
    float     innerConeAngle = 0.2618f;  ///< ~15 degrees (radians), full brightness inside.
    float     outerConeAngle = 0.5236f;  ///< ~30 degrees (radians), falloff to zero at edge.
    float     range          = 20.0f;    ///< Cone influence range (metres).
};

// ─── LevelSunSettings ─────────────────────────────────────────────────────────
// Directional (sun/sky) light parameters for the level.

struct LevelSunSettings
{
    glm::vec3 direction = glm::vec3(0.371f, 0.928f, 0.278f);  ///< Towards-light, normalised.
    glm::vec3 color     = glm::vec3(1.0f, 0.95f, 0.8f);
    float     intensity = 0.0f;  ///< 0 = interior default (no direct sunlight).
};

// ─── LevelPlayerStart ─────────────────────────────────────────────────────────
// Initial player spawn transform. One per level.

struct LevelPlayerStart
{
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
    float     yaw      = 0.0f;  ///< Radians; 0 = looking toward +Z.
};

// ─── LevelEntityVisualType ───────────────────────────────────────────
// Ordinals match editor::EntityVisualType exactly so app code may cast directly.

enum class LevelEntityVisualType : u32
{
    BillboardCutout   = 0,  ///< Alpha-cutout billboard (G-buffer, hard discard).
    BillboardBlended  = 1,  ///< Alpha-blended billboard (transparent forward pass).
    AnimatedBillboard = 2,  ///< Billboard with sprite-sheet frame animation.
    VoxelObject       = 3,  ///< MagicaVoxel .vox greedy-meshed object.
    StaticMesh        = 4,  ///< GLTF static mesh (first primitive).
    Decal             = 5,  ///< Projected deferred decal (OBB).
    ParticleEmitter   = 6,  ///< GPU particle emitter.
    RotatedSpriteSet  = 7,  ///< Directional sprite: column selected by view angle.
    None              = 0xFFu,  ///< No visual component; entity is physics/script-only.
};

// ─── LevelCollisionShape ──────────────────────────────────────────────
// Mirrors physics::CollisionShape (same ordinal values) without creating a
// dependency from world/ on physics/. App code casts between them directly.

enum class LevelCollisionShape : u32
{
    None       = 0,  ///< No collision; entity is decorative.
    Box        = 1,  ///< Axis-aligned box. halfExtents carries the half-extents.
    Capsule    = 2,  ///< Vertical capsule. halfExtents.x = radius, .y = halfHeight.
    ConvexHull = 3,  ///< Reserved for future use.
};

// ─── LevelEntity ──────────────────────────────────────────────────────────────
// A single placed entity baked into the level pack.
// Carries just enough data for the runtime to register collision bodies and
// seed ECS transforms on level load. Visual mesh / material data is not stored
// here (those arrive via the ECS scene graph from the editor, out of scope for
// the Phase 2A binary format).

struct LevelEntity
{
    std::string         name;                               ///< Editor-assigned UTF-8 name (≤ 255 bytes).
    glm::vec3           position    = glm::vec3(0.0f);     ///< World-space position (feet for characters).
    float               yaw         = 0.0f;                ///< Spawn yaw (radians; 0 = facing +Z).
    u32                 sectorId    = 0xFFFFFFFFu;         ///< Containing sector, or INVALID_SECTOR_ID.

    // Physics descriptor
    LevelCollisionShape shape       = LevelCollisionShape::None;
    bool                dynamic     = false;               ///< false = static, true = dynamic rigid body.
    float               mass        = 1.0f;                ///< kg; ignored for static bodies.
    /// Box: full half-extents (x,y,z).
    /// Capsule: x = radius, y = halfHeight, z = 0 (ignored).
    glm::vec3           halfExtents = glm::vec3(0.5f, 0.5f, 0.5f);

    // Script descriptor (v3+)
    std::string                                  scriptPath;   ///< Lua source file path; empty = no script.
    std::unordered_map<std::string, std::string> exposedVars;  ///< Globals injected before first update().

    // Audio emitter descriptor (v3+)
    std::string soundPath;                   ///< Sound asset path; empty = no audio emitter.
    float       soundFalloffRadius = 10.0f;  ///< World units at which volume reaches silence.
    float       soundVolume        = 1.0f;   ///< Per-emitter volume multiplier [0, 1].
    bool        soundLoop          = false;  ///< Loop the sound continuously.
    bool        soundAutoPlay      = true;   ///< Start on first AudioSystem::update().

    // Visual descriptor (v4+)
    LevelEntityVisualType visualType = LevelEntityVisualType::None;

    // Common visual
    std::string assetPath;                         ///< Primary asset (texture, .vox, or .gltf).
    glm::vec4   tint       = glm::vec4(1.0f);     ///< RGBA tint multiplier.
    glm::vec3   visualScale = glm::vec3(1.0f);    ///< World-space scale (for mesh/decal/voxel).
    float       visualPitch = 0.0f;               ///< X-axis rotation (radians).
    float       visualRoll  = 0.0f;               ///< Z-axis rotation (radians).

    // Animated billboard / RotatedSpriteSet
    u32   animFrameCount  = 1;     ///< Number of columns in the atlas (frames per row).
    u32   animCols        = 1;     ///< Atlas column count (== animFrameCount for anim types).
    u32   animRows        = 1;     ///< Atlas row count.
    float animFrameRate   = 8.0f;  ///< Playback speed (fps).
    u32   rotatedSpriteDirCount = 8;  ///< View direction count for RotatedSpriteSet (8 or 16).

    // Decal material params
    std::string decalNormalPath;           ///< Optional tangent-space normal map path.
    float       decalRoughness = 0.5f;    ///< Roughness written to G-buffer RT1.b.
    float       decalMetalness = 0.0f;    ///< Metalness written to G-buffer RT1.a.
    float       decalOpacity   = 1.0f;    ///< Global fade multiplier.

    // Particle emitter params
    float     particleEmissionRate  = 10.0f;
    glm::vec3 particleEmitDir       = glm::vec3(0.0f, 1.0f, 0.0f);
    float     particleConeHalfAngle = glm::pi<float>() / 12.0f;  ///< 15 degrees
    float     particleSpeedMin      = 1.0f;
    float     particleSpeedMax      = 3.0f;
    float     particleLifetimeMin   = 1.0f;
    float     particleLifetimeMax   = 3.0f;
    glm::vec4 particleColorStart    = glm::vec4(1.0f);
    glm::vec4 particleColorEnd      = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    float     particleSizeStart     = 0.1f;
    float     particleSizeEnd       = 0.05f;
    float     particleDrag          = 0.0f;
    glm::vec3 particleGravity       = glm::vec3(0.0f, -9.81f, 0.0f);
};

// ─── LevelTexture ─────────────────────────────────────────────────────────────
// A single compiled texture entry. Pixels are RGBA8Unorm (4 bytes per pixel,
// row-major, top-left origin). Keyed by material UUID in LevelPackData.

struct LevelTexture
{
    u32             width  = 0;
    u32             height = 0;
    std::vector<u8> pixels;  ///< RGBA8Unorm, width * height * 4 bytes.
};

// ─── LevelPackData ────────────────────────────────────────────────────────────
// The complete in-memory representation of a .dlevel file.
// This is what DaedalusApp receives after loadDlevel() and uses to build the
// GPU scene: tessellate map, upload textures, instantiate lights, place player.

struct LevelPackData
{
    WorldMapData                                     map;
    std::optional<LevelPlayerStart>                  playerStart;
    LevelSunSettings                                 sun;
    std::vector<LevelLight>                          lights;

    /// UUID-keyed compiled textures. Keyed by the material UUID referenced in
    /// map surfaces (Wall::frontMaterialId, Sector::floorMaterialId, etc.).
    /// Null-UUID entries are never stored. Missing UUIDs render with the
    /// engine's fallback white texture.
    std::unordered_map<UUID, LevelTexture, UUIDHash> textures;

    /// Placed entities with physics descriptors. Empty in v1 files.
    std::vector<LevelEntity>                         entities;
};

// ─── DlevelError ─────────────────────────────────────────────────────────────

enum class DlevelError : u32
{
    FileNotFound,    ///< The specified path does not exist or cannot be opened.
    WriteError,      ///< Failed to write to the destination path.
    ParseError,      ///< Data is corrupt or does not match the expected format.
    VersionMismatch, ///< Binary format version is newer than this build supports.
};

// ─── I/O ──────────────────────────────────────────────────────────────────────

/// Deserialise a LevelPackData from a binary .dlevel file.
///
/// @param path  Absolute or relative path to the .dlevel file.
/// @return      The loaded level pack, or a DlevelError.
[[nodiscard]] std::expected<LevelPackData, DlevelError>
loadDlevel(const std::filesystem::path& path);

/// Serialise a LevelPackData to a binary .dlevel file.
///
/// @param pack  The level pack data to write.
/// @param path  Destination path. The file is created or overwritten.
/// @return      void on success, or a DlevelError.
[[nodiscard]] std::expected<void, DlevelError>
saveDlevel(const LevelPackData& pack, const std::filesystem::path& path);

} // namespace daedalus::world
