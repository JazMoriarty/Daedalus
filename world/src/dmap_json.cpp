// dmap_json.cpp
// JSON .dmap.json serialisation / deserialisation via nlohmann/json.
//
// Intended for debugging and source-control diffs only. The binary .dmap
// format is used at runtime.
//
// JSON schema (abbreviated):
// {
//   "version": 1,
//   "name":    "...",
//   "author":  "...",
//   "global_ambient_color": [r, g, b],
//   "global_ambient_intensity": 1.0,
//   "sectors": [
//     {
//       "floor_height": 0.0,
//       "ceil_height":  4.0,
//       "floor_material": "hi:lo",
//       "ceil_material":  "hi:lo",
//       "ambient_color": [r, g, b],
//       "ambient_intensity": 1.0,
//       "flags": 0,
//       "walls": [
//         {
//           "p0": [x, z],
//           "flags": 0,
//           "portal_sector": -1,
//           "front_material": "hi:lo",
//           "upper_material": "hi:lo",
//           "lower_material": "hi:lo",
//           "uv_offset": [u, v],
//           "uv_scale":  [u, v],
//           "uv_rotation": 0.0
//         }
//       ]
//     }
//   ]
// }

#include "daedalus/world/dmap_io.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#include <nlohmann/json.hpp>
#pragma clang diagnostic pop

#include <fstream>
#include <iomanip>
#include <sstream>

