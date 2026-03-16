// dlevel_binary.cpp
// Binary .dlevel v5 serialisation / deserialisation.
//
// ─── Format specification (version 5) ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
//
// All multi-byte integers are little-endian.
//
// Header (28 bytes):
//   [4]  magic          u32 = 0x4C564C44  ('D','L','V','L' as little-endian)
//   [4]  version        u32 = 5  (exact match required)
//   [4]  sectorCount    u32
//   [4]  textureCount   u32
//   [4]  lightCount     u32
//   [4]  hasPlayerStart u32  (0 = absent, 1 = present)
//   [4]  entityCount    u32
//
// Map meta (variable):
//   [2]  nameLen        u16  (byte length of name string)
//   [nameLen] name      UTF-8
//   [2]  authorLen      u16
//   [authorLen] author  UTF-8
//   [12] globalAmbientColor       f32[3]
//   [4]  globalAmbientIntensity   f32
//
// Per sector (repeated sectorCount times) — identical layout to .dmap v1:
//   [4]  wallCount       u32
//   [4]  flags           u32  (SectorFlags)
//   [4]  floorHeight     f32
//   [4]  ceilHeight      f32
//   [16] floorMaterialId UUID (u64 hi, u64 lo)
//   [16] ceilMaterialId  UUID
//   [12] ambientColor    f32[3]
//   [4]  ambientIntensity f32
//   --- total sector header: 60 bytes ---
//
//   Per wall (repeated wallCount times):
//     [8]  p0              f32[2]
//     [4]  flags           u32  (WallFlags)
//     [4]  portalSectorId  u32
//     [16] frontMaterialId UUID
//     [16] upperMaterialId UUID
//     [16] lowerMaterialId UUID
//     [8]  uvOffset        f32[2]
//     [8]  uvScale         f32[2]
//     [4]  uvRotation      f32
//     --- total per wall: 84 bytes ---
//
// Textures (textureCount entries):
//   [8]  uuid_hi        u64
//   [8]  uuid_lo        u64
//   [4]  width          u32
//   [4]  height         u32
//   [4]  dataSize       u32  (= width * height * 4)
//   [dataSize] pixels   u8[] (RGBA8Unorm, row-major, top-left origin)
//
// Sun settings (28 bytes, always present):
//   [12] direction       f32[3]  (normalised, towards-light)
//   [12] color           f32[3]
//   [4]  intensity       f32
//
// Lights (lightCount entries, 60 bytes each):
//   [4]  type            u32   (0 = Point, 1 = Spot)
//   [12] position        f32[3]
//   [12] color           f32[3]
//   [4]  radius          f32
//   [4]  intensity       f32
//   [12] direction       f32[3]  (Spot only; written but ignored for Point)
//   [4]  innerConeAngle  f32
//   [4]  outerConeAngle  f32
//   [4]  range           f32
//
// Player start (16 bytes; present only when hasPlayerStart == 1):
//   [12] position        f32[3]
//   [4]  yaw             f32    (radians; 0 = towards +Z)
//
// Entities (entityCount entries; present only in version >= 2):
//   Per entity (variable length due to name and string fields):
//   [2]  nameLen         u16
//   [nameLen] name       UTF-8  (editor-assigned name, ≤ 255 bytes)
//   [12] position        f32[3]
//   [4]  yaw             f32    (spawn yaw, radians)
//   [4]  sectorId        u32
//   [4]  shape           u32    (LevelCollisionShape: None=0 Box=1 Capsule=2 ConvexHull=3)
//   [4]  dynamic         u32    (0 = static, 1 = dynamic rigid body)
//   [4]  mass            f32
//   [12] halfExtents     f32[3] (Box: half-extents; Capsule: {radius, halfHeight, 0})
//
//   --- Script descriptor (v3) ---
//   [2]  scriptPathLen   u16
//   [scriptPathLen] scriptPath UTF-8
//   [4]  exposedVarCount u32
//   Per exposed var (repeated exposedVarCount times):
//     [2]  keyLen   u16
//     [keyLen] key  UTF-8
//     [2]  valueLen u16
//     [valueLen] value UTF-8
//
//   --- Audio descriptor ---
//   [2]  soundPathLen    u16
//   [soundPathLen] soundPath UTF-8
//   [4]  soundFalloffRadius f32
//   [4]  soundVolume        f32
//   [4]  soundLoop          u32  (0 = false, 1 = true)
//   [4]  soundAutoPlay      u32  (0 = false, 1 = true)
//
//   --- Visual descriptor (v4) ---
//   [4]  visualType      u32  (LevelEntityVisualType; 0xFF = None)
//   [2]  assetPathLen    u16
//   [assetPathLen] assetPath UTF-8
//   [16] tint            f32[4]
//   [12] visualScale     f32[3]
//   [4]  visualPitch     f32
//   [4]  visualRoll      f32
//   [4]  animFrameCount  u32
//   [4]  animCols        u32
//   [4]  animRows        u32
//   [4]  animFrameRate   f32
//   [4]  rotatedSpriteDirCount u32
//   [2]  decalNormalPathLen u16
//   [decalNormalPathLen] decalNormalPath UTF-8
//   [4]  decalRoughness  f32
//   [4]  decalMetalness  f32
//   [4]  decalOpacity    f32
//   [4]  particleEmissionRate   f32
//   [12] particleEmitDir        f32[3]
//   [4]  particleConeHalfAngle  f32
//   [4]  particleSpeedMin       f32
//   [4]  particleSpeedMax       f32
//   [4]  particleLifetimeMin    f32
//   [4]  particleLifetimeMax    f32
//   [16] particleColorStart     f32[4]
//   [16] particleColorEnd       f32[4]
//   [4]  particleSizeStart      f32
//   [4]  particleSizeEnd        f32
//   [4]  particleDrag           f32
//   [12] particleGravity        f32[3]
//
//   --- Particle emitter extended params (v5) ---
//   [4]  particleEmissiveScale  f32
//   [4]  particleTurbulenceScale f32
//   [4]  particleVelocityStretch f32
//   [4]  particleSoftRange       f32
//   [4]  particleEmissiveStart   f32
//   [4]  particleEmissiveEnd     f32
//   [4]  particleAtlasCols       u32
//   [4]  particleAtlasRows       u32
//   [4]  particleAtlasFrameRate  f32
//   [4]  particleEmitsLight      u32 (0 = false, 1 = true)
//   [4]  particleShadowDensity   f32

