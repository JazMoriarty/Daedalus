// render_settings_data.h
// Flat, render-header-free mirror of SceneView post-processing sub-structs.
// Stored on EditMapDocument; copied to SceneView at frame time by Viewport3D.
// All values are plain floats/bools/ints so no render headers are required
// in this public editor interface.

#pragma once

#include <cstdint>
#include <string>

namespace daedalus::editor
{

// ─── VolumetricFogData ────────────────────────────────────────────────────────

struct VolumetricFogData
{
    bool  enabled    = false;
    float density    = 0.02f;   ///< Extinction coefficient (1/m).
    float anisotropy = 0.3f;    ///< H-G phase g factor (-1..1).
    float scattering = 0.8f;    ///< Single-scatter albedo (0..1).
    float fogNear    = 0.5f;    ///< Fog near depth limit (metres).
    float fogFar     = 80.0f;   ///< Fog far depth limit (metres).

    /// Ambient in-scatter colour (stored as separate floats — no render dep).
    float ambientFogR = 0.04f;
    float ambientFogG = 0.05f;
    float ambientFogB = 0.06f;
};

// ─── SSRData ──────────────────────────────────────────────────────────────────

struct SSRData
{
    bool     enabled         = false;
    float    maxDistance     = 20.0f;  ///< Max ray-march distance (metres).
    float    thickness       = 0.15f;  ///< Depth intersection tolerance (metres).
    float    roughnessCutoff = 0.6f;   ///< Skip SSR above this roughness.
    float    fadeStart       = 0.1f;   ///< Screen-edge UV distance to begin fading.
    uint32_t maxSteps        = 64u;    ///< Max ray-march iterations.
};

// ─── DoFData ──────────────────────────────────────────────────────────────────

struct DoFData
{
    bool  enabled        = false;
    float focusDistance  = 5.0f;   ///< World-space focus plane distance (metres).
    float focusRange     = 2.0f;   ///< Depth of the in-focus band (metres).
    float bokehRadius    = 8.0f;   ///< Maximum blur radius (pixels).
    float nearTransition = 1.0f;   ///< Near-field ramp distance (metres).
    float farTransition  = 3.0f;   ///< Far-field ramp distance (metres).
};

// ─── MotionBlurData ───────────────────────────────────────────────────────────

struct MotionBlurData
{
    bool     enabled      = false;
    float    shutterAngle = 0.5f;  ///< Fraction of frame time shutter is open (0..1).
    uint32_t numSamples   = 8u;    ///< Number of velocity-direction samples.
};

// ─── ColorGradingData ─────────────────────────────────────────────────────────

struct ColorGradingData
{
    bool  enabled   = false;
    float intensity = 1.0f;  ///< Blend weight (0 = passthrough, 1 = full LUT).
    /// Path to a 32×32×32 3D LUT image on disk.  Empty = use renderer identity LUT.
    /// Loaded at runtime by Viewport3D; not a GPU handle.
    std::string lutPath;
};

// ─── OptionalFxData ───────────────────────────────────────────────────────────

struct OptionalFxData
{
    bool  enabled           = false;
    float caAmount          = 0.0f;    ///< Chromatic aberration radius (0 = off).
    float vignetteIntensity = 0.30f;   ///< Vignette darkening strength (0..1).
    float vignetteRadius    = 0.40f;   ///< Vignette inner edge in UV² (lower = larger).
    float grainAmount       = 0.04f;   ///< Film grain amplitude (0 = off).
};

// ─── UpscalingData ────────────────────────────────────────────────────────────

struct UpscalingData
{
    bool fxaaEnabled = true;  ///< true = FXAA; false = UpscalingMode::None.
};

// ─── RenderSettingsData ───────────────────────────────────────────────────────
/// Aggregate of all post-processing settings for one map.
/// Serialised to the .emap sidecar (schema v2).

struct RenderSettingsData
{
    VolumetricFogData fog;
    SSRData           ssr;
    DoFData           dof;
    MotionBlurData    motionBlur;
    ColorGradingData  colorGrading;
    OptionalFxData    optionalFx;
    UpscalingData     upscaling;
};

} // namespace daedalus::editor
