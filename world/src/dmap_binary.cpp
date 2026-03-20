// dmap_binary.cpp
// Binary .dmap serialisation / deserialisation.
//
// ─── Format specification ─────────────────────────────────────────────────────────
//
// All multi-byte integers are little-endian.
// Versions are forwards-compatible: the reader rejects files where
// version > k_VERSION, but accepts all older versions using defaults for
// fields that did not exist in that version.
//
// Header (16 bytes):
//   [4]  magic         u32 = 0x50414D44  ('D','M','A','P')
//   [4]  version       u32 = 2  (current)
//   [4]  sectorCount   u32
//   [4]  _pad          u32 = 0  (reserved)
//
// Map meta (variable):
//   [2]  nameLen       u16
//   [nameLen] name     UTF-8
//   [2]  authorLen     u16
//   [authorLen] author UTF-8
//   [12] globalAmbientColor   f32[3]
//   [4]  globalAmbientIntensity f32
//
// Per sector (repeated sectorCount times):
//   [4]  wallCount       u32
//   [4]  flags           u32  (SectorFlags)
//   [4]  floorHeight     f32
//   [4]  ceilHeight      f32
//   [16] floorMaterialId UUID (u64 hi, u64 lo)
//   [16] ceilMaterialId  UUID
//   [12] ambientColor    f32[3]
//   [4]  ambientIntensity f32
//   --- v1 sector header ends here (60 bytes) ---
//   v2 additions (appended, omitted when reading v1 files):
//   [4]  floorShape      u32  (FloorShape enum: 0=Flat, 1=Heightfield, 2=VisualStairs)
//   [4]  hasStairProfile u32  (0 or 1)
//   if hasStairProfile == 1:
//     [4]  stepCount     u32
//     [4]  riserHeight   f32
//     [4]  treadDepth    f32
//     [4]  directionAngle f32
//
//   Per wall (repeated wallCount times, immediately after sector record):
//     [8]  p0              f32[2]  (map X, map Z)
//     [4]  flags           u32     (WallFlags)
//     [4]  portalSectorId  u32     (0xFFFFFFFF = none)
//     [16] frontMaterialId UUID
//     [16] upperMaterialId UUID
//     [16] lowerMaterialId UUID
//     [8]  uvOffset        f32[2]
//     [8]  uvScale         f32[2]
//     [4]  uvRotation      f32
//     --- v1 wall record ends here (84 bytes) ---
//     --- v2 additions (appended, omitted when reading v1 files):
//     [1]  heightFlags     u8   bit 0 = hasFloorHeightOverride,
//                               bit 1 = hasCeilHeightOverride
//     [4]  floorHeightOverride f32  (only if bit 0 set)
//     [4]  ceilHeightOverride  f32  (only if bit 1 set)
//
//   v3 sector additions (appended after v2 stairProfile, omitted when reading v1/v2):
//   [4]  floorPortalSectorId u32  (0xFFFFFFFF = none)
//   [4]  ceilPortalSectorId  u32  (0xFFFFFFFF = none)
//   [16] floorPortalMaterialId UUID
//   [16] ceilPortalMaterialId  UUID

#include "daedalus/world/dmap_io.h"

#include <cstring>
#include <fstream>

namespace daedalus::world
{

namespace
{

constexpr u32 k_MAGIC   = 0x50414D44u;  // 'D','M','A','P' as little-endian u32
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
};

} // anonymous namespace

// ─── saveDmap ─────────────────────────────────────────────────────────────────

std::expected<void, DmapError> saveDmap(const WorldMapData&         map,
                                         const std::filesystem::path& path)
{
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) { return std::unexpected(DmapError::WriteError); }

    Writer w{ofs};

    // Header
    w.write(k_MAGIC);
    w.write(k_VERSION);
    w.write(static_cast<u32>(map.sectors.size()));
    const u32 pad = 0u;
    w.write(pad);

    // Map meta
    w.writeStr(map.name);
    w.writeStr(map.author);
    w.writeVec3(map.globalAmbientColor);
    w.write(map.globalAmbientIntensity);

    // Sectors
    for (const Sector& sec : map.sectors)
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
            // v2: per-vertex height overrides
            const u8 hFlags =
                (wall.floorHeightOverride.has_value() ? 0x01u : 0u) |
                (wall.ceilHeightOverride .has_value() ? 0x02u : 0u);
            w.write(hFlags);
            if (wall.floorHeightOverride) w.write(*wall.floorHeightOverride);
            if (wall.ceilHeightOverride)  w.write(*wall.ceilHeightOverride);
        }
        // v2: floor shape and optional stair profile
        w.write(static_cast<u32>(sec.floorShape));
        const u32 hasStair = sec.stairProfile.has_value() ? 1u : 0u;
        w.write(hasStair);
        if (sec.stairProfile)
        {
            w.write(sec.stairProfile->stepCount);
            w.write(sec.stairProfile->riserHeight);
            w.write(sec.stairProfile->treadDepth);
            w.write(sec.stairProfile->directionAngle);
        }
        // v3: floor and ceiling portal IDs + materials
        w.write(sec.floorPortalSectorId);
        w.write(sec.ceilPortalSectorId);
        w.writeUUID(sec.floorPortalMaterialId);
        w.writeUUID(sec.ceilPortalMaterialId);
    }

    if (!ofs) { return std::unexpected(DmapError::WriteError); }
    return {};
}

