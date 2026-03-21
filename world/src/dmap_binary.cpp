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
//   [4]  version       u32 = 5  (current)
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
//
//     v4 wall additions (appended after v2 heightFlags, omitted when reading v1/v2/v3):
//     [1]  curveFlags     u8   bit 0 = hasCurveControlA, bit 1 = hasCurveControlB
//     [4]  curveSubdivisions u32 (always written in v4, default 12)
//     [8]  curveControlA  f32[2]  (only if bit 0 set)
//     [8]  curveControlB  f32[2]  (only if bit 1 set)
//
//   v4 sector additions (appended after v3 portal fields, omitted when reading v1/v2/v3):
//   [4]  detailCount u32
//   Per detail brush (detailCount records, fixed size 156 bytes each):
//     [64] transform       f32[16]
//     [4]  type            u32 (DetailBrushType)
//     --- geometry params (all always written) ---
//     [12] halfExtents     f32[3]
//     [4]  slopeAxis       u32
//     [4]  radius          f32
//     [4]  height          f32
//     [4]  segmentCount    u32
//     [4]  spanWidth       f32
//     [4]  archHeight      f32
//     [4]  thickness       f32
//     [4]  archProfile     u32 (ArchProfile)
//     [4]  archSegments    u32
//     [16] meshAssetId     UUID
//     [16] materialId      UUID
//     [4]  brushFlags      u32 (bit 0 = collidable, bit 1 = castsShadow)
//
//   v5 sector additions (appended after v4 detail brushes, omitted when reading v1/v2/v3/v4):
//   [4]  hasHeightfield  u32  (0 or 1)
//   if hasHeightfield == 1:
//     [4]  gridWidth  u32
//     [4]  gridDepth  u32
//     [8]  worldMin   f32[2]
//     [8]  worldMax   f32[2]
//     [gridWidth*gridDepth*4]  samples  f32[]   (uncompressed; .dlevel compresses via zlib)
//
//   v6 sector additions (appended after v5 heightfield, omitted when reading v1–v5):
//   [8]  floorUvOffset    f32[2]
//   [8]  floorUvScale     f32[2]
//   [4]  floorUvRotation  f32
//   [8]  ceilUvOffset     f32[2]
//   [8]  ceilUvScale      f32[2]
//   [4]  ceilUvRotation   f32

#include "daedalus/world/dmap_io.h"

#include <cstring>
#include <fstream>