#include "daedalus/world/dlevel_io.h"

#include <cstring>
#include <fstream>

namespace daedalus::world
{

namespace
{

constexpr u32 k_MAGIC   = 0x4C564C44u;  // 'D','L','V','L' as little-endian u32
constexpr u32 k_VERSION = 5u;

// ─── Write helpers ────────────────────────────────────────────────────────────

struct Writer
{
    std::ofstream& ofs;

    template<typename T>
    void write(const T& value)
    {
        ofs.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    void writeStr(const std::string& s)
    {
        const auto len = static_cast<u16>(s.size());
        write(len);
        if (len > 0) { ofs.write(s.data(), len); }
    }

    void writeUUID(const UUID& id)
    {
        write(id.hi);
        write(id.lo);
    }

    void writeVec2(const glm::vec2& v) { write(v.x); write(v.y); }
    void writeVec3(const glm::vec3& v) { write(v.x); write(v.y); write(v.z); }
};

// ─── Read helpers ─────────────────────────────────────────────────────────────

struct Reader
{
    const std::vector<byte>& buf;
    std::size_t              pos = 0;

    [[nodiscard]] bool atEnd() const noexcept { return pos >= buf.size(); }

    template<typename T>
    [[nodiscard]] bool read(T& out) noexcept
    {
        if (pos + sizeof(T) > buf.size()) { return false; }
        std::memcpy(&out, buf.data() + pos, sizeof(T));
        pos += sizeof(T);
        return true;
    }

    [[nodiscard]] bool readStr(std::string& out)
    {
        u16 len = 0;
        if (!read(len)) { return false; }
        if (pos + len > buf.size()) { return false; }
        out.assign(reinterpret_cast<const char*>(buf.data() + pos), len);
        pos += len;
        return true;
    }

    [[nodiscard]] bool readUUID(UUID& id) noexcept
    {
        return read(id.hi) && read(id.lo);
    }

    [[nodiscard]] bool readVec2(glm::vec2& v) noexcept
    {
        return read(v.x) && read(v.y);
    }

    [[nodiscard]] bool readVec3(glm::vec3& v) noexcept
    {
        return read(v.x) && read(v.y) && read(v.z);
    }

