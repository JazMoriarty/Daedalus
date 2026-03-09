// scene_view.h
// Per-frame scene description passed from the application to FrameRenderer.

#pragma once

#include "daedalus/core/types.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/particle_pool.h"
#include "daedalus/render/scene_data.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace daedalus::render
{

// ─── Material ─────────────────────────────────────────────────────────────────
// PBR material properties for a single draw call.
// All texture pointers are non-owning; the owner is responsible for lifetime.
// nullptr textures fall back to 1×1 engine-owned defaults in FrameRenderer.

struct Material
{
    rhi::ITexture* albedo    = nullptr;  ///< sRGB albedo map.  nullptr → opaque white.
    rhi::ITexture* normalMap = nullptr;  ///< Tangent-space normal map. nullptr → flat (0,0,1).
    rhi::ITexture* emissive  = nullptr;  ///< Linear emissive map.     nullptr → black.
    f32 roughness = 0.5f;               ///< Scalar override (0 = mirror, 1 = fully rough).
    f32 metalness = 0.0f;               ///< Scalar override (0 = dielectric, 1 = metal).

    /// True when this draw is the planar mirror surface quad.  The G-buffer fragment
    /// shader reconstructs per-pixel world position and reprojects it through
    /// FrameGPU::mirrorViewProj to obtain the correct UV into the mirror render target.
    /// Only valid for draws in SceneView::meshDraws (not transparentDraws).
    bool isMirrorSurface = false;

    /// Per-draw albedo tint (rgb) and opacity multiplier (a).
    /// Ignored by the opaque G-buffer shader; consumed by the transparent forward shader.
    /// Default: opaque white (no tint, no opacity change).
    glm::vec4 tint = glm::vec4(1.0f);

    /// Sprite sheet UV crop applied in both the G-buffer and transparent shaders.
    /// uv_sampled = in.uv * uvScale + uvOffset
    /// Static geometry leaves these at their defaults (full texture, no offset).
    glm::vec2 uvOffset = glm::vec2(0.0f);  ///< UV origin of the active frame cell.
    glm::vec2 uvScale  = glm::vec2(1.0f);  ///< UV size of one frame cell.
};

// ─── MeshDraw ─────────────────────────────────────────────────────────────────
// A single draw call: geometry pointers + per-instance transform + material.

struct MeshDraw
{
    rhi::IBuffer*  vertexBuffer = nullptr;         ///< Interleaved vertices (stride 48B)
    rhi::IBuffer*  indexBuffer  = nullptr;         ///< u32 indices
    u32            indexCount   = 0;

    glm::mat4      modelMatrix  = glm::mat4(1.0f);
    glm::mat4      prevModel    = glm::mat4(1.0f); ///< For TAA motion vectors

    Material       material;                       ///< PBR material for this draw
};

// ─── PointLight ───────────────────────────────────────────────────────────────

struct PointLight
{
    glm::vec3 position;
    f32       radius    = 1.0f;
    glm::vec3 color     = glm::vec3(1.0f);
    f32       intensity = 1.0f;
};

// ─── SpotLight ────────────────────────────────────────────────────────────────
// A cone-shaped light that casts a shadow map.

struct SpotLight
{
    glm::vec3 position;
    glm::vec3 direction;                          ///< Normalised, points into cone
    f32       innerConeAngle = glm::radians(15.0f); ///< radians — full brightness inside
    f32       outerConeAngle = glm::radians(30.0f); ///< radians — falloff to zero at edge
    f32       range          = 10.0f;
    glm::vec3 color          = glm::vec3(1.0f);
    f32       intensity      = 10.0f;
    /// When true this light is eligible to claim the single shadow map.
    /// The nearest eligible light to the camera wins each frame.
    /// When false the light contributes radiance but never casts shadows.
    bool      castsShadows   = true;
};

// ─── DecalDraw ────────────────────────────────────────────────────────────────
// A single deferred decal draw: an OBB defined by its model matrix plus the
// textures and scalars needed to blend into G-buffer RT0 and RT1.

struct DecalDraw
{
    /// model  : local unit-cube → world space  (vertex stage: positions the OBB)
    /// invModel: world → local unit-cube        (fragment stage: bounds test + UV)
    glm::mat4 modelMatrix    = glm::mat4(1.0f);
    glm::mat4 invModelMatrix = glm::mat4(1.0f);

    rhi::ITexture* albedoTexture = nullptr;  ///< RGBA; alpha = blend weight.  Never null.
    rhi::ITexture* normalTexture = nullptr;  ///< Optional tangent-space normal map.

    f32 roughness = 0.5f;
    f32 metalness = 0.0f;
    f32 opacity   = 1.0f;  ///< Global fade multiplier applied on top of texture alpha.
};

// ─── VolumetricFogParams ────────────────────────────────────────────────────────────────────
// Per-frame volumetric fog parameters.  When enabled is false FrameRenderer skips
// all three fog compute passes with no GPU cost.

struct VolumetricFogParams
{
    bool      enabled    = false;                            ///< Skip fog passes when false.
    float     density    = 0.02f;                            ///< Extinction coefficient (1/m).
    float     anisotropy = 0.3f;                             ///< H-G phase g factor (-1..1).
    float     scattering = 0.8f;                             ///< Single-scatter albedo (0..1).
    float     fogFar     = 80.0f;                            ///< Fog far depth limit (metres).
    float     fogNear    = 0.5f;                             ///< Fog near depth limit (metres).
    glm::vec3 ambientFog = glm::vec3(0.04f, 0.05f, 0.06f);  ///< Ambient in-scatter colour.
};

// ─── ParticleEmitterDraw ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Per-emitter data submitted to FrameRenderer each frame.
// pool and atlasTexture are non-owning; the application owns their lifetime.

struct ParticleEmitterDraw
{
    ParticlePool*   pool         = nullptr;  ///< GPU buffers for this emitter.  Never null.
    rhi::ITexture*  atlasTexture = nullptr;  ///< Sprite sheet atlas (RGBA8Unorm).  Never null.

    /// Fully packed constants uploaded to all particle shaders this frame.
    /// particleRenderSystem() fills this from the ECS component + TransformComponent.
    ParticleEmitterConstantsGPU constants;
};

// ─── DoFParams ────────────────────────────────────────────────────────────────────────────────────────────────
// Per-frame depth-of-field parameters.  When enabled is false FrameRenderer skips
// all three DoF compute passes with no GPU cost.

struct DoFParams
{
    bool  enabled        = false;   ///< Skip DoF passes when false.
    float focusDistance  = 5.0f;   ///< World-space focus plane distance (metres).
    float focusRange     = 2.0f;   ///< Depth of the in-focus band (metres).
    float bokehRadius    = 8.0f;   ///< Maximum blur radius (pixels).
    float nearTransition = 1.0f;   ///< Near-field ramp distance (metres).
    float farTransition  = 3.0f;   ///< Far-field ramp distance (metres).
};

// ─── MotionBlurParams ─────────────────────────────────────────────────────────────────────────────────────────
// Per-frame motion blur parameters.  When enabled is false FrameRenderer skips
// the motion blur compute pass with no GPU cost.

struct MotionBlurParams
{
    bool  enabled      = false;   ///< Skip motion blur pass when false.
    float shutterAngle = 0.5f;    ///< Fraction of frame time the shutter is open (0..1).
    u32   numSamples   = 8u;      ///< Number of velocity-direction samples.
};

// ─── ColorGradingParams ──────────────────────────────────────────────────────────────────────────────────────
// Per-frame LUT-based colour grading parameters.  When enabled is false FrameRenderer
// routes Tonemap directly to the swapchain with no GPU cost.

struct ColorGradingParams
{
    bool           enabled    = false;    ///< Skip colour grading pass when false.
    float          intensity  = 1.0f;    ///< Blend weight (0 = passthrough, 1 = full LUT).
    rhi::ITexture* lutTexture = nullptr; ///< 32×32×32 RGBA8Unorm 3D LUT; nullptr → engine identity.
};

// ─── OptionalFxParams ─────────────────────────────────────────────────────────────────────────────
// Per-frame optional post-FX parameters.  When enabled is false FrameRenderer skips
// the pass with zero GPU cost.

struct OptionalFxParams
{
    bool  enabled           = false;   ///< Skip pass when false.
    float caAmount          = 0.0f;   ///< Chromatic aberration radius (0 = off, 0.01 = strong).
    float vignetteIntensity = 0.30f;  ///< Vignette darkening strength (0..1).
    float vignetteRadius    = 0.40f;  ///< Vignette inner edge in UV² (lower = larger vignette).
    float grainAmount       = 0.04f;  ///< Film grain amplitude (0 = off, 0.05 = subtle).
};

// ─── UpscalingMode ─────────────────────────────────────────────────────────────────────────────────────

enum class UpscalingMode : u32
{
    None = 0,  ///< No AA upscaling pass — output is raw tonemap/CG output.
    FXAA = 1,  ///< Fast approximate anti-aliasing (9-tap screen-space edge smooth).
};

// ─── UpscalingParams ─────────────────────────────────────────────────────────────────────────────────────
// Per-frame upscaling / anti-aliasing mode.  When mode is None, FrameRenderer skips
// the FXAA pass with zero GPU cost.

struct UpscalingParams
{
    UpscalingMode mode = UpscalingMode::FXAA;  ///< Default: FXAA enabled.
};

// ─── MirrorDraw ────────────────────────────────────────────────────────────────────────────────────────────────
// Describes a single planar mirror surface for the mirror pre-pass.
//
// The application is responsible for:
//   1. Pre-allocating renderTarget (BGRA8Unorm, RenderTarget|ShaderRead, rtWidth×rtHeight).
//   2. Each frame: populating reflectedView, reflectedProj, and reflectedDraws.
//   3. Each frame: adding the mirror surface MeshDraw to SceneView::meshDraws
//      with material.albedo pointing to renderTarget.
//
// FrameRenderer::renderMirrorPrepass() renders reflectedDraws from the reflected
// camera into renderTarget before the main G-buffer pass, so the mirror surface
// reads a freshly-rendered reflection when the main G-buffer runs.
//
// Note: for correct winding in the reflected view, FrameRenderer uses a dedicated
// G-buffer PSO with CullMode::None.

struct MirrorDraw
{
    glm::mat4 reflectedView = glm::mat4(1.0f);  ///< Pre-computed reflected view matrix.
    glm::mat4 reflectedProj = glm::mat4(1.0f);  ///< Projection for the reflected view.
    rhi::ITexture* renderTarget = nullptr;  ///< Pre-allocated BGRA8Unorm render target.
    u32 rtWidth  = 512u;                    ///< Width of renderTarget.
    u32 rtHeight = 512u;                    ///< Height of renderTarget.
    std::vector<MeshDraw> reflectedDraws;  ///< Geometry rendered from the reflected POV.
    // Mirror surface MeshDraw must be added to SceneView::meshDraws by the application,
    // with material.albedo pointing to renderTarget.
};

// ─── SceneView ────────────────────────────────────────────────────────────────────────────────────────────────
// Complete frame description: camera + lights + draw list.

struct SceneView
{
    // ─── Camera ───────────────────────────────────────────────────────────────

    glm::mat4 view     = glm::mat4(1.0f);
    glm::mat4 proj     = glm::mat4(1.0f);
    glm::mat4 prevView = glm::mat4(1.0f);  ///< Previous frame view (TAA)
    glm::mat4 prevProj = glm::mat4(1.0f);  ///< Previous frame proj (TAA)

    glm::vec3 cameraPos = glm::vec3(0.0f);
    glm::vec3 cameraDir = glm::vec3(0.0f, 0.0f, -1.0f);

    // ─── Sun / directional light ──────────────────────────────────────────────

    glm::vec3 sunDirection = glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));
    glm::vec3 sunColor     = glm::vec3(1.0f, 0.95f, 0.8f);
    f32       sunIntensity = 3.0f;
    glm::vec3 ambientColor = glm::vec3(0.05f, 0.05f, 0.08f);

    // ─── Point lights ─────────────────────────────────────────────────────────

    std::vector<PointLight> pointLights;

    // ─── Spot lights ───────────────────────────────────────────────────────────────
    // All spot lights contribute radiance.  The nearest one with castsShadows=true
    // claims the single shadow map each frame (sorted to index 0 by FrameRenderer).

    std::vector<SpotLight> spotLights;

    // ─── Mesh draw list ─────────────────────────────────────────────────
    // Populated by the application each frame.  FrameRenderer iterates this
    // list for both the shadow depth pass and the G-buffer pass.

    std::vector<MeshDraw> meshDraws;

    // ─── Transparent draw list ─────────────────────────────────────────────────────────
    // Alpha-blended draws rendered in the forward transparency pass (Pass 6).
    // Must be sorted back-to-front with sortTransparentDraws() before submission
    // to FrameRenderer.  Not submitted to the shadow or G-buffer passes.

    std::vector<MeshDraw> transparentDraws;

    // ─── Decal draw list ───────────────────────────────────────────────────────────────────────────────────────────────
    // Populated by decalRenderSystem() each frame.  FrameRenderer renders these
    // in Pass 2.5 (between G-buffer and SSAO) by alpha-blending into G-buffer
    // RT0 (albedo) and RT1 (normal/roughness/metalness).

    std::vector<DecalDraw> decalDraws;

    // ─── Particle emitter draw list ─────────────────────────────────────────────────────────────────────────────────────
    // Populated by particleRenderSystem() each frame.  FrameRenderer runs the
    // emit→simulate→sort→compact compute chain and the draw pass for each entry.

    std::vector<ParticleEmitterDraw> particleEmitters;

