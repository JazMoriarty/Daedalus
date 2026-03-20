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

constexpr u32 k_JSON_VERSION = 2u;

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