    [[nodiscard]] bool readBytes(std::vector<u8>& out, u32 count)
    {
        if (pos + count > buf.size()) { return false; }
        out.assign(
            reinterpret_cast<const u8*>(buf.data() + pos),
            reinterpret_cast<const u8*>(buf.data() + pos + count));
        pos += count;
        return true;
    }
};

} // anonymous namespace

// ─── saveDlevel ───────────────────────────────────────────────────────────────

std::expected<void, DlevelError> saveDlevel(const LevelPackData&         pack,
                                             const std::filesystem::path& path)
{
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) { return std::unexpected(DlevelError::WriteError); }

    Writer w{ofs};

    // ── Header ──────────────────────────────────────────────────────────────
    w.write(k_MAGIC);
    w.write(k_VERSION);
    w.write(static_cast<u32>(pack.map.sectors.size()));
    w.write(static_cast<u32>(pack.textures.size()));
    w.write(static_cast<u32>(pack.lights.size()));
    w.write(static_cast<u32>(pack.playerStart.has_value() ? 1u : 0u));
    w.write(static_cast<u32>(pack.entities.size()));  // v2

    // ── Map meta ────────────────────────────────────────────────────────────
    w.writeStr(pack.map.name);
    w.writeStr(pack.map.author);
    w.writeVec3(pack.map.globalAmbientColor);
    w.write(pack.map.globalAmbientIntensity);

    // ── Sectors (identical layout to .dmap v1) ──────────────────────────────
    for (const Sector& sec : pack.map.sectors)
    {
        w.write(static_cast<u32>(sec.walls.size()));
        w.write(static_cast<u32>(sec.flags));
        w.write(sec.floorHeight);
        w.write(sec.ceilHeight);
        w.writeUUID(sec.floorMaterialId);
        w.writeUUID(sec.ceilMaterialId);
        w.writeVec3(sec.ambientColor);
        w.write(sec.ambientIntensity);

        for (const Wall& wall : sec.walls)
        {
            w.writeVec2(wall.p0);
            w.write(static_cast<u32>(wall.flags));
            w.write(wall.portalSectorId);
            w.writeUUID(wall.frontMaterialId);
            w.writeUUID(wall.upperMaterialId);
            w.writeUUID(wall.lowerMaterialId);
            w.writeVec2(wall.uvOffset);
            w.writeVec2(wall.uvScale);
            w.write(wall.uvRotation);
        }
    }

    // ── Textures ────────────────────────────────────────────────────────────
    for (const auto& [uuid, tex] : pack.textures)
    {
        w.writeUUID(uuid);
        w.write(tex.width);
        w.write(tex.height);
        const u32 dataSize = tex.width * tex.height * 4u;
        w.write(dataSize);
        if (dataSize > 0u && !tex.pixels.empty())
            ofs.write(reinterpret_cast<const char*>(tex.pixels.data()),
                      static_cast<std::streamsize>(dataSize));
    }

    // ── Sun settings ────────────────────────────────────────────────────────
    w.writeVec3(pack.sun.direction);
    w.writeVec3(pack.sun.color);
    w.write(pack.sun.intensity);

    // ── Lights ──────────────────────────────────────────────────────────────
    for (const LevelLight& light : pack.lights)
    {
        w.write(static_cast<u32>(light.type));
        w.writeVec3(light.position);
        w.writeVec3(light.color);
        w.write(light.radius);
        w.write(light.intensity);
        w.writeVec3(light.direction);
        w.write(light.innerConeAngle);
        w.write(light.outerConeAngle);
        w.write(light.range);
    }

    // ── Player start (optional) ──────────────────────────────────────────────
    if (pack.playerStart.has_value())
    {
        w.writeVec3(pack.playerStart->position);
        w.write(pack.playerStart->yaw);
    }