// ─── SSRParams ─────────────────────────────────────────────────────────────────────
// Per-frame screen-space reflection parameters.  When enabled is false,
// FrameRenderer skips the SSR pass entirely and Bloom/Tonemap read the
// raw TAA output with zero GPU cost.

struct SSRParams
{
    bool  enabled         = false;   ///< Skip SSR pass when false.
    float maxDistance     = 20.0f;   ///< Max ray march distance (metres).
    float thickness       = 0.15f;   ///< Depth intersection tolerance (metres).
    float roughnessCutoff = 0.6f;    ///< Skip SSR above this roughness value.
    float fadeStart       = 0.1f;    ///< Screen-edge UV distance at which to begin fading.
    u32   maxSteps        = 64u;     ///< Maximum ray-march iterations.
};

// ─── VolumetricFogParams
    // Populated by the application each frame.  FrameRenderer runs the scatter,
    // integrate, and composite compute passes only when fog.enabled is true.

    VolumetricFogParams fog;

    // ─── Screen-space reflections
    // Populated by the application each frame.  FrameRenderer runs the SSR
    // compute pass only when ssr.enabled is true.  Bloom and tonemap always
    // consume the post-SSR colour (or the TAA output directly when disabled).

    SSRParams ssr;

    // ─── Depth of field
    // FrameRenderer runs the three DoF compute passes (CoC, blur, composite)
    // only when dof.enabled is true.

    DoFParams dof;

    // ─── Motion blur
    // FrameRenderer runs the motion blur compute pass only when
    // motionBlur.enabled is true.

    MotionBlurParams motionBlur;

    // ─── Colour grading
    // When enabled, FrameRenderer routes Tonemap to an intermediate texture
    // and applies a 3D LUT grade before writing the swapchain output.

    ColorGradingParams colorGrading;

    // ─── Optional post-FX (vignette, film grain, chromatic aberration)
    // When enabled, FrameRenderer runs the optional_fx_frag pass after tonemap/CG.
    // Skipped with zero GPU cost when enabled is false.

    OptionalFxParams optionalFx;

    // ─── Upscaling / anti-aliasing
    // When mode == UpscalingMode::FXAA, FrameRenderer applies FXAA as the final pass.
    // mode == UpscalingMode::None skips the FXAA pass (zero GPU cost).

    UpscalingParams upscaling;

    // ─── Mirror draws
    // Populated by the application each frame.  FrameRenderer renders each mirror's
    // reflectedDraws into its renderTarget before the main G-buffer pass.
    // The mirror surface MeshDraw must also be added to meshDraws by the application.

    std::vector<MirrorDraw> mirrors;

    // ─── Timing

    f32 time      = 0.0f;
    f32 deltaTime = 0.0f;
    u32 frameIndex = 0;
};

// ─── sortTransparentDraws
// Sort SceneView::transparentDraws back-to-front (farthest first) by squared
// distance from scene.cameraPos.  Must be called after all transparent draws
// are appended and before FrameRenderer::renderFrame.
//
// Position is taken from the translation column of each draw's modelMatrix,
// which is exact for billboard sprites and a good approximation for meshes.
//
// @param scene  SceneView whose transparentDraws list is sorted in place.
inline void sortTransparentDraws(SceneView& scene) noexcept
{
    const glm::vec3 cam = scene.cameraPos;
    std::sort(scene.transparentDraws.begin(), scene.transparentDraws.end(),
        [&cam](const MeshDraw& a, const MeshDraw& b) noexcept
        {
            const glm::vec3 pa = glm::vec3(a.modelMatrix[3]);
            const glm::vec3 pb = glm::vec3(b.modelMatrix[3]);
            const float da = glm::dot(pa - cam, pa - cam);
            const float db = glm::dot(pb - cam, pb - cam);
            return da > db;  // back-to-front: farthest draw first
        });
}

} // namespace daedalus::render