namespace daedalus::world
{

namespace
{

constexpr u32 k_JSON_VERSION = 5u;

// ─── UUID helpers ─────────────────────────────────────────────────────────────

[[nodiscard]] std::string uuidToString(const UUID& id)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << id.hi << ':' << std::setw(16) << id.lo;
    return oss.str();
}

[[nodiscard]] UUID uuidFromString(const std::string& s)
{
    UUID id;
    const auto colon = s.find(':');
    if (colon == std::string::npos) { return id; }
    try
    {
        id.hi = std::stoull(s.substr(0, colon), nullptr, 16);
        id.lo = std::stoull(s.substr(colon + 1),  nullptr, 16);
    }
    catch (...) {}
    return id;
}

// ─── Serialise ────────────────────────────────────────────────────────────────

[[nodiscard]] nlohmann::json wallToJson(const Wall& w)
{
    nlohmann::json j = {
        {"p0",            {w.p0.x, w.p0.y}},
        {"flags",         static_cast<u32>(w.flags)},
        {"portal_sector", (w.portalSectorId == INVALID_SECTOR_ID)
                              ? -1
                              : static_cast<i64>(w.portalSectorId)},
        {"front_material", uuidToString(w.frontMaterialId)},
        {"upper_material", uuidToString(w.upperMaterialId)},
        {"lower_material", uuidToString(w.lowerMaterialId)},
        {"uv_offset",     {w.uvOffset.x,   w.uvOffset.y}},
        {"uv_scale",      {w.uvScale.x,    w.uvScale.y}},
        {"uv_rotation",    w.uvRotation},
    };
    // Phase 1F-A: emit only when set (omitted fields default to nullopt on load).
    if (w.floorHeightOverride) j["floor_height_override"] = *w.floorHeightOverride;
    if (w.ceilHeightOverride)  j["ceil_height_override"]  = *w.ceilHeightOverride;
    // Phase 1F-C: curve handles (omit when no curve set).
    if (w.curveControlA)
    {
        j["curve_control_a"]   = {w.curveControlA->x, w.curveControlA->y};
        j["curve_subdivisions"] = w.curveSubdivisions;
        if (w.curveControlB)
            j["curve_control_b"] = {w.curveControlB->x, w.curveControlB->y};
    }
    return j;
}

[[nodiscard]] nlohmann::json sectorToJson(const Sector& sec)
{
    nlohmann::json jw = nlohmann::json::array();
    for (const auto& w : sec.walls) { jw.push_back(wallToJson(w)); }

    nlohmann::json j = {
        {"floor_height",       sec.floorHeight},
        {"ceil_height",        sec.ceilHeight},
        {"floor_material",     uuidToString(sec.floorMaterialId)},
        {"ceil_material",      uuidToString(sec.ceilMaterialId)},
        {"ambient_color",      {sec.ambientColor.r, sec.ambientColor.g, sec.ambientColor.b}},
        {"ambient_intensity",  sec.ambientIntensity},
        {"flags",              static_cast<u32>(sec.flags)},
        {"floor_shape",        static_cast<u32>(sec.floorShape)},
        {"walls",              std::move(jw)},
    };
    // Phase 1F-A: emit stair profile only when present.
    if (sec.stairProfile)
    {
        j["stair_profile"] = {
            {"step_count",       sec.stairProfile->stepCount},
            {"riser_height",     sec.stairProfile->riserHeight},
            {"tread_depth",      sec.stairProfile->treadDepth},
            {"direction_angle",  sec.stairProfile->directionAngle},
        };
    }
    // Phase 1F-B: floor and ceiling portal fields (omit when not set).
    if (sec.floorPortalSectorId != INVALID_SECTOR_ID)
        j["floor_portal_sector"] = static_cast<i64>(sec.floorPortalSectorId);
    if (sec.ceilPortalSectorId != INVALID_SECTOR_ID)
        j["ceil_portal_sector"] = static_cast<i64>(sec.ceilPortalSectorId);
    if (sec.floorPortalSectorId != INVALID_SECTOR_ID)
        j["floor_portal_material"] = uuidToString(sec.floorPortalMaterialId);
    if (sec.ceilPortalSectorId != INVALID_SECTOR_ID)
        j["ceil_portal_material"] = uuidToString(sec.ceilPortalMaterialId);
    // Phase 1F-C: detail brushes (omit when empty).
    if (sec.heightfield)
    {
        const auto& hf = *sec.heightfield;
        nlohmann::json jh;
        jh["grid_width"]  = hf.gridWidth;
        jh["grid_depth"]  = hf.gridDepth;
        jh["world_min"]   = {hf.worldMin.x, hf.worldMin.y};
        jh["world_max"]   = {hf.worldMax.x, hf.worldMax.y};
        nlohmann::json js = nlohmann::json::array();
        for (const f32 s : hf.samples) js.push_back(s);
        jh["samples"] = std::move(js);
        j["heightfield"] = std::move(jh);
    }
    if (!sec.details.empty())
    {
        nlohmann::json jd = nlohmann::json::array();
        for (const auto& brush : sec.details)
        {
            nlohmann::json jb;
            // transform: flat array of 16 floats
            nlohmann::json jt = nlohmann::json::array();
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    jt.push_back(brush.transform[c][r]);
            jb["transform"]      = std::move(jt);
            jb["type"]           = static_cast<u32>(brush.type);
            jb["half_extents"]   = {brush.geom.halfExtents.x, brush.geom.halfExtents.y, brush.geom.halfExtents.z};
            jb["slope_axis"]     = brush.geom.slopeAxis;
            jb["radius"]         = brush.geom.radius;
            jb["height"]         = brush.geom.height;
            jb["segment_count"]  = brush.geom.segmentCount;
            jb["span_width"]     = brush.geom.spanWidth;
            jb["arch_height"]    = brush.geom.archHeight;
            jb["thickness"]      = brush.geom.thickness;
            jb["arch_profile"]   = static_cast<u32>(brush.geom.archProfile);
            jb["arch_segments"]  = brush.geom.archSegments;
            jb["mesh_asset_id"]  = uuidToString(brush.geom.meshAssetId);
            jb["material_id"]    = uuidToString(brush.materialId);
            jb["collidable"]     = brush.collidable;
            jb["cast_shadow"]    = brush.castsShadow;
            jd.push_back(std::move(jb));
        }
        j["details"] = std::move(jd);
    }
    return j;
}

[[nodiscard]] nlohmann::json mapToJson(const WorldMapData& map)
{
    nlohmann::json js = nlohmann::json::array();
    for (const auto& sec : map.sectors) { js.push_back(sectorToJson(sec)); }

    return {
        {"version", k_JSON_VERSION},
        {"name",    map.name},
        {"author",  map.author},
        {"global_ambient_color",     {map.globalAmbientColor.r,
                                      map.globalAmbientColor.g,
                                      map.globalAmbientColor.b}},
        {"global_ambient_intensity", map.globalAmbientIntensity},
        {"sectors", std::move(js)},
    };
}

// ─── Deserialise ──────────────────────────────────────────────────────────────

[[nodiscard]] bool wallFromJson(const nlohmann::json& jw, Wall& out)
{
    try
    {
        out.p0 = {jw["p0"][0].get<float>(), jw["p0"][1].get<float>()};
        out.flags = static_cast<WallFlags>(jw["flags"].get<u32>());

        const i64 ps = jw.value("portal_sector", i64{-1});
        out.portalSectorId = (ps < 0)
            ? INVALID_SECTOR_ID
            : static_cast<SectorId>(ps);

        out.frontMaterialId = uuidFromString(jw["front_material"].get<std::string>());
        out.upperMaterialId = uuidFromString(jw["upper_material"].get<std::string>());
        out.lowerMaterialId = uuidFromString(jw["lower_material"].get<std::string>());

        out.uvOffset   = {jw["uv_offset"][0].get<float>(),  jw["uv_offset"][1].get<float>()};
        out.uvScale    = {jw["uv_scale"][0].get<float>(),   jw["uv_scale"][1].get<float>()};
        out.uvRotation = jw["uv_rotation"].get<float>();
        // Phase 1F-A: optional height overrides (absent = nullopt).
        if (jw.contains("floor_height_override"))
            out.floorHeightOverride = jw["floor_height_override"].get<float>();
        if (jw.contains("ceil_height_override"))
            out.ceilHeightOverride = jw["ceil_height_override"].get<float>();
        // Phase 1F-C: curve handles.
        if (jw.contains("curve_control_a"))
        {
            out.curveControlA = glm::vec2{jw["curve_control_a"][0].get<float>(),
                                          jw["curve_control_a"][1].get<float>()};
            out.curveSubdivisions = jw.value("curve_subdivisions", out.curveSubdivisions);
            if (jw.contains("curve_control_b"))
                out.curveControlB = glm::vec2{jw["curve_control_b"][0].get<float>(),
                                              jw["curve_control_b"][1].get<float>()};
        }
        return true;
    }
    catch (...) { return false; }
}

[[nodiscard]] bool sectorFromJson(const nlohmann::json& jsec, Sector& out)
{
    try
    {
        out.floorHeight = jsec["floor_height"].get<float>();
        out.ceilHeight  = jsec["ceil_height"].get<float>();
        out.floorMaterialId = uuidFromString(jsec["floor_material"].get<std::string>());
        out.ceilMaterialId  = uuidFromString(jsec["ceil_material"].get<std::string>());
        out.ambientColor = {
            jsec["ambient_color"][0].get<float>(),
            jsec["ambient_color"][1].get<float>(),
            jsec["ambient_color"][2].get<float>()
        };
        out.ambientIntensity = jsec["ambient_intensity"].get<float>();
        out.flags = static_cast<SectorFlags>(jsec["flags"].get<u32>());
        // Phase 1F-A: floor shape and stair profile (absent = defaults).
        out.floorShape = static_cast<FloorShape>(
            jsec.value("floor_shape", static_cast<u32>(FloorShape::Flat)));
        if (jsec.contains("stair_profile"))
        {
            const auto& jsp = jsec["stair_profile"];
            StairProfile sp;
            sp.stepCount      = jsp.value("step_count",      sp.stepCount);
            sp.riserHeight    = jsp.value("riser_height",    sp.riserHeight);
            sp.treadDepth     = jsp.value("tread_depth",     sp.treadDepth);
            sp.directionAngle = jsp.value("direction_angle", sp.directionAngle);
            out.stairProfile  = sp;
        }
        // Phase 1F-B: floor and ceiling portal fields (absent = INVALID / null UUID).
        {
            const i64 fp = jsec.value("floor_portal_sector", i64{-1});
            out.floorPortalSectorId = (fp < 0) ? INVALID_SECTOR_ID
                                               : static_cast<SectorId>(fp);
        }
        {
            const i64 cp = jsec.value("ceil_portal_sector", i64{-1});
            out.ceilPortalSectorId = (cp < 0) ? INVALID_SECTOR_ID
                                              : static_cast<SectorId>(cp);
        }
        if (jsec.contains("floor_portal_material"))
            out.floorPortalMaterialId = uuidFromString(jsec["floor_portal_material"].get<std::string>());
        if (jsec.contains("ceil_portal_material"))
            out.ceilPortalMaterialId = uuidFromString(jsec["ceil_portal_material"].get<std::string>());
        // Phase 1F-C: detail brushes.
        // Phase 1F-D: heightfield.
        if (jsec.contains("heightfield"))
        {
            const auto& jh = jsec["heightfield"];
            HeightfieldFloor hf;
            hf.gridWidth = jh.value("grid_width", 2u);
            hf.gridDepth = jh.value("grid_depth", 2u);
            if (jh.contains("world_min"))
                hf.worldMin = {jh["world_min"][0].get<float>(), jh["world_min"][1].get<float>()};
            if (jh.contains("world_max"))
                hf.worldMax = {jh["world_max"][0].get<float>(), jh["world_max"][1].get<float>()};
            if (jh.contains("samples"))
            {
                const auto& js = jh["samples"];
                hf.samples.reserve(js.size());
                for (const auto& sv : js) hf.samples.push_back(sv.get<float>());
            }
            out.heightfield = std::move(hf);
        }
        if (jsec.contains("details"))
        {
            for (const auto& jb : jsec["details"])
            {
                DetailBrush brush;
                // transform
                const auto& jt = jb["transform"];
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        brush.transform[c][r] = jt[c*4+r].get<float>();
                brush.type = static_cast<DetailBrushType>(
                    jb.value("type", static_cast<u32>(DetailBrushType::Box)));
                if (jb.contains("half_extents"))
                    brush.geom.halfExtents = {jb["half_extents"][0].get<float>(),
                                              jb["half_extents"][1].get<float>(),
                                              jb["half_extents"][2].get<float>()};
                brush.geom.slopeAxis    = jb.value("slope_axis",    brush.geom.slopeAxis);
                brush.geom.radius       = jb.value("radius",        brush.geom.radius);
                brush.geom.height       = jb.value("height",        brush.geom.height);
                brush.geom.segmentCount = jb.value("segment_count", brush.geom.segmentCount);
                brush.geom.spanWidth    = jb.value("span_width",    brush.geom.spanWidth);
                brush.geom.archHeight   = jb.value("arch_height",   brush.geom.archHeight);
                brush.geom.thickness    = jb.value("thickness",     brush.geom.thickness);
                brush.geom.archProfile  = static_cast<ArchProfile>(
                    jb.value("arch_profile", static_cast<u32>(brush.geom.archProfile)));
                brush.geom.archSegments = jb.value("arch_segments",  brush.geom.archSegments);
                if (jb.contains("mesh_asset_id"))
                    brush.geom.meshAssetId = uuidFromString(jb["mesh_asset_id"].get<std::string>());
                if (jb.contains("material_id"))
                    brush.materialId = uuidFromString(jb["material_id"].get<std::string>());
                brush.collidable  = jb.value("collidable",  brush.collidable);
                brush.castsShadow = jb.value("cast_shadow", brush.castsShadow);
                out.details.push_back(std::move(brush));
            }
        }
        for (const auto& jw : jsec["walls"])
        {
            Wall w;
            if (!wallFromJson(jw, w)) { return false; }
            out.walls.push_back(std::move(w));
        }
        return true;
    }
    catch (...) { return false; }
}

} // anonymous namespace