    // ── Entities ─────────────────────────────────────────────────────────────
    for (const LevelEntity& ent : pack.entities)
    {
        // base fields
        w.writeStr(ent.name);
        w.writeVec3(ent.position);
        w.write(ent.yaw);
        w.write(ent.sectorId);
        w.write(static_cast<u32>(ent.shape));
        w.write(static_cast<u32>(ent.dynamic ? 1u : 0u));
        w.write(ent.mass);
        w.writeVec3(ent.halfExtents);

        // script descriptor
        w.writeStr(ent.scriptPath);
        w.write(static_cast<u32>(ent.exposedVars.size()));
        for (const auto& [k, v] : ent.exposedVars)
        {
            w.writeStr(k);
            w.writeStr(v);
        }

        // audio descriptor
        w.writeStr(ent.soundPath);
        w.write(ent.soundFalloffRadius);
        w.write(ent.soundVolume);
        w.write(static_cast<u32>(ent.soundLoop     ? 1u : 0u));
        w.write(static_cast<u32>(ent.soundAutoPlay  ? 1u : 0u));

        // v4 visual descriptor
        w.write(static_cast<u32>(ent.visualType));
        w.writeStr(ent.assetPath);
        w.write(ent.tint.r); w.write(ent.tint.g); w.write(ent.tint.b); w.write(ent.tint.a);
        w.writeVec3(ent.visualScale);
        w.write(ent.visualPitch);
        w.write(ent.visualRoll);
        w.write(ent.animFrameCount);
        w.write(ent.animCols);
        w.write(ent.animRows);
        w.write(ent.animFrameRate);
        w.write(ent.rotatedSpriteDirCount);
        w.writeStr(ent.decalNormalPath);
        w.write(ent.decalRoughness);
        w.write(ent.decalMetalness);
        w.write(ent.decalOpacity);
        w.write(ent.particleEmissionRate);
        w.writeVec3(ent.particleEmitDir);
        w.write(ent.particleConeHalfAngle);
        w.write(ent.particleSpeedMin);
        w.write(ent.particleSpeedMax);
        w.write(ent.particleLifetimeMin);
        w.write(ent.particleLifetimeMax);
        w.write(ent.particleColorStart.r); w.write(ent.particleColorStart.g);
        w.write(ent.particleColorStart.b); w.write(ent.particleColorStart.a);
        w.write(ent.particleColorEnd.r); w.write(ent.particleColorEnd.g);
        w.write(ent.particleColorEnd.b); w.write(ent.particleColorEnd.a);
        w.write(ent.particleSizeStart);
        w.write(ent.particleSizeEnd);
        w.write(ent.particleDrag);
        w.writeVec3(ent.particleGravity);
        // v5 extended particle params
        w.write(ent.particleEmissiveScale);
        w.write(ent.particleTurbulenceScale);
        w.write(ent.particleVelocityStretch);
        w.write(ent.particleSoftRange);
        w.write(ent.particleEmissiveStart);
        w.write(ent.particleEmissiveEnd);
        w.write(ent.particleAtlasCols);
        w.write(ent.particleAtlasRows);
        w.write(ent.particleAtlasFrameRate);
        w.write(static_cast<u32>(ent.particleEmitsLight ? 1u : 0u));
        w.write(ent.particleShadowDensity);
    }

    if (!ofs) { return std::unexpected(DlevelError::WriteError); }
    return {};
}

// ─── loadDlevel ───────────────────────────────────────────────────────────────

std::expected<LevelPackData, DlevelError> loadDlevel(const std::filesystem::path& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) { return std::unexpected(DlevelError::FileNotFound); }

    ifs.seekg(0, std::ios::end);
    const auto fileSize = static_cast<std::size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    std::vector<byte> buf(fileSize);
    if (fileSize > 0)
    {
        ifs.read(reinterpret_cast<char*>(buf.data()),
                 static_cast<std::streamsize>(fileSize));
        if (!ifs) { return std::unexpected(DlevelError::ParseError); }
    }

    Reader r{buf};

    // ── Header ──────────────────────────────────────────────────────────────
    u32 magic = 0, version = 0, sectorCount = 0;
    u32 textureCount = 0, lightCount = 0, hasPlayerStart = 0;
    if (!r.read(magic)         || !r.read(version)        || !r.read(sectorCount) ||
        !r.read(textureCount)  || !r.read(lightCount)     || !r.read(hasPlayerStart))
    {
        return std::unexpected(DlevelError::ParseError);
    }
    if (magic   != k_MAGIC)   { return std::unexpected(DlevelError::ParseError);      }
    if (version != k_VERSION)  { return std::unexpected(DlevelError::VersionMismatch); }

    u32 entityCount = 0;
    if (!r.read(entityCount)) { return std::unexpected(DlevelError::ParseError); }

    LevelPackData pack;

    // ── Map meta ────────────────────────────────────────────────────────────
    if (!r.readStr(pack.map.name) || !r.readStr(pack.map.author))
        return std::unexpected(DlevelError::ParseError);
    if (!r.readVec3(pack.map.globalAmbientColor) ||
        !r.read(pack.map.globalAmbientIntensity))
        return std::unexpected(DlevelError::ParseError);

