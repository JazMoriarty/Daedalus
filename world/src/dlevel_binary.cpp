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
//
// Render settings (present only in version >= 6; ~119 bytes + LUT path string):
//   [4]  fogEnabled                       u32 (0 = false, 1 = true)
//   [12] fogColor                         f32[3]
//   [4]  fogDensity                       f32
//   [4]  fogHeightFalloff                 f32
//   [4]  ssrEnabled                       u32 (0 = false, 1 = true)
//   [4]  ssrMaxRayDistance                f32
//   [4]  ssrStrideLength                  f32
//   [4]  ssrMaxSteps                      u32
//   [4]  ssrBinarySearchIterations        u32
//   [4]  dofEnabled                       u32 (0 = false, 1 = true)
//   [4]  dofFocalDistance                 f32
//   [4]  dofFocalRange                    f32
//   [4]  dofBokehRadius                   f32
//   [4]  motionBlurEnabled                u32 (0 = false, 1 = true)
//   [4]  motionBlurStrength               f32
//   [4]  motionBlurMaxSamples             u32
//   [4]  colorGradingEnabled              u32 (0 = false, 1 = true)
//   [2]  colorGradingLutPath              u16 (length prefix) + UTF-8 string
//   [4]  colorGradingLutStrength          f32
//   [4]  bloomEnabled                     u32 (0 = false, 1 = true)
//   [4]  bloomThreshold                   f32
//   [4]  bloomIntensity                   f32
//   [4]  chromaticAberrationEnabled      u32 (0 = false, 1 = true)
//   [4]  chromaticAberrationStrength     f32
//   [4]  vignetteEnabled                  u32 (0 = false, 1 = true)
//   [4]  vignetteIntensity                f32
//   [4]  vignetteSmoothness               f32
//   [4]  filmGrainEnabled                 u32 (0 = false, 1 = true)
//   [4]  filmGrainIntensity               f32
//   [4]  fxaaEnabled                      u32 (0 = false, 1 = true)
//   [4]  fxaaQualitySubpix                f32
//   [4]  fxaaQualityEdgeThreshold         f32
//   [4]  fxaaQualityEdgeThresholdMin     f32
//   [4]  rayTracingEnabled                u32 (0 = false, 1 = true)
//   [4]  rayTracingMaxBounces             u32
//   [4]  rayTracingSamplesPerPixel       u32
//   [4]  rayTracingEnableGI              u32 (0 = false, 1 = true)
//   [4]  rayTracingGIIntensity            f32

#include "daedalus/world/dlevel_io.h"

#include <cstring>
#include <fstream>

namespace daedalus::world
{

namespace
{

constexpr u32 k_MAGIC   = 0x4C564C44u;  // 'D','L','V','L' as little-endian u32
constexpr u32 k_VERSION = 9u;  // v9: ceiling heightfield serialization

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

