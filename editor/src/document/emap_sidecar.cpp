// emap_sidecar.cpp
// JSON serialisation of editor-only EditMapDocument state.
//
// JSON schema (version 4):
// {
//   "version": 2,
//   "scene": {
//     "sun_direction":  [x, y, z],
//     "sun_color":      [r, g, b],
//     "sun_intensity":  2.0,
//     "ambient_color":  [r, g, b]
//   },
//   "map_defaults": {
//     "floor_height": 0.0,
//     "ceil_height":  4.0,
//     "gravity":      9.81,
//     "sky_path":     ""
//   },
//   "player_start": null,
//   // or:
//   "player_start": { "position": [x, y, z], "yaw": 0.0 },
//   "lights": [
//     { "type": 0, "position": [x,y,z], "color": [r,g,b],
//       "radius": 10.0, "intensity": 2.0 },
//     // Spot lights additionally carry:
//     { "type": 1, ..., "direction": [x,y,z],
//       "inner_cone_angle": 0.26, "outer_cone_angle": 0.52, "range": 20.0 }
//   ],
//   "entities": [
//     {
//       "visual_type":  0,
//       "position":     [x, y, z],
//       "yaw":          0.0,
//       "scale":        [1, 1, 1],
//       "asset_path":   "",
//       "tint":         [r, g, b, a],
//       "layer_index":  0,
//       // AnimSettings (always persisted, relevant for AnimatedBillboard):
//       "pitch": 0.0, "roll": 0.0,
//       "entity_name": "", "alignment_mode": 0,
//       "anim_frame_count": 1, "anim_cols": 1, "anim_rows": 1, "anim_frame_rate": 8.0,
//       // DecalMaterialParams (always persisted, relevant for Decal):
//       "decal_normal_path": "", "decal_roughness": 0.5,
//       "decal_metalness": 0.0, "decal_opacity": 1.0,
//       // ParticleEmitterParams (always persisted, relevant for ParticleEmitter):
//       // Component stubs (always persisted):
//       "physics_shape": 0, "physics_is_static": true, "physics_mass": 1.0,
//       "script_path": "",
//       "script_exposed_vars": { "key": "value" },
//       "audio_sound_path": "", "audio_falloff_radius": 10.0, "audio_loop": false,
//       "audio_volume": 1.0, "audio_auto_play": true,
//       // ParticleEmitterParams:
//       "particle_emission_rate": 10.0, "particle_emit_dir": [0,1,0],
//       "particle_cone_half_angle": 0.26, "particle_speed_min": 1.0,
//       "particle_speed_max": 3.0, "particle_lifetime_min": 1.0,
//       "particle_lifetime_max": 3.0,
//       "particle_color_start": [r,g,b,a], "particle_color_end": [r,g,b,a],
//       "particle_size_start": 0.1, "particle_size_end": 0.05,
//       "particle_drag": 0.0, "particle_gravity": [0,-9.81,0]
//     }
//   ],
//   "layers": [
//     { "name": "Default", "visible": true, "locked": false }
//   ],
//   "active_layer": 0,
//   "sector_layers": [0, 0, 1, ...],
//   "prefabs": [
//     {
//       "name": "small_room",
//       "sectors": [
//         {
//           "floor_height": 0.0, "ceil_height": 4.0,
//           "floor_mat_hi": 0, "floor_mat_lo": 0,
//           "ceil_mat_hi":  0, "ceil_mat_lo":  0,
//           "ambient_color": [r, g, b], "ambient_intensity": 1.0,
//           "sector_flags": 0,
//           "walls": [
//             { "p0": [x, z], "wall_flags": 0,
//               "front_mat_hi": 0, "front_mat_lo": 0,
//               "upper_mat_hi": 0, "upper_mat_lo": 0,
//               "lower_mat_hi": 0, "lower_mat_lo": 0,
//               "back_mat_hi":  0, "back_mat_lo":  0,
//               "uv_offset": [u, v], "uv_scale": [u, v], "uv_rotation": 0.0 }
//           ]
//         }
//       ],
//       "entities": [ ... ]  // same format as root "entities" array
//     }
//   ]
// }

#include "emap_sidecar.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/entity_def.h"
#include "daedalus/editor/prefab_def.h"
#include "daedalus/editor/render_settings_data.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#include <nlohmann/json.hpp>
#pragma clang diagnostic pop

#include <fstream>

