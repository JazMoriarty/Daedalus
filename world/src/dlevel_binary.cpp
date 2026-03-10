// dlevel_binary.cpp
// Binary .dlevel v2 serialisation / deserialisation.
//
// ─── Format specification (version 2) ────────────────────────────────────────
//
// All multi-byte integers are little-endian.
//
// Header (28 bytes):
//   [4]  magic          u32 = 0x4C564C44  ('D','L','V','L' as little-endian)
//   [4]  version        u32 = 2
//   [4]  sectorCount    u32
//   [4]  textureCount   u32
//   [4]  lightCount     u32
//   [4]  hasPlayerStart u32  (0 = absent, 1 = present)
//   [4]  entityCount    u32   ← added in v2; absent in v1 files
//
// (Version 1 header was 24 bytes with no entityCount field. v2 readers treat
//  v1 files as entityCount = 0 and do not attempt to read the entity section.)
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
//   --- Audio descriptor (v3) ---
//   [2]  soundPathLen    u16
//   [soundPathLen] soundPath UTF-8
//   [4]  soundFalloffRadius f32
//   [4]  soundVolume        f32
//   [4]  soundLoop          u32  (0 = false, 1 = true)
//   [4]  soundAutoPlay      u32  (0 = false, 1 = true)

#include "daedalus/world/dlevel_io.h"

#include <cstring>
#include <fstream>

namespace daedalus::world
{

namespace
{

constexpr u32 k_MAGIC   = 0x4C564C44u;  // 'D','L','V','L' as little-endian u32
constexpr u32 k_VERSION = 3u;

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

    // ── Entities (v3) ──────────────────────────────────────────────────────────────────────
    for (const LevelEntity& ent : pack.entities)
    {
        // v2 base fields
        w.writeStr(ent.name);
        w.writeVec3(ent.position);
        w.write(ent.yaw);
        w.write(ent.sectorId);
        w.write(static_cast<u32>(ent.shape));
        w.write(static_cast<u32>(ent.dynamic ? 1u : 0u));
        w.write(ent.mass);
        w.writeVec3(ent.halfExtents);

        // v3 script descriptor
        w.writeStr(ent.scriptPath);
        w.write(static_cast<u32>(ent.exposedVars.size()));
        for (const auto& [k, v] : ent.exposedVars)
        {
            w.writeStr(k);
            w.writeStr(v);
        }

        // v3 audio descriptor
        w.writeStr(ent.soundPath);
        w.write(ent.soundFalloffRadius);
        w.write(ent.soundVolume);
        w.write(static_cast<u32>(ent.soundLoop     ? 1u : 0u));
        w.write(static_cast<u32>(ent.soundAutoPlay  ? 1u : 0u));
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
    if (version >  k_VERSION) { return std::unexpected(DlevelError::VersionMismatch); }

    // v2 adds entityCount to the header; v1 files have no such field.
    u32 entityCount = 0;
    if (version >= 2u)
    {
        if (!r.read(entityCount)) { return std::unexpected(DlevelError::ParseError); }
    }

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

    // ── Entities (v3) ──────────────────────────────────────────────────────────────────────
    pack.entities.resize(entityCount);
    for (u32 ei = 0; ei < entityCount; ++ei)
    {
        LevelEntity& ent = pack.entities[ei];
        u32 shapeRaw = 0, dynamicRaw = 0;

        // v2 base fields
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

        // v3 script descriptor
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

        // v3 audio descriptor
        u32 loopRaw = 0, autoPlayRaw = 0;
        if (!r.readStr(ent.soundPath)         ||
            !r.read(ent.soundFalloffRadius)   ||
            !r.read(ent.soundVolume)          ||
            !r.read(loopRaw)                  ||
            !r.read(autoPlayRaw))
            return std::unexpected(DlevelError::ParseError);

        ent.soundLoop     = (loopRaw     != 0u);
        ent.soundAutoPlay = (autoPlayRaw != 0u);
    }

    return pack;
}

} // namespace daedalus::world