// ─── saveDmapJson ─────────────────────────────────────────────────────────────

std::expected<void, DmapError> saveDmapJson(const WorldMapData&         map,
                                              const std::filesystem::path& path)
{
    std::ofstream ofs(path);
    if (!ofs.is_open()) { return std::unexpected(DmapError::WriteError); }

    try
    {
        ofs << mapToJson(map).dump(4) << '\n';
    }
    catch (...)
    {
        return std::unexpected(DmapError::WriteError);
    }

    if (!ofs) { return std::unexpected(DmapError::WriteError); }
    return {};
}

// ─── loadDmapJson ─────────────────────────────────────────────────────────────

std::expected<WorldMapData, DmapError> loadDmapJson(const std::filesystem::path& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) { return std::unexpected(DmapError::FileNotFound); }

    nlohmann::json root;
    try
    {
        ifs >> root;
    }
    catch (...)
    {
        return std::unexpected(DmapError::ParseError);
    }

    try
    {
        const u32 version = root["version"].get<u32>();
        if (version > k_JSON_VERSION)
        {
            return std::unexpected(DmapError::VersionMismatch);
        }

        WorldMapData map;
        map.name   = root["name"].get<std::string>();
        map.author = root["author"].get<std::string>();
        map.globalAmbientColor = {
            root["global_ambient_color"][0].get<float>(),
            root["global_ambient_color"][1].get<float>(),
            root["global_ambient_color"][2].get<float>()
        };
        map.globalAmbientIntensity = root["global_ambient_intensity"].get<float>();

        for (const auto& jsec : root["sectors"])
        {
            Sector sec;
            if (!sectorFromJson(jsec, sec))
            {
                return std::unexpected(DmapError::ParseError);
            }
            map.sectors.push_back(std::move(sec));
        }

        return map;
    }
    catch (...)
    {
        return std::unexpected(DmapError::ParseError);
    }
}

} // namespace daedalus::world