namespace daedalus::editor
{

namespace
{

constexpr int k_VERSION = 8;

// ─── Write helpers ────────────────────────────────────────────────────────────

nlohmann::json vec3ToJson(const glm::vec3& v)
{
    return {v.x, v.y, v.z};
}

nlohmann::json vec4ToJson(const glm::vec4& v)
{
    return {v.x, v.y, v.z, v.w};
}

// ─── Read helpers ─────────────────────────────────────────────────────────────

[[nodiscard]] glm::vec3 vec3FromJson(const nlohmann::json& j)
{
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

[[nodiscard]] glm::vec4 vec4FromJson(const nlohmann::json& j)
{
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

// ─── Vec2 helpers ─────────────────────────────────────────────────────────────

nlohmann::json vec2ToJson(const glm::vec2& v)
{
    return {v.x, v.y};
}

[[nodiscard]] glm::vec2 vec2FromJson(const nlohmann::json& j)
{
    return {j[0].get<float>(), j[1].get<float>()};
}

// ─── EntityDef helpers ────────────────────────────────────────────────────────

[[nodiscard]] nlohmann::json entityToJson(const EntityDef& ed)
{
    return {
        {"visual_type",    static_cast<uint32_t>(ed.visualType)},
        {"position",       vec3ToJson(ed.position)},
        {"yaw",            ed.yaw},
        {"pitch",          ed.pitch},
        {"roll",           ed.roll},
        {"entity_name",    ed.entityName},
        {"alignment_mode", static_cast<uint32_t>(ed.alignmentMode)},
        {"scale",          vec3ToJson(ed.scale)},
        {"asset_path",     ed.assetPath},
        {"tint",           vec4ToJson(ed.tint)},
        {"layer_index",    ed.layerIndex},
        // AnimSettings
        {"anim_frame_count", ed.anim.frameCount},
        {"anim_cols",        ed.anim.cols},
        {"anim_rows",        ed.anim.rows},
        {"anim_frame_rate",  ed.anim.frameRate},
        // RotatedSpriteSettings
        {"rs_direction_count", ed.rotatedSprite.directionCount},
        {"rs_anim_rows",       ed.rotatedSprite.animRows},
        {"rs_anim_cols",       ed.rotatedSprite.animCols},
        {"rs_frame_rate",      ed.rotatedSprite.frameRate},
        // DecalMaterialParams
        {"decal_normal_path", ed.decalMat.normalPath},
        {"decal_roughness",   ed.decalMat.roughness},
        {"decal_metalness",   ed.decalMat.metalness},
        {"decal_opacity",     ed.decalMat.opacity},
        {"decal_z_index",     ed.decalMat.zIndex},
        // Component stubs
        {"physics_shape",        static_cast<uint32_t>(ed.physics.shape)},
        {"physics_is_static",    ed.physics.isStatic},
        {"physics_mass",         ed.physics.mass},
        {"script_path",          ed.script.scriptPath},
        {"script_exposed_vars",  [&]{
            nlohmann::json obj = nlohmann::json::object();
            for (const auto& [k, v] : ed.script.exposedVars)
                obj[k] = v;
            return obj;
        }()},
        {"audio_sound_path",     ed.audio.soundPath},
        {"audio_falloff_radius", ed.audio.falloffRadius},
        {"audio_loop",           ed.audio.loop},
        {"audio_volume",         ed.audio.volume},
        {"audio_auto_play",      ed.audio.autoPlay},
        // ParticleEmitterParams
        {"particle_emission_rate",   ed.particle.emissionRate},
        {"particle_emit_dir",        vec3ToJson(ed.particle.emitDir)},
        {"particle_cone_half_angle", ed.particle.coneHalfAngle},
        {"particle_speed_min",       ed.particle.speedMin},
        {"particle_speed_max",       ed.particle.speedMax},
        {"particle_lifetime_min",    ed.particle.lifetimeMin},
        {"particle_lifetime_max",    ed.particle.lifetimeMax},
        {"particle_color_start",     vec4ToJson(ed.particle.colorStart)},
        {"particle_color_end",       vec4ToJson(ed.particle.colorEnd)},
        {"particle_size_start",      ed.particle.sizeStart},
        {"particle_size_end",        ed.particle.sizeEnd},
        {"particle_drag",            ed.particle.drag},
        {"particle_gravity",         vec3ToJson(ed.particle.gravity)},
        {"particle_soft_range",      ed.particle.softRange},
        {"particle_emissive_start",  ed.particle.emissiveStart},
        {"particle_emissive_end",    ed.particle.emissiveEnd},
        {"particle_emits_light",       ed.particle.emitsLight},
        {"particle_emit_light_radius", ed.particle.emitLightRadius},
        {"particle_shadow_density",    ed.particle.shadowDensity},
    };
}

[[nodiscard]] EntityDef entityFromJson(const nlohmann::json& je)
{
    EntityDef ed;
    ed.visualType    = static_cast<EntityVisualType>(je["visual_type"].get<uint32_t>());
    ed.position      = vec3FromJson(je["position"]);
    ed.yaw           = je["yaw"].get<float>();
    ed.pitch         = je["pitch"].get<float>();
    ed.roll          = je["roll"].get<float>();
    ed.entityName    = je["entity_name"].get<std::string>();
    ed.alignmentMode = static_cast<EntityAlignment>(je["alignment_mode"].get<uint32_t>());
    ed.scale         = vec3FromJson(je["scale"]);
    ed.assetPath   = je["asset_path"].get<std::string>();
    ed.tint        = vec4FromJson(je["tint"]);
    ed.layerIndex  = je["layer_index"].get<uint32_t>();
    // AnimSettings
    ed.anim.frameCount = je["anim_frame_count"].get<uint32_t>();
    ed.anim.cols       = je["anim_cols"].get<uint32_t>();
    ed.anim.rows       = je["anim_rows"].get<uint32_t>();
    ed.anim.frameRate  = je["anim_frame_rate"].get<float>();
    // RotatedSpriteSettings
    ed.rotatedSprite.directionCount = je["rs_direction_count"].get<uint32_t>();
    ed.rotatedSprite.animRows       = je["rs_anim_rows"].get<uint32_t>();
    ed.rotatedSprite.animCols       = je["rs_anim_cols"].get<uint32_t>();
    ed.rotatedSprite.frameRate      = je["rs_frame_rate"].get<float>();
    // DecalMaterialParams
    ed.decalMat.normalPath = je["decal_normal_path"].get<std::string>();
    ed.decalMat.roughness  = je["decal_roughness"].get<float>();
    ed.decalMat.metalness  = je["decal_metalness"].get<float>();
    ed.decalMat.opacity    = je["decal_opacity"].get<float>();
    ed.decalMat.zIndex     = je.contains("decal_z_index")
                             ? je["decal_z_index"].get<int>() : 0;
    // Component stubs
    ed.physics.shape        = static_cast<CollisionShape>(je["physics_shape"].get<uint32_t>());
    ed.physics.isStatic     = je["physics_is_static"].get<bool>();
    ed.physics.mass         = je["physics_mass"].get<float>();
    ed.script.scriptPath    = je["script_path"].get<std::string>();
    if (je.contains("script_exposed_vars") && je["script_exposed_vars"].is_object())
        for (const auto& [k, v] : je["script_exposed_vars"].items())
            ed.script.exposedVars.emplace(k, v.get<std::string>());
    ed.audio.soundPath      = je["audio_sound_path"].get<std::string>();
    ed.audio.falloffRadius  = je["audio_falloff_radius"].get<float>();
    ed.audio.loop           = je["audio_loop"].get<bool>();
    ed.audio.volume         = je.contains("audio_volume")    ? je["audio_volume"].get<float>()  : 1.0f;
    ed.audio.autoPlay       = je.contains("audio_auto_play") ? je["audio_auto_play"].get<bool>() : true;
    // ParticleEmitterParams
    ed.particle.emissionRate   = je["particle_emission_rate"].get<float>();
    ed.particle.emitDir        = vec3FromJson(je["particle_emit_dir"]);
    ed.particle.coneHalfAngle  = je["particle_cone_half_angle"].get<float>();
    ed.particle.speedMin       = je["particle_speed_min"].get<float>();
    ed.particle.speedMax       = je["particle_speed_max"].get<float>();
    ed.particle.lifetimeMin    = je["particle_lifetime_min"].get<float>();
    ed.particle.lifetimeMax    = je["particle_lifetime_max"].get<float>();
    ed.particle.colorStart     = vec4FromJson(je["particle_color_start"]);
    ed.particle.colorEnd       = vec4FromJson(je["particle_color_end"]);
    ed.particle.sizeStart      = je["particle_size_start"].get<float>();
    ed.particle.sizeEnd        = je["particle_size_end"].get<float>();
    ed.particle.drag           = je["particle_drag"].get<float>();
    ed.particle.gravity        = vec3FromJson(je["particle_gravity"]);
    ed.particle.softRange      = je.contains("particle_soft_range")
                                 ? je["particle_soft_range"].get<float>() : 0.0f;
    ed.particle.emissiveStart  = je.contains("particle_emissive_start")
                                 ? je["particle_emissive_start"].get<float>() : 1.0f;
    ed.particle.emissiveEnd    = je.contains("particle_emissive_end")
                                 ? je["particle_emissive_end"].get<float>()   : 0.0f;
    ed.particle.emitsLight       = je.contains("particle_emits_light")
                                   ? je["particle_emits_light"].get<bool>()         : false;
    ed.particle.emitLightRadius  = je.contains("particle_emit_light_radius")
                                   ? je["particle_emit_light_radius"].get<float>()  : 15.0f;
    ed.particle.shadowDensity    = je.contains("particle_shadow_density")
                                   ? je["particle_shadow_density"].get<float>()     : 0.0f;
    return ed;
}

// ─── Sector / prefab helpers ──────────────────────────────────────────────────

[[nodiscard]] nlohmann::json wallToJson(const world::Wall& w)
{
    return {
        {"p0",           vec2ToJson(w.p0)},
        {"wall_flags",   static_cast<uint32_t>(w.flags)},
        // portal links are always stripped in prefab sectors
        {"front_mat_hi", w.frontMaterialId.hi},
        {"front_mat_lo", w.frontMaterialId.lo},
        {"upper_mat_hi", w.upperMaterialId.hi},
        {"upper_mat_lo", w.upperMaterialId.lo},
        {"lower_mat_hi", w.lowerMaterialId.hi},
        {"lower_mat_lo", w.lowerMaterialId.lo},
        {"back_mat_hi",  w.backMaterialId.hi},
        {"back_mat_lo",  w.backMaterialId.lo},
        {"uv_offset",    vec2ToJson(w.uvOffset)},
        {"uv_scale",     vec2ToJson(w.uvScale)},
        {"uv_rotation",  w.uvRotation},
    };
}

[[nodiscard]] world::Wall wallFromJson(const nlohmann::json& jw)
{
    world::Wall w;
    w.p0                   = vec2FromJson(jw["p0"]);
    w.flags                = static_cast<world::WallFlags>(jw["wall_flags"].get<uint32_t>());
    w.portalSectorId       = world::INVALID_SECTOR_ID;  // always stripped
    w.frontMaterialId.hi   = jw["front_mat_hi"].get<uint64_t>();
    w.frontMaterialId.lo   = jw["front_mat_lo"].get<uint64_t>();
    w.upperMaterialId.hi   = jw["upper_mat_hi"].get<uint64_t>();
    w.upperMaterialId.lo   = jw["upper_mat_lo"].get<uint64_t>();
    w.lowerMaterialId.hi   = jw["lower_mat_hi"].get<uint64_t>();
    w.lowerMaterialId.lo   = jw["lower_mat_lo"].get<uint64_t>();
    w.backMaterialId.hi    = jw["back_mat_hi"].get<uint64_t>();
    w.backMaterialId.lo    = jw["back_mat_lo"].get<uint64_t>();
    w.uvOffset             = vec2FromJson(jw["uv_offset"]);
    w.uvScale              = vec2FromJson(jw["uv_scale"]);
    w.uvRotation           = jw["uv_rotation"].get<float>();
    return w;
}

[[nodiscard]] nlohmann::json sectorToJson(const world::Sector& s)
{
    nlohmann::json jwalls = nlohmann::json::array();
    for (const auto& w : s.walls)
        jwalls.push_back(wallToJson(w));

    return {
        {"floor_height",       s.floorHeight},
        {"ceil_height",        s.ceilHeight},
        {"floor_mat_hi",       s.floorMaterialId.hi},
        {"floor_mat_lo",       s.floorMaterialId.lo},
        {"ceil_mat_hi",        s.ceilMaterialId.hi},
        {"ceil_mat_lo",        s.ceilMaterialId.lo},
        {"ambient_color",      vec3ToJson(s.ambientColor)},
        {"ambient_intensity",  s.ambientIntensity},
        {"sector_flags",       static_cast<uint32_t>(s.flags)},
        {"floor_uv_offset",    vec2ToJson(s.floorUvOffset)},
        {"floor_uv_scale",     vec2ToJson(s.floorUvScale)},
        {"floor_uv_rotation",  s.floorUvRotation},
        {"ceil_uv_offset",     vec2ToJson(s.ceilUvOffset)},
        {"ceil_uv_scale",      vec2ToJson(s.ceilUvScale)},
        {"ceil_uv_rotation",   s.ceilUvRotation},
        {"walls",              std::move(jwalls)},
    };
}

[[nodiscard]] world::Sector sectorFromJson(const nlohmann::json& js)
{
    world::Sector s;
    s.floorHeight          = js["floor_height"].get<float>();
    s.ceilHeight           = js["ceil_height"].get<float>();
    s.floorMaterialId.hi   = js["floor_mat_hi"].get<uint64_t>();
    s.floorMaterialId.lo   = js["floor_mat_lo"].get<uint64_t>();
    s.ceilMaterialId.hi    = js["ceil_mat_hi"].get<uint64_t>();
    s.ceilMaterialId.lo    = js["ceil_mat_lo"].get<uint64_t>();
    s.ambientColor         = vec3FromJson(js["ambient_color"]);
    s.ambientIntensity     = js["ambient_intensity"].get<float>();
    s.flags                = static_cast<world::SectorFlags>(js["sector_flags"].get<uint32_t>());
    if (js.contains("floor_uv_offset"))   s.floorUvOffset   = vec2FromJson(js["floor_uv_offset"]);
    if (js.contains("floor_uv_scale"))    s.floorUvScale    = vec2FromJson(js["floor_uv_scale"]);
    if (js.contains("floor_uv_rotation")) s.floorUvRotation = js["floor_uv_rotation"].get<float>();
    if (js.contains("ceil_uv_offset"))    s.ceilUvOffset    = vec2FromJson(js["ceil_uv_offset"]);
    if (js.contains("ceil_uv_scale"))     s.ceilUvScale     = vec2FromJson(js["ceil_uv_scale"]);
    if (js.contains("ceil_uv_rotation"))  s.ceilUvRotation  = js["ceil_uv_rotation"].get<float>();
    for (const auto& jw : js["walls"])
        s.walls.push_back(wallFromJson(jw));
    return s;
}

} // anonymous namespace

// ─── saveEmap ─────────────────────────────────────────────────────────────────

std::expected<void, EmapError>
saveEmap(const EditMapDocument& doc, const std::filesystem::path& emapPath)
{
    nlohmann::json root;
    root["version"] = k_VERSION;

    // Scene settings.
    const SceneSettings& ss = doc.sceneSettings();
    root["scene"] = {
        {"sun_direction", vec3ToJson(ss.sunDirection)},
        {"sun_color",     vec3ToJson(ss.sunColor)},
        {"sun_intensity", ss.sunIntensity},
        {"ambient_color",     vec3ToJson(doc.mapData().globalAmbientColor)},
        {"ambient_intensity", doc.mapData().globalAmbientIntensity},
    };

    // Map defaults.
    root["map_defaults"] = {
        {"floor_height", doc.defaultFloorHeight()},
        {"ceil_height",  doc.defaultCeilHeight()},
        {"gravity",      doc.gravity()},
        {"sky_path",     doc.skyPath()},
    };

    // Player start.
    if (const auto& ps = doc.playerStart(); ps.has_value())
    {
        root["player_start"] = {
            {"position", {ps->position.x, ps->position.y, ps->position.z}},
            {"yaw",      ps->yaw},
        };
    }
    else
    {
        root["player_start"] = nullptr;
    }

    // Lights.
    {
        nlohmann::json jlights = nlohmann::json::array();
        for (const LightDef& ld : doc.lights())
        {
            nlohmann::json jl = {
                {"type",      static_cast<uint32_t>(ld.type)},
                {"position",  {ld.position.x, ld.position.y, ld.position.z}},
                {"color",     {ld.color.r,    ld.color.g,    ld.color.b}},
                {"radius",    ld.radius},
                {"intensity", ld.intensity},
            };
            if (ld.type == LightType::Spot)
            {
                jl["direction"]       = {ld.direction.x, ld.direction.y, ld.direction.z};
                jl["inner_cone_angle"] = ld.innerConeAngle;
                jl["outer_cone_angle"] = ld.outerConeAngle;
                jl["range"]            = ld.range;
            }
            jlights.push_back(std::move(jl));
        }
        root["lights"] = std::move(jlights);
    }

    // Entities.
    {
        nlohmann::json jentities = nlohmann::json::array();
        for (const EntityDef& ed : doc.entities())
            jentities.push_back(entityToJson(ed));
        root["entities"] = std::move(jentities);
    }

    // Layers.
    {
        nlohmann::json jlayers = nlohmann::json::array();
        for (const EditorLayer& layer : doc.layers())
        {
            jlayers.push_back({
                {"name",    layer.name},
                {"visible", layer.visible},
                {"locked",  layer.locked},
            });
        }
        root["layers"]       = std::move(jlayers);
        root["active_layer"] = doc.activeLayerIdx();
    }

    // Sector layer assignments.
    {
        nlohmann::json jsl = nlohmann::json::array();
        const std::size_t nSectors = doc.mapData().sectors.size();
        for (std::size_t i = 0; i < nSectors; ++i)
            jsl.push_back(doc.sectorLayerIndex(i));
        root["sector_layers"] = std::move(jsl);
    }

    // Render settings (v2).
    {
        const RenderSettingsData& rs = doc.renderSettings();
        root["render_settings"] = {
            // Fog
            {"fog_enabled",    rs.fog.enabled},
            {"fog_density",    rs.fog.density},
            {"fog_anisotropy", rs.fog.anisotropy},
            {"fog_scattering", rs.fog.scattering},
            {"fog_near",       rs.fog.fogNear},
            {"fog_far",        rs.fog.fogFar},
            {"fog_amb_r",      rs.fog.ambientFogR},
            {"fog_amb_g",      rs.fog.ambientFogG},
            {"fog_amb_b",      rs.fog.ambientFogB},
            // SSR
            {"ssr_enabled",          rs.ssr.enabled},
            {"ssr_max_dist",          rs.ssr.maxDistance},
            {"ssr_thickness",         rs.ssr.thickness},
            {"ssr_roughness_cutoff",  rs.ssr.roughnessCutoff},
            {"ssr_fade_start",        rs.ssr.fadeStart},
            {"ssr_max_steps",         rs.ssr.maxSteps},
            // DoF
            {"dof_enabled",        rs.dof.enabled},
            {"dof_focus_dist",     rs.dof.focusDistance},
            {"dof_focus_range",    rs.dof.focusRange},
            {"dof_bokeh_radius",   rs.dof.bokehRadius},
            {"dof_near_trans",     rs.dof.nearTransition},
            {"dof_far_trans",      rs.dof.farTransition},
            // Motion blur
            {"mb_enabled",       rs.motionBlur.enabled},
            {"mb_shutter_angle", rs.motionBlur.shutterAngle},
            {"mb_num_samples",   rs.motionBlur.numSamples},
            // Colour grading
            {"cg_enabled",   rs.colorGrading.enabled},
            {"cg_intensity", rs.colorGrading.intensity},
            {"cg_lut_path",  rs.colorGrading.lutPath},
            // Optional FX
            {"fx_enabled",           rs.optionalFx.enabled},
            {"fx_ca_amount",          rs.optionalFx.caAmount},
            {"fx_vignette_intensity", rs.optionalFx.vignetteIntensity},
            {"fx_vignette_radius",    rs.optionalFx.vignetteRadius},
            {"fx_grain_amount",       rs.optionalFx.grainAmount},
            // Upscaling
            {"up_fxaa", rs.upscaling.fxaaEnabled},
            // Ray tracing (v5)
            {"rt_enabled",  rs.rt.enabled},
            {"rt_bounces",  rs.rt.maxBounces},
            {"rt_spp",      rs.rt.samplesPerPixel},
            {"rt_denoise",  rs.rt.denoise},
        };
    }

    // Asset root path for MaterialCatalog.
    root["asset_root"] = doc.assetRoot();

    // Viewport camera state (v6).
    if (const auto& cam = doc.viewportCamera(); cam.has_value())
    {
        root["viewport_camera"] = {
            {"eye",   vec3ToJson(cam->eye)},
            {"yaw",   cam->yaw},
            {"pitch", cam->pitch},
        };
    }

    // Prefab library.
    {
        nlohmann::json jprefabs = nlohmann::json::array();
        for (const PrefabDef& pf : doc.prefabs())
        {
            nlohmann::json jsectors = nlohmann::json::array();
            for (const world::Sector& s : pf.sectors)
                jsectors.push_back(sectorToJson(s));

            nlohmann::json jentities = nlohmann::json::array();
            for (const EntityDef& ed : pf.entities)
                jentities.push_back(entityToJson(ed));

            jprefabs.push_back({
                {"name",     pf.name},
                {"sectors",  std::move(jsectors)},
                {"entities", std::move(jentities)},
            });
        }
        root["prefabs"] = std::move(jprefabs);
    }

    // Write to disk.
    std::ofstream ofs(emapPath);
    if (!ofs.is_open())
        return std::unexpected(EmapError::WriteError);

    try
    {
        ofs << root.dump(4) << '\n';
    }
    catch (...)
    {
        return std::unexpected(EmapError::WriteError);
    }

    if (!ofs)
        return std::unexpected(EmapError::WriteError);

    return {};
}

// ─── loadEmap ─────────────────────────────────────────────────────────────────

std::expected<void, EmapError>
loadEmap(EditMapDocument& doc, const std::filesystem::path& emapPath)
{
    std::ifstream ifs(emapPath);
    if (!ifs.is_open())
        return std::unexpected(EmapError::ParseError);  // caller guards file-exists

    nlohmann::json root;
    try
    {
        ifs >> root;
    }
    catch (...)
    {
        return std::unexpected(EmapError::ParseError);
    }

    try
    {
        // Version guard: accept any version up to the current schema version.
        // Fields added in later versions are guarded by contains() below, so
        // older files load cleanly with defaults for any new fields.
        const int version = root["version"].get<int>();
        if (version < 1 || version > k_VERSION)
            return std::unexpected(EmapError::ParseError);

        // Scene settings.
        if (root.contains("scene"))
        {
            const auto& js        = root["scene"];
            SceneSettings& ss     = doc.sceneSettings();
            ss.sunDirection  = vec3FromJson(js["sun_direction"]);
            ss.sunColor      = vec3FromJson(js["sun_color"]);
            ss.sunIntensity  = js["sun_intensity"].get<float>();
            doc.mapData().globalAmbientColor = vec3FromJson(js["ambient_color"]);
            if (js.contains("ambient_intensity"))
                doc.mapData().globalAmbientIntensity = js["ambient_intensity"].get<float>();
        }

        // Render settings (v2; silently skipped when loading v1).
        if (root.contains("render_settings"))
        {
            const auto& jr = root["render_settings"];
            RenderSettingsData& rs = doc.renderSettings();

            // Fog.
            if (jr.contains("fog_enabled"))    rs.fog.enabled       = jr["fog_enabled"].get<bool>();
            if (jr.contains("fog_density"))    rs.fog.density       = jr["fog_density"].get<float>();
            if (jr.contains("fog_anisotropy")) rs.fog.anisotropy    = jr["fog_anisotropy"].get<float>();
            if (jr.contains("fog_scattering")) rs.fog.scattering    = jr["fog_scattering"].get<float>();
            if (jr.contains("fog_near"))       rs.fog.fogNear       = jr["fog_near"].get<float>();
            if (jr.contains("fog_far"))        rs.fog.fogFar        = jr["fog_far"].get<float>();
            if (jr.contains("fog_amb_r"))      rs.fog.ambientFogR   = jr["fog_amb_r"].get<float>();
            if (jr.contains("fog_amb_g"))      rs.fog.ambientFogG   = jr["fog_amb_g"].get<float>();
            if (jr.contains("fog_amb_b"))      rs.fog.ambientFogB   = jr["fog_amb_b"].get<float>();

            // SSR.
            if (jr.contains("ssr_enabled"))         rs.ssr.enabled         = jr["ssr_enabled"].get<bool>();
            if (jr.contains("ssr_max_dist"))         rs.ssr.maxDistance     = jr["ssr_max_dist"].get<float>();
            if (jr.contains("ssr_thickness"))        rs.ssr.thickness       = jr["ssr_thickness"].get<float>();
            if (jr.contains("ssr_roughness_cutoff")) rs.ssr.roughnessCutoff = jr["ssr_roughness_cutoff"].get<float>();
            if (jr.contains("ssr_fade_start"))       rs.ssr.fadeStart       = jr["ssr_fade_start"].get<float>();
            if (jr.contains("ssr_max_steps"))        rs.ssr.maxSteps        = jr["ssr_max_steps"].get<uint32_t>();

            // DoF.
            if (jr.contains("dof_enabled"))      rs.dof.enabled        = jr["dof_enabled"].get<bool>();
            if (jr.contains("dof_focus_dist"))   rs.dof.focusDistance  = jr["dof_focus_dist"].get<float>();
            if (jr.contains("dof_focus_range"))  rs.dof.focusRange     = jr["dof_focus_range"].get<float>();
            if (jr.contains("dof_bokeh_radius")) rs.dof.bokehRadius    = jr["dof_bokeh_radius"].get<float>();
            if (jr.contains("dof_near_trans"))   rs.dof.nearTransition = jr["dof_near_trans"].get<float>();
            if (jr.contains("dof_far_trans"))    rs.dof.farTransition  = jr["dof_far_trans"].get<float>();

            // Motion blur.
            if (jr.contains("mb_enabled"))       rs.motionBlur.enabled      = jr["mb_enabled"].get<bool>();
            if (jr.contains("mb_shutter_angle")) rs.motionBlur.shutterAngle = jr["mb_shutter_angle"].get<float>();
            if (jr.contains("mb_num_samples"))   rs.motionBlur.numSamples   = jr["mb_num_samples"].get<uint32_t>();

            // Colour grading.
            if (jr.contains("cg_enabled"))   rs.colorGrading.enabled   = jr["cg_enabled"].get<bool>();
            if (jr.contains("cg_intensity")) rs.colorGrading.intensity = jr["cg_intensity"].get<float>();
            if (jr.contains("cg_lut_path"))  rs.colorGrading.lutPath   = jr["cg_lut_path"].get<std::string>();

            // Optional FX.
            if (jr.contains("fx_enabled"))           rs.optionalFx.enabled           = jr["fx_enabled"].get<bool>();
            if (jr.contains("fx_ca_amount"))          rs.optionalFx.caAmount          = jr["fx_ca_amount"].get<float>();
            if (jr.contains("fx_vignette_intensity")) rs.optionalFx.vignetteIntensity = jr["fx_vignette_intensity"].get<float>();
            if (jr.contains("fx_vignette_radius"))    rs.optionalFx.vignetteRadius    = jr["fx_vignette_radius"].get<float>();
            if (jr.contains("fx_grain_amount"))       rs.optionalFx.grainAmount       = jr["fx_grain_amount"].get<float>();

            // Upscaling.
            if (jr.contains("up_fxaa")) rs.upscaling.fxaaEnabled = jr["up_fxaa"].get<bool>();

            // Ray tracing (v5).
            if (jr.contains("rt_enabled"))  rs.rt.enabled         = jr["rt_enabled"].get<bool>();
            if (jr.contains("rt_bounces"))  rs.rt.maxBounces      = jr["rt_bounces"].get<uint32_t>();
            if (jr.contains("rt_spp"))      rs.rt.samplesPerPixel = jr["rt_spp"].get<uint32_t>();
            if (jr.contains("rt_denoise"))  rs.rt.denoise         = jr["rt_denoise"].get<bool>();
        }

        // Asset root.
        if (root.contains("asset_root"))
            doc.setAssetRoot(root["asset_root"].get<std::string>());

        // Map defaults.
        if (root.contains("map_defaults"))
        {
            const auto& jd = root["map_defaults"];
            doc.setDefaultFloorHeight(jd["floor_height"].get<float>());
            doc.setDefaultCeilHeight (jd["ceil_height"].get<float>());
            doc.setGravity           (jd["gravity"].get<float>());
            doc.setSkyPath           (jd["sky_path"].get<std::string>());
        }

        // Player start.
        if (root.contains("player_start") && !root["player_start"].is_null())
        {
            const auto& jps = root["player_start"];
            PlayerStart ps;
            ps.position = vec3FromJson(jps["position"]);
            ps.yaw      = jps["yaw"].get<float>();
            doc.setPlayerStart(ps);
        }

        // Lights.
        if (root.contains("lights"))
        {
            doc.lights().clear();
            for (const auto& jl : root["lights"])
            {
                LightDef ld;
                ld.type      = static_cast<LightType>(jl["type"].get<uint32_t>());
                ld.position  = vec3FromJson(jl["position"]);
                ld.color     = vec3FromJson(jl["color"]);
                ld.radius    = jl["radius"].get<float>();
                ld.intensity = jl["intensity"].get<float>();
                if (ld.type == LightType::Spot)
                {
                    if (jl.contains("direction"))
                        ld.direction = vec3FromJson(jl["direction"]);
                    if (jl.contains("inner_cone_angle"))
                        ld.innerConeAngle = jl["inner_cone_angle"].get<float>();
                    if (jl.contains("outer_cone_angle"))
                        ld.outerConeAngle = jl["outer_cone_angle"].get<float>();
                    if (jl.contains("range"))
                        ld.range = jl["range"].get<float>();
                }
                doc.lights().push_back(ld);
            }
        }

        // Layers.
        if (root.contains("layers"))
        {
            doc.layers().clear();
            for (const auto& jl : root["layers"])
            {
                EditorLayer layer;
                layer.name    = jl["name"].get<std::string>();
                layer.visible = jl["visible"].get<bool>();
                layer.locked  = jl["locked"].get<bool>();
                doc.layers().push_back(std::move(layer));
            }
            // Ensure at least one layer.
            if (doc.layers().empty())
                doc.layers().push_back(EditorLayer{"Default", true, false});

            if (root.contains("active_layer"))
                doc.setActiveLayerIdx(root["active_layer"].get<uint32_t>());
        }

        // Entities.
        if (root.contains("entities"))
        {
            doc.entities().clear();
            for (const auto& je : root["entities"])
                doc.entities().push_back(entityFromJson(je));
        }

        // Prefab library.
        if (root.contains("prefabs"))
        {
            doc.prefabs().clear();
            for (const auto& jp : root["prefabs"])
            {
                PrefabDef pf;
                pf.name = jp["name"].get<std::string>();
                if (jp.contains("sectors"))
                    for (const auto& js : jp["sectors"])
                        pf.sectors.push_back(sectorFromJson(js));
                if (jp.contains("entities"))
                    for (const auto& je : jp["entities"])
                        pf.entities.push_back(entityFromJson(je));
                doc.prefabs().push_back(std::move(pf));
            }
        }

        // Viewport camera state (v6).
        if (root.contains("viewport_camera"))
        {
            const auto& jc = root["viewport_camera"];
            EditMapDocument::ViewportCameraState cam;
            if (jc.contains("eye"))   cam.eye   = vec3FromJson(jc["eye"]);
            if (jc.contains("yaw"))   cam.yaw   = jc["yaw"].get<float>();
            if (jc.contains("pitch")) cam.pitch = jc["pitch"].get<float>();
            doc.setViewportCamera(cam);
        }

        // Sector layer assignments — stored as a parallel vector rebuilt here.
        if (root.contains("sector_layers"))
        {
            // Access the private vector via the public accessor pattern:
            // appendSectorLayer() only appends the active layer, so we rebuild
            // manually by calling the exposed sectorLayerIndex write path.
            // Since sectorLayers is not directly mutable via the public API,
            // we repopulate via the resetSectorLayers helper (added below).
            const auto& jsl = root["sector_layers"];
            std::vector<uint32_t> sl;
            sl.reserve(jsl.size());
            for (const auto& entry : jsl)
                sl.push_back(entry.get<uint32_t>());
            doc.resetSectorLayers(std::move(sl));
        }
    }
    catch (...)
    {
        return std::unexpected(EmapError::ParseError);
    }

    return {};
}

} // namespace daedalus::editor