    // ── Sectors ─────────────────────────────────────────────────────────────
    pack.map.sectors.resize(sectorCount);
    for (u32 si = 0; si < sectorCount; ++si)
    {
        Sector& sec = pack.map.sectors[si];

        u32 wallCount = 0, flagsRaw = 0;
        if (!r.read(wallCount)      || !r.read(flagsRaw)        ||
            !r.read(sec.floorHeight) || !r.read(sec.ceilHeight))
            return std::unexpected(DlevelError::ParseError);
        sec.flags = static_cast<SectorFlags>(flagsRaw);

        if (!r.readUUID(sec.floorMaterialId) || !r.readUUID(sec.ceilMaterialId))
            return std::unexpected(DlevelError::ParseError);
        if (!r.readVec3(sec.ambientColor) || !r.read(sec.ambientIntensity))
            return std::unexpected(DlevelError::ParseError);

        sec.walls.resize(wallCount);
        for (u32 wi = 0; wi < wallCount; ++wi)
        {
            Wall& wall = sec.walls[wi];
            u32   wallFlagsRaw = 0;

            if (!r.readVec2(wall.p0) || !r.read(wallFlagsRaw) ||
                !r.read(wall.portalSectorId))
                return std::unexpected(DlevelError::ParseError);
            wall.flags = static_cast<WallFlags>(wallFlagsRaw);

            if (!r.readUUID(wall.frontMaterialId) ||
                !r.readUUID(wall.upperMaterialId)  ||
                !r.readUUID(wall.lowerMaterialId))
                return std::unexpected(DlevelError::ParseError);
            if (!r.readVec2(wall.uvOffset) || !r.readVec2(wall.uvScale) ||
                !r.read(wall.uvRotation))
                return std::unexpected(DlevelError::ParseError);
        }
    }

    // ── Textures ────────────────────────────────────────────────────────────
    for (u32 ti = 0; ti < textureCount; ++ti)
    {
        UUID         uuid;
        LevelTexture tex;
        u32          dataSize = 0;

        if (!r.readUUID(uuid)       || !r.read(tex.width) ||
            !r.read(tex.height)     || !r.read(dataSize))
            return std::unexpected(DlevelError::ParseError);

        if (dataSize > 0u)
        {
            if (!r.readBytes(tex.pixels, dataSize))
                return std::unexpected(DlevelError::ParseError);
        }
        pack.textures.emplace(uuid, std::move(tex));
    }

    // ── Sun settings ────────────────────────────────────────────────────────
    if (!r.readVec3(pack.sun.direction) ||
        !r.readVec3(pack.sun.color)     ||
        !r.read(pack.sun.intensity))
        return std::unexpected(DlevelError::ParseError);

    // ── Lights ──────────────────────────────────────────────────────────────
    pack.lights.resize(lightCount);
    for (u32 li = 0; li < lightCount; ++li)
    {
        LevelLight& light = pack.lights[li];
        u32         typeRaw = 0;

        if (!r.read(typeRaw)               ||
            !r.readVec3(light.position)    ||
            !r.readVec3(light.color)       ||
            !r.read(light.radius)          ||
            !r.read(light.intensity)       ||
            !r.readVec3(light.direction)   ||
            !r.read(light.innerConeAngle)  ||
            !r.read(light.outerConeAngle)  ||
            !r.read(light.range))
            return std::unexpected(DlevelError::ParseError);

        light.type = static_cast<LevelLightType>(typeRaw);
    }

    // ── Player start (optional) ──────────────────────────────────────────────
    if (hasPlayerStart == 1u)
    {
        LevelPlayerStart ps;
        if (!r.readVec3(ps.position) || !r.read(ps.yaw))
            return std::unexpected(DlevelError::ParseError);
        pack.playerStart = ps;
    }