namespace daedalus::world
{

namespace
{

constexpr u32 k_MAGIC   = 0x50414D44u;  // 'D','M','A','P' as little-endian u32
constexpr u32 k_VERSION = 6u;

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
    void writeMat4(const glm::mat4& m)
    {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                write(m[c][r]);
    }
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
    [[nodiscard]] bool readMat4(glm::mat4& m) noexcept
    {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                if (!read(m[c][r])) return false;
        return true;
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
            // v4: Bezier curve handles
            const u8 cFlags =
                (wall.curveControlA.has_value() ? 0x01u : 0u) |
                (wall.curveControlB.has_value() ? 0x02u : 0u);
            w.write(cFlags);
            w.write(wall.curveSubdivisions);
            if (wall.curveControlA) w.writeVec2(*wall.curveControlA);
            if (wall.curveControlB) w.writeVec2(*wall.curveControlB);
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
        // v4: detail brushes (fixed-size record per brush)
        w.write(static_cast<u32>(sec.details.size()));
        for (const auto& brush : sec.details)
        {
            w.writeMat4(brush.transform);
            w.write(static_cast<u32>(brush.type));
            w.writeVec3(brush.geom.halfExtents);
            w.write(brush.geom.slopeAxis);
            w.write(brush.geom.radius);
            w.write(brush.geom.height);
            w.write(brush.geom.segmentCount);
            w.write(brush.geom.spanWidth);
            w.write(brush.geom.archHeight);
            w.write(brush.geom.thickness);
            w.write(static_cast<u32>(brush.geom.archProfile));
            w.write(brush.geom.archSegments);
            w.writeUUID(brush.geom.meshAssetId);
            w.writeUUID(brush.materialId);
            const u32 brushFlags =
                (brush.collidable  ? 0x01u : 0u) |
                (brush.castsShadow ? 0x02u : 0u);
            w.write(brushFlags);
        }
        // v5: heightfield terrain floor
        const bool hasHF = sec.heightfield.has_value();
        w.write(static_cast<u32>(hasHF ? 1u : 0u));
        if (hasHF)
        {
            const auto& hf = *sec.heightfield;
            w.write(hf.gridWidth);
            w.write(hf.gridDepth);
            w.writeVec2(hf.worldMin);
            w.writeVec2(hf.worldMax);
            // Samples: raw f32 array (uncompressed in .dmap; .dlevel compresses via zlib).
            for (const f32 s : hf.samples) w.write(s);
        }
        // v6: floor and ceiling UV mapping
        w.writeVec2(sec.floorUvOffset);
        w.writeVec2(sec.floorUvScale);
        w.write(sec.floorUvRotation);
        w.writeVec2(sec.ceilUvOffset);
        w.writeVec2(sec.ceilUvScale);
        w.write(sec.ceilUvRotation);
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
            // v4 additions: Bezier curve handles
            if (version >= 4)
            {
                u8 cFlags = 0;
                if (!r.read(cFlags)) return std::unexpected(DmapError::ParseError);
                if (!r.read(wall.curveSubdivisions)) return std::unexpected(DmapError::ParseError);
                if (cFlags & 0x01u)
                {
                    glm::vec2 ca{0.0f};
                    if (!r.readVec2(ca)) return std::unexpected(DmapError::ParseError);
                    wall.curveControlA = ca;
                }
                if (cFlags & 0x02u)
                {
                    glm::vec2 cb{0.0f};
                    if (!r.readVec2(cb)) return std::unexpected(DmapError::ParseError);
                    wall.curveControlB = cb;
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
        // v4: detail brushes
        if (version >= 4)
        {
            u32 detailCount = 0;
            if (!r.read(detailCount)) return std::unexpected(DmapError::ParseError);
            sec.details.resize(detailCount);
            for (auto& brush : sec.details)
            {
                if (!r.readMat4(brush.transform)) return std::unexpected(DmapError::ParseError);
                u32 typeRaw = 0;
                if (!r.read(typeRaw)) return std::unexpected(DmapError::ParseError);
                brush.type = static_cast<DetailBrushType>(typeRaw);
                if (!r.readVec3(brush.geom.halfExtents) || !r.read(brush.geom.slopeAxis))
                    return std::unexpected(DmapError::ParseError);
                if (!r.read(brush.geom.radius) || !r.read(brush.geom.height) ||
                    !r.read(brush.geom.segmentCount))
                    return std::unexpected(DmapError::ParseError);
                if (!r.read(brush.geom.spanWidth) || !r.read(brush.geom.archHeight) ||
                    !r.read(brush.geom.thickness))
                    return std::unexpected(DmapError::ParseError);
                u32 profileRaw = 0;
                if (!r.read(profileRaw) || !r.read(brush.geom.archSegments))
                    return std::unexpected(DmapError::ParseError);
                brush.geom.archProfile = static_cast<ArchProfile>(profileRaw);
                if (!r.readUUID(brush.geom.meshAssetId) || !r.readUUID(brush.materialId))
                    return std::unexpected(DmapError::ParseError);
                u32 brushFlags = 0;
                if (!r.read(brushFlags)) return std::unexpected(DmapError::ParseError);
                brush.collidable  = (brushFlags & 0x01u) != 0u;
                brush.castsShadow = (brushFlags & 0x02u) != 0u;
            }
        }
        // v5: heightfield
        if (version >= 5)
        {
            u32 hasHF = 0;
            if (!r.read(hasHF)) return std::unexpected(DmapError::ParseError);
            if (hasHF)
            {
                HeightfieldFloor hf;
                if (!r.read(hf.gridWidth) || !r.read(hf.gridDepth))
                    return std::unexpected(DmapError::ParseError);
                if (!r.readVec2(hf.worldMin) || !r.readVec2(hf.worldMax))
                    return std::unexpected(DmapError::ParseError);
                const u32 count = hf.gridWidth * hf.gridDepth;
                hf.samples.resize(count);
                for (u32 k = 0; k < count; ++k)
                    if (!r.read(hf.samples[k])) return std::unexpected(DmapError::ParseError);
                sec.heightfield = std::move(hf);
            }
        }
        // v6: floor and ceiling UV mapping (defaults already set by Sector constructor)
        if (version >= 6)
        {
            if (!r.readVec2(sec.floorUvOffset) || !r.readVec2(sec.floorUvScale) ||
                !r.read(sec.floorUvRotation))
                return std::unexpected(DmapError::ParseError);
            if (!r.readVec2(sec.ceilUvOffset) || !r.readVec2(sec.ceilUvScale) ||
                !r.read(sec.ceilUvRotation))
                return std::unexpected(DmapError::ParseError);
        }
    }

    return map;
}

} // namespace daedalus::world