// ─── loadDmap ─────────────────────────────────────────────────────────────────

std::expected<WorldMapData, DmapError> loadDmap(const std::filesystem::path& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) { return std::unexpected(DmapError::FileNotFound); }

    ifs.seekg(0, std::ios::end);
    const auto fileSize = static_cast<std::size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    std::vector<byte> buf(fileSize);
    if (fileSize > 0)
    {
        ifs.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(fileSize));
        if (!ifs) { return std::unexpected(DmapError::ParseError); }
    }

    Reader r{buf};

    // Header
    u32 magic = 0, version = 0, sectorCount = 0, pad = 0;
    if (!r.read(magic) || !r.read(version) ||
        !r.read(sectorCount) || !r.read(pad))
    {
        return std::unexpected(DmapError::ParseError);
    }
    if (magic != k_MAGIC) { return std::unexpected(DmapError::ParseError); }
    if (version > k_VERSION) { return std::unexpected(DmapError::VersionMismatch); }

    WorldMapData map;

    // Map meta
    if (!r.readStr(map.name) || !r.readStr(map.author))
    {
        return std::unexpected(DmapError::ParseError);
    }
    if (!r.readVec3(map.globalAmbientColor) || !r.read(map.globalAmbientIntensity))
    {
        return std::unexpected(DmapError::ParseError);
    }

    map.sectors.resize(sectorCount);
    for (u32 si = 0; si < sectorCount; ++si)
    {
        Sector& sec = map.sectors[si];

        u32 wallCount = 0, flagsRaw = 0;
        if (!r.read(wallCount) || !r.read(flagsRaw) ||
            !r.read(sec.floorHeight) || !r.read(sec.ceilHeight))
        {
            return std::unexpected(DmapError::ParseError);
        }
        sec.flags = static_cast<SectorFlags>(flagsRaw);

        if (!r.readUUID(sec.floorMaterialId) || !r.readUUID(sec.ceilMaterialId))
        {
            return std::unexpected(DmapError::ParseError);
        }
        if (!r.readVec3(sec.ambientColor) || !r.read(sec.ambientIntensity))
        {
            return std::unexpected(DmapError::ParseError);
        }

        sec.walls.resize(wallCount);
        for (u32 wi = 0; wi < wallCount; ++wi)
        {
            Wall& wall = sec.walls[wi];
            u32 wallFlagsRaw = 0;

            if (!r.readVec2(wall.p0) || !r.read(wallFlagsRaw) ||
                !r.read(wall.portalSectorId))
            {
                return std::unexpected(DmapError::ParseError);
            }
            wall.flags = static_cast<WallFlags>(wallFlagsRaw);

            if (!r.readUUID(wall.frontMaterialId) ||
                !r.readUUID(wall.upperMaterialId) ||
                !r.readUUID(wall.lowerMaterialId))
            {
                return std::unexpected(DmapError::ParseError);
            }
            if (!r.readVec2(wall.uvOffset) || !r.readVec2(wall.uvScale) ||
                !r.read(wall.uvRotation))
            {
                return std::unexpected(DmapError::ParseError);
            }
            // v2 additions: per-vertex height overrides
            if (version >= 2)
            {
                u8 hFlags = 0;
                if (!r.read(hFlags)) return std::unexpected(DmapError::ParseError);
                if (hFlags & 0x01u)
                {
                    f32 v = 0.0f;
                    if (!r.read(v)) return std::unexpected(DmapError::ParseError);
                    wall.floorHeightOverride = v;
                }
                if (hFlags & 0x02u)
                {
                    f32 v = 0.0f;
                    if (!r.read(v)) return std::unexpected(DmapError::ParseError);
                    wall.ceilHeightOverride = v;
                }
            }
        }
        // v2 additions: floor shape and optional stair profile
        if (version >= 2)
        {
            u32 shapeRaw = 0, hasStair = 0;
            if (!r.read(shapeRaw) || !r.read(hasStair))
                return std::unexpected(DmapError::ParseError);
            sec.floorShape = static_cast<FloorShape>(shapeRaw);
            if (hasStair)
            {
                StairProfile sp;
                if (!r.read(sp.stepCount) || !r.read(sp.riserHeight) ||
                    !r.read(sp.treadDepth) || !r.read(sp.directionAngle))
                    return std::unexpected(DmapError::ParseError);
                sec.stairProfile = sp;
            }
        }
        // v3: floor and ceiling portal fields
        if (version >= 3)
        {
            if (!r.read(sec.floorPortalSectorId) || !r.read(sec.ceilPortalSectorId))
                return std::unexpected(DmapError::ParseError);
            if (!r.readUUID(sec.floorPortalMaterialId) || !r.readUUID(sec.ceilPortalMaterialId))
                return std::unexpected(DmapError::ParseError);
        }
    }

    return map;
}

} // namespace daedalus::world