    // ── Entities ─────────────────────────────────────────────────────────────
    pack.entities.resize(entityCount);
    for (u32 ei = 0; ei < entityCount; ++ei)
    {
        LevelEntity& ent = pack.entities[ei];
        u32 shapeRaw = 0, dynamicRaw = 0;

        // base fields
        if (!r.readStr(ent.name)          ||
            !r.readVec3(ent.position)     ||
            !r.read(ent.yaw)              ||
            !r.read(ent.sectorId)         ||
            !r.read(shapeRaw)             ||
            !r.read(dynamicRaw)           ||
            !r.read(ent.mass)             ||
            !r.readVec3(ent.halfExtents))
            return std::unexpected(DlevelError::ParseError);

        ent.shape   = static_cast<LevelCollisionShape>(shapeRaw);
        ent.dynamic = (dynamicRaw != 0u);

        // script descriptor
        if (!r.readStr(ent.scriptPath))
            return std::unexpected(DlevelError::ParseError);

        u32 varCount = 0;
        if (!r.read(varCount))
            return std::unexpected(DlevelError::ParseError);
        for (u32 vi = 0; vi < varCount; ++vi)
        {
            std::string k, v;
            if (!r.readStr(k) || !r.readStr(v))
                return std::unexpected(DlevelError::ParseError);
            ent.exposedVars.emplace(std::move(k), std::move(v));
        }

        // audio descriptor
        u32 loopRaw = 0, autoPlayRaw = 0;
        if (!r.readStr(ent.soundPath)         ||
            !r.read(ent.soundFalloffRadius)   ||
            !r.read(ent.soundVolume)          ||
            !r.read(loopRaw)                  ||
            !r.read(autoPlayRaw))
            return std::unexpected(DlevelError::ParseError);

        ent.soundLoop     = (loopRaw     != 0u);
        ent.soundAutoPlay = (autoPlayRaw != 0u);

        // v4 visual descriptor
        u32 visualTypeRaw = 0;
        if (!r.read(visualTypeRaw))
            return std::unexpected(DlevelError::ParseError);
        ent.visualType = static_cast<LevelEntityVisualType>(visualTypeRaw);

        if (!r.readStr(ent.assetPath))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(ent.tint.r) || !r.read(ent.tint.g) ||
            !r.read(ent.tint.b) || !r.read(ent.tint.a))
            return std::unexpected(DlevelError::ParseError);
        if (!r.readVec3(ent.visualScale))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(ent.visualPitch) || !r.read(ent.visualRoll))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(ent.animFrameCount) || !r.read(ent.animCols) ||
            !r.read(ent.animRows)       || !r.read(ent.animFrameRate))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(ent.rotatedSpriteDirCount))
            return std::unexpected(DlevelError::ParseError);
        if (!r.readStr(ent.decalNormalPath))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(ent.decalRoughness) || !r.read(ent.decalMetalness) ||
            !r.read(ent.decalOpacity))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(ent.particleEmissionRate))
            return std::unexpected(DlevelError::ParseError);
        if (!r.readVec3(ent.particleEmitDir))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(ent.particleConeHalfAngle) ||
            !r.read(ent.particleSpeedMin)      ||
            !r.read(ent.particleSpeedMax)      ||
            !r.read(ent.particleLifetimeMin)   ||
            !r.read(ent.particleLifetimeMax))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(ent.particleColorStart.r) || !r.read(ent.particleColorStart.g) ||
            !r.read(ent.particleColorStart.b) || !r.read(ent.particleColorStart.a))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(ent.particleColorEnd.r) || !r.read(ent.particleColorEnd.g) ||
            !r.read(ent.particleColorEnd.b)  || !r.read(ent.particleColorEnd.a))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(ent.particleSizeStart) || !r.read(ent.particleSizeEnd) ||
            !r.read(ent.particleDrag))
            return std::unexpected(DlevelError::ParseError);
        if (!r.readVec3(ent.particleGravity))
            return std::unexpected(DlevelError::ParseError);
        // v5 extended particle params
        if (!r.read(ent.particleEmissiveScale)   ||
            !r.read(ent.particleTurbulenceScale) ||
            !r.read(ent.particleVelocityStretch) ||
            !r.read(ent.particleSoftRange)       ||
            !r.read(ent.particleEmissiveStart)   ||
            !r.read(ent.particleEmissiveEnd)     ||
            !r.read(ent.particleAtlasCols)       ||
            !r.read(ent.particleAtlasRows)       ||
            !r.read(ent.particleAtlasFrameRate))
            return std::unexpected(DlevelError::ParseError);
        u32 emitsLightRaw = 0;
        if (!r.read(emitsLightRaw) || !r.read(ent.particleShadowDensity))
            return std::unexpected(DlevelError::ParseError);
        ent.particleEmitsLight = (emitsLightRaw != 0u);
    }

    return pack;
}

} // namespace daedalus::world