    // ── Sectors ─────────────────────────────────────────────────────────────
    for (const Sector& sec : pack.map.sectors)
    {
        // ─ Existing header (unchanged, matches v1-v7) ─────────────────────
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
            // ─ Existing wall fields (unchanged) ─────────────────────────
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

        // ─ Phase 1F sector extension (v8+) ───────────────────────────────
        w.write(static_cast<u32>(sec.floorShape));
        w.write(sec.floorPortalSectorId);
        w.write(sec.ceilPortalSectorId);
        w.writeUUID(sec.floorPortalMaterialId);
        w.writeUUID(sec.ceilPortalMaterialId);

        // Stair profile (optional)
        const u32 hasStair = sec.stairProfile.has_value() ? 1u : 0u;
        w.write(hasStair);
        if (hasStair)
        {
            w.write(sec.stairProfile->stepCount);
            w.write(sec.stairProfile->riserHeight);
            w.write(sec.stairProfile->treadDepth);
            w.write(sec.stairProfile->directionAngle);
        }

        // Floor heightfield (optional)
        const u32 hasHF = (sec.heightfield.has_value() &&
                           !sec.heightfield->samples.empty()) ? 1u : 0u;
        w.write(hasHF);
        if (hasHF)
        {
            const HeightfieldFloor& hf = *sec.heightfield;
            w.write(hf.gridWidth);
            w.write(hf.gridDepth);
            w.writeVec2(hf.worldMin);
            w.writeVec2(hf.worldMax);
            const u32 sampleCount = hf.gridWidth * hf.gridDepth;
            w.write(sampleCount);
            for (u32 s = 0; s < sampleCount && s < static_cast<u32>(hf.samples.size()); ++s)
                w.write(hf.samples[s]);
        }

        // Ceiling shape + heightfield (optional)
        w.write(static_cast<u32>(sec.ceilingShape));
        const u32 hasCeilHF = (sec.ceilHeightfield.has_value() &&
                               !sec.ceilHeightfield->samples.empty()) ? 1u : 0u;
        std::printf("[dlevel] Sector %zu: ceilingShape=%u, hasCeilHF=%u, has_value=%d\n",
                    static_cast<std::size_t>(&sec - &pack.map.sectors[0]),
                    static_cast<u32>(sec.ceilingShape),
                    hasCeilHF,
                    sec.ceilHeightfield.has_value() ? 1 : 0);
        w.write(hasCeilHF);
        if (hasCeilHF)
        {
            const HeightfieldFloor& hf = *sec.ceilHeightfield;
            std::printf("[dlevel] Writing ceiling heightfield for sector (grid: %ux%u, samples: %zu)\n",
                        hf.gridWidth, hf.gridDepth, hf.samples.size());
            w.write(hf.gridWidth);
            w.write(hf.gridDepth);
            w.writeVec2(hf.worldMin);
            w.writeVec2(hf.worldMax);
            const u32 sampleCount = hf.gridWidth * hf.gridDepth;
            w.write(sampleCount);
            for (u32 s = 0; s < sampleCount && s < static_cast<u32>(hf.samples.size()); ++s)
                w.write(hf.samples[s]);
        }

        // Detail brushes
        w.write(static_cast<u32>(sec.details.size()));
        for (const DetailBrush& db : sec.details)
        {
            // mat4 transform (16 floats, column-major)
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    w.write(db.transform[col][row]);
            w.write(static_cast<u32>(db.type));
            // DetailBrushGeomParams (all fields, type-specific ones defaulted)
            w.write(db.geom.halfExtents.x); w.write(db.geom.halfExtents.y); w.write(db.geom.halfExtents.z);
            w.write(db.geom.slopeAxis);
            w.write(db.geom.radius);
            w.write(db.geom.height);
            w.write(db.geom.segmentCount);
            w.write(db.geom.spanWidth);
            w.write(db.geom.archHeight);
            w.write(db.geom.thickness);
            w.write(static_cast<u32>(db.geom.archProfile));
            w.write(db.geom.archSegments);
            w.writeUUID(db.geom.meshAssetId);
            w.writeUUID(db.materialId);
            w.write(static_cast<u32>(db.collidable   ? 1u : 0u));
            w.write(static_cast<u32>(db.castsShadow  ? 1u : 0u));
        }

        // Phase 1F per-wall extension (one block per wall, in wall order)
        for (const Wall& wall : sec.walls)
        {
            // Floor height override
            const u32 hasFloorOvr = wall.floorHeightOverride.has_value() ? 1u : 0u;
            w.write(hasFloorOvr);
            w.write(wall.floorHeightOverride.value_or(0.0f));
            // Ceiling height override
            const u32 hasCeilOvr = wall.ceilHeightOverride.has_value() ? 1u : 0u;
            w.write(hasCeilOvr);
            w.write(wall.ceilHeightOverride.value_or(0.0f));
            // Bezier curve control point A
            const u32 hasCurveA = wall.curveControlA.has_value() ? 1u : 0u;
            w.write(hasCurveA);
            w.writeVec2(wall.curveControlA.value_or(glm::vec2{0.0f, 0.0f}));
            // Bezier curve control point B
            const u32 hasCurveB = wall.curveControlB.has_value() ? 1u : 0u;
            w.write(hasCurveB);
            w.writeVec2(wall.curveControlB.value_or(glm::vec2{0.0f, 0.0f}));
            // Subdivisions
            w.write(wall.curveSubdivisions);
            // Back material (portal back face)
            w.writeUUID(wall.backMaterialId);
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

    // ── Render settings (v6) ─────────────────────────────────────────────────
    const LevelRenderSettings& rs = pack.renderSettings;
    
    // Fog
    w.write(static_cast<u32>(rs.fogEnabled ? 1u : 0u));
    w.write(rs.fogAmbientR); w.write(rs.fogAmbientG); w.write(rs.fogAmbientB);
    w.write(rs.fogDensity);
    w.write(rs.fogAnisotropy);  // was: 0.0f fogHeightFalloff — repurposed for anisotropy
    w.write(rs.fogScattering);
    w.write(rs.fogNear);
    w.write(rs.fogFar);
    
    // SSR
    w.write(static_cast<u32>(rs.ssrEnabled ? 1u : 0u));
    w.write(rs.ssrMaxDistance);
    w.write(rs.ssrThickness);
    w.write(rs.ssrMaxSteps);
    w.write(8u);  // ssrBinarySearchIterations — not in struct, write default
    
    // DoF
    w.write(static_cast<u32>(rs.dofEnabled ? 1u : 0u));
    w.write(rs.dofFocusDistance);
    w.write(rs.dofFocusRange);
    w.write(rs.dofBokehRadius);
    
    // Motion blur
    w.write(static_cast<u32>(rs.motionBlurEnabled ? 1u : 0u));
    w.write(rs.motionBlurShutterAngle);
    w.write(rs.motionBlurNumSamples);
    
    // Color grading
    w.write(static_cast<u32>(rs.colorGradingEnabled ? 1u : 0u));
    w.writeStr(rs.colorGradingLutPath);
    w.write(rs.colorGradingIntensity);
    
    // Bloom — not in struct, write defaults
    w.write(0u);  // bloomEnabled = false
    w.write(1.0f);  // bloomThreshold
    w.write(0.5f);  // bloomIntensity
    
    // Chromatic aberration
    w.write(static_cast<u32>(rs.optFxEnabled ? 1u : 0u));
    w.write(rs.optFxCaAmount);
    
    // Vignette
    w.write(static_cast<u32>(rs.optFxEnabled ? 1u : 0u));
    w.write(rs.optFxVignetteIntensity);
    w.write(rs.optFxVignetteRadius);
    
    // Film grain
    w.write(static_cast<u32>(rs.optFxEnabled ? 1u : 0u));
    w.write(rs.optFxGrainAmount);
    
    // FXAA
    w.write(static_cast<u32>(rs.fxaaEnabled ? 1u : 0u));
    w.write(0.75f);  // fxaaQualitySubpix — not in struct, write default
    w.write(0.125f);  // fxaaQualityEdgeThreshold — not in struct, write default
    w.write(0.0312f);  // fxaaQualityEdgeThresholdMin — not in struct, write default
    
    // Ray tracing
    w.write(static_cast<u32>(rs.rtEnabled ? 1u : 0u));
    w.write(rs.rtMaxBounces);
    w.write(rs.rtSamplesPerPixel);
    w.write(static_cast<u32>(rs.rtDenoise ? 1u : 0u));  // rtEnableGI mapped to denoise for now
    w.write(1.0f);  // rayTracingGIIntensity — not in struct, write default

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
    if (magic != k_MAGIC) { return std::unexpected(DlevelError::ParseError); }
    // Accept v5 through v9.
    if (version < 5u || version > 9u) { return std::unexpected(DlevelError::VersionMismatch); }

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

        // ─ Phase 1F sector extension (v8+; defaults applied for older versions) ─
        if (version >= 8u)
        {
            u32 floorShapeRaw = 0;
            if (!r.read(floorShapeRaw)) return std::unexpected(DlevelError::ParseError);
            sec.floorShape = static_cast<FloorShape>(floorShapeRaw);

            if (!r.read(sec.floorPortalSectorId) || !r.read(sec.ceilPortalSectorId))
                return std::unexpected(DlevelError::ParseError);
            if (!r.readUUID(sec.floorPortalMaterialId) ||
                !r.readUUID(sec.ceilPortalMaterialId))
                return std::unexpected(DlevelError::ParseError);

            // Stair profile
            u32 hasStair = 0;
            if (!r.read(hasStair)) return std::unexpected(DlevelError::ParseError);
            if (hasStair)
            {
                StairProfile sp;
                if (!r.read(sp.stepCount)      || !r.read(sp.riserHeight) ||
                    !r.read(sp.treadDepth)      || !r.read(sp.directionAngle))
                    return std::unexpected(DlevelError::ParseError);
                sec.stairProfile = sp;
            }

            // Floor heightfield
            u32 hasHF = 0;
            if (!r.read(hasHF)) return std::unexpected(DlevelError::ParseError);
            if (hasHF)
            {
                HeightfieldFloor hf;
                u32 sampleCount = 0;
                if (!r.read(hf.gridWidth) || !r.read(hf.gridDepth))
                    return std::unexpected(DlevelError::ParseError);
                if (!r.readVec2(hf.worldMin) || !r.readVec2(hf.worldMax))
                    return std::unexpected(DlevelError::ParseError);
                if (!r.read(sampleCount)) return std::unexpected(DlevelError::ParseError);
                hf.samples.resize(sampleCount);
                for (u32 s = 0; s < sampleCount; ++s)
                    if (!r.read(hf.samples[s])) return std::unexpected(DlevelError::ParseError);
                sec.heightfield = std::move(hf);
            }

            // Ceiling shape + heightfield (v9+)
            if (version >= 9u)
            {
                u32 ceilingShapeRaw = 0;
                if (!r.read(ceilingShapeRaw))
                {
                    std::fprintf(stderr, "[dlevel] Parse error: failed to read ceilingShape (sector %u)\n", si);
                    return std::unexpected(DlevelError::ParseError);
                }
                sec.ceilingShape = static_cast<CeilingShape>(ceilingShapeRaw);
                
                u32 hasCeilHF = 0;
                if (!r.read(hasCeilHF))
                {
                    std::fprintf(stderr, "[dlevel] Parse error: failed to read hasCeilHF flag (sector %u)\n", si);
                    return std::unexpected(DlevelError::ParseError);
                }
                if (hasCeilHF)
                {
                    HeightfieldFloor hf;
                    u32 sampleCount = 0;
                    if (!r.read(hf.gridWidth) || !r.read(hf.gridDepth))
                    {
                        std::fprintf(stderr, "[dlevel] Parse error: failed to read ceiling heightfield grid dimensions (sector %u)\n", si);
                        return std::unexpected(DlevelError::ParseError);
                    }
                    if (!r.readVec2(hf.worldMin) || !r.readVec2(hf.worldMax))
                    {
                        std::fprintf(stderr, "[dlevel] Parse error: failed to read ceiling heightfield world bounds (sector %u)\n", si);
                        return std::unexpected(DlevelError::ParseError);
                    }
                    if (!r.read(sampleCount))
                    {
                        std::fprintf(stderr, "[dlevel] Parse error: failed to read ceiling heightfield sampleCount (sector %u)\n", si);
                        return std::unexpected(DlevelError::ParseError);
                    }
                    hf.samples.resize(sampleCount);
                    for (u32 s = 0; s < sampleCount; ++s)
                    {
                        if (!r.read(hf.samples[s]))
                        {
                            std::fprintf(stderr, "[dlevel] Parse error: failed to read ceiling heightfield sample %u/%u (sector %u)\n", s, sampleCount, si);
                            return std::unexpected(DlevelError::ParseError);
                        }
                    }
                    sec.ceilHeightfield = std::move(hf);
                }
            }

            // Detail brushes
            u32 detailCount = 0;
            if (!r.read(detailCount)) return std::unexpected(DlevelError::ParseError);
            sec.details.resize(detailCount);
            for (u32 di = 0; di < detailCount; ++di)
            {
                DetailBrush& db = sec.details[di];
                // mat4 transform
                for (int col = 0; col < 4; ++col)
                    for (int row = 0; row < 4; ++row)
                        if (!r.read(db.transform[col][row]))
                            return std::unexpected(DlevelError::ParseError);
                u32 typeRaw = 0;
                if (!r.read(typeRaw)) return std::unexpected(DlevelError::ParseError);
                db.type = static_cast<DetailBrushType>(typeRaw);
                // DetailBrushGeomParams
                if (!r.read(db.geom.halfExtents.x) || !r.read(db.geom.halfExtents.y) ||
                    !r.read(db.geom.halfExtents.z)  || !r.read(db.geom.slopeAxis))
                    return std::unexpected(DlevelError::ParseError);
                if (!r.read(db.geom.radius)       || !r.read(db.geom.height)       ||
                    !r.read(db.geom.segmentCount)  || !r.read(db.geom.spanWidth))
                    return std::unexpected(DlevelError::ParseError);
                if (!r.read(db.geom.archHeight)   || !r.read(db.geom.thickness))
                    return std::unexpected(DlevelError::ParseError);
                u32 archProfileRaw = 0;
                if (!r.read(archProfileRaw) || !r.read(db.geom.archSegments))
                    return std::unexpected(DlevelError::ParseError);
                db.geom.archProfile = static_cast<ArchProfile>(archProfileRaw);
                if (!r.readUUID(db.geom.meshAssetId))
                    return std::unexpected(DlevelError::ParseError);
                if (!r.readUUID(db.materialId)) return std::unexpected(DlevelError::ParseError);
                u32 collidableRaw = 0, shadowRaw = 0;
                if (!r.read(collidableRaw) || !r.read(shadowRaw))
                    return std::unexpected(DlevelError::ParseError);
                db.collidable  = (collidableRaw != 0u);
                db.castsShadow = (shadowRaw     != 0u);
            }

            // Phase 1F per-wall extension
            for (u32 wi = 0; wi < wallCount; ++wi)
            {
                Wall& wall = sec.walls[wi];
                u32 hasFloorOvr = 0, hasCeilOvr = 0, hasCurveA = 0, hasCurveB = 0;
                float floorOvrVal = 0.0f, ceilOvrVal = 0.0f;
                glm::vec2 curveA{}, curveB{};
                u32 curveSubdivs = 12u;

                if (!r.read(hasFloorOvr) || !r.read(floorOvrVal)) return std::unexpected(DlevelError::ParseError);
                if (!r.read(hasCeilOvr)  || !r.read(ceilOvrVal))  return std::unexpected(DlevelError::ParseError);
                if (!r.read(hasCurveA)   || !r.readVec2(curveA))  return std::unexpected(DlevelError::ParseError);
                if (!r.read(hasCurveB)   || !r.readVec2(curveB))  return std::unexpected(DlevelError::ParseError);
                if (!r.read(curveSubdivs))                         return std::unexpected(DlevelError::ParseError);
                if (!r.readUUID(wall.backMaterialId))               return std::unexpected(DlevelError::ParseError);

                if (hasFloorOvr) wall.floorHeightOverride = floorOvrVal;
                if (hasCeilOvr)  wall.ceilHeightOverride  = ceilOvrVal;
                if (hasCurveA)   wall.curveControlA       = curveA;
                if (hasCurveB)   wall.curveControlB       = curveB;
                wall.curveSubdivisions = curveSubdivs;
            }
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

    // ── Render settings (v6 only; v5 uses defaults) ─────────────────────────────
    if (version >= 6u)
    {
        LevelRenderSettings& rs = pack.renderSettings;
        u32 u32_tmp = 0;
        float f32_tmp = 0.0f;

        // Fog
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        rs.fogEnabled = (u32_tmp != 0u);
        if (!r.read(rs.fogAmbientR) || !r.read(rs.fogAmbientG) || !r.read(rs.fogAmbientB) ||
            !r.read(rs.fogDensity))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(rs.fogAnisotropy))  // v6: was fogHeightFalloff; v7+: anisotropy
            return std::unexpected(DlevelError::ParseError);
        // v7 added fogScattering, fogNear, fogFar
        if (version >= 7u)
        {
            if (!r.read(rs.fogScattering) || !r.read(rs.fogNear) || !r.read(rs.fogFar))
                return std::unexpected(DlevelError::ParseError);
        }

        // SSR
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        rs.ssrEnabled = (u32_tmp != 0u);
        if (!r.read(rs.ssrMaxDistance) || !r.read(rs.ssrThickness) || !r.read(rs.ssrMaxSteps))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(u32_tmp))  // ssrBinarySearchIterations — not in struct, discard
            return std::unexpected(DlevelError::ParseError);

        // DoF
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        rs.dofEnabled = (u32_tmp != 0u);
        if (!r.read(rs.dofFocusDistance) || !r.read(rs.dofFocusRange) || !r.read(rs.dofBokehRadius))
            return std::unexpected(DlevelError::ParseError);

        // Motion blur
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        rs.motionBlurEnabled = (u32_tmp != 0u);
        if (!r.read(rs.motionBlurShutterAngle) || !r.read(rs.motionBlurNumSamples))
            return std::unexpected(DlevelError::ParseError);

        // Color grading
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        rs.colorGradingEnabled = (u32_tmp != 0u);
        if (!r.readStr(rs.colorGradingLutPath) || !r.read(rs.colorGradingIntensity))
            return std::unexpected(DlevelError::ParseError);

        // Bloom — not in struct, discard
        u32 bloomEnabled = 0;
        float bloomThreshold = 0.0f, bloomIntensity = 0.0f;
        if (!r.read(bloomEnabled) || !r.read(bloomThreshold) || !r.read(bloomIntensity))
            return std::unexpected(DlevelError::ParseError);

        // Chromatic aberration
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        rs.optFxEnabled = (u32_tmp != 0u);
        if (!r.read(rs.optFxCaAmount))
            return std::unexpected(DlevelError::ParseError);

        // Vignette
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        // Vignette enabled is folded into optFxEnabled (already set above)
        if (!r.read(rs.optFxVignetteIntensity) || !r.read(rs.optFxVignetteRadius))
            return std::unexpected(DlevelError::ParseError);

        // Film grain
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        // Film grain enabled is folded into optFxEnabled (already set above)
        if (!r.read(rs.optFxGrainAmount))
            return std::unexpected(DlevelError::ParseError);

        // FXAA
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        rs.fxaaEnabled = (u32_tmp != 0u);
        float fxaaSubpix = 0.0f, fxaaEdgeThresh = 0.0f, fxaaEdgeMin = 0.0f;
        if (!r.read(fxaaSubpix) || !r.read(fxaaEdgeThresh) || !r.read(fxaaEdgeMin))
            return std::unexpected(DlevelError::ParseError);
        // FXAA quality params not in struct, discarded

        // Ray tracing
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        rs.rtEnabled = (u32_tmp != 0u);
        if (!r.read(rs.rtMaxBounces) || !r.read(rs.rtSamplesPerPixel))
            return std::unexpected(DlevelError::ParseError);
        if (!r.read(u32_tmp)) return std::unexpected(DlevelError::ParseError);
        rs.rtDenoise = (u32_tmp != 0u);  // rtEnableGI mapped to denoise
        if (!r.read(f32_tmp))  // rayTracingGIIntensity — not in struct, discard
            return std::unexpected(DlevelError::ParseError);
    }
    // else: v5 files use default-initialized LevelRenderSettings (all fields default to 0/false)

    return pack;
}

} // namespace daedalus::world
