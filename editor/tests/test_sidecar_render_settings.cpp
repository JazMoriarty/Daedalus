// test_sidecar_render_settings.cpp
// Roundtrip tests for RenderSettingsData serialization in emap_sidecar v2.

#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/render_settings_data.h"
#include "document/emap_sidecar.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace daedalus::editor;

// ─── Helpers ─────────────────────────────────────────────────────────────────

struct TempEmapFile
{
    std::filesystem::path path;
    explicit TempEmapFile(const char* name)
        : path(std::filesystem::temp_directory_path() / name) {}
    ~TempEmapFile() { std::filesystem::remove(path); }
};

// ─── Default values survive roundtrip ────────────────────────────────────────

TEST(EmapRenderSettings, DefaultsRoundtrip)
{
    TempEmapFile tmp("test_rs_defaults.emap");

    EditMapDocument docOut;
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    const RenderSettingsData& rs = docIn.renderSettings();

    // Fog defaults.
    EXPECT_EQ  (rs.fog.enabled,    false);
    EXPECT_FLOAT_EQ(rs.fog.density,    0.02f);
    EXPECT_FLOAT_EQ(rs.fog.fogNear,    0.5f);
    EXPECT_FLOAT_EQ(rs.fog.fogFar,     80.0f);

    // SSR defaults.
    EXPECT_EQ  (rs.ssr.enabled,    false);
    EXPECT_EQ  (rs.ssr.maxSteps,   64u);

    // DoF defaults.
    EXPECT_EQ  (rs.dof.enabled,    false);
    EXPECT_FLOAT_EQ(rs.dof.focusDistance, 5.0f);

    // Colour grading.
    EXPECT_EQ  (rs.colorGrading.enabled,  false);
    EXPECT_EQ  (rs.colorGrading.lutPath,  std::string{});

    // FXAA on by default.
    EXPECT_EQ  (rs.upscaling.fxaaEnabled, true);
}

// ─── Fog settings roundtrip ───────────────────────────────────────────────────

TEST(EmapRenderSettings, FogRoundtrip)
{
    TempEmapFile tmp("test_rs_fog.emap");

    EditMapDocument docOut;
    {
        VolumetricFogData& fog  = docOut.renderSettings().fog;
        fog.enabled    = true;
        fog.density    = 0.05f;
        fog.anisotropy = 0.7f;
        fog.scattering = 0.9f;
        fog.fogNear    = 1.0f;
        fog.fogFar     = 120.0f;
        fog.ambientFogR = 0.1f;
        fog.ambientFogG = 0.2f;
        fog.ambientFogB = 0.3f;
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    const VolumetricFogData& fog = docIn.renderSettings().fog;
    EXPECT_EQ      (fog.enabled,    true);
    EXPECT_FLOAT_EQ(fog.density,    0.05f);
    EXPECT_FLOAT_EQ(fog.anisotropy, 0.7f);
    EXPECT_FLOAT_EQ(fog.scattering, 0.9f);
    EXPECT_FLOAT_EQ(fog.fogNear,    1.0f);
    EXPECT_FLOAT_EQ(fog.fogFar,     120.0f);
    EXPECT_FLOAT_EQ(fog.ambientFogR, 0.1f);
    EXPECT_FLOAT_EQ(fog.ambientFogG, 0.2f);
    EXPECT_FLOAT_EQ(fog.ambientFogB, 0.3f);
}

// ─── SSR settings roundtrip ───────────────────────────────────────────────────

TEST(EmapRenderSettings, SSRRoundtrip)
{
    TempEmapFile tmp("test_rs_ssr.emap");

    EditMapDocument docOut;
    {
        SSRData& ssr         = docOut.renderSettings().ssr;
        ssr.enabled          = true;
        ssr.maxDistance      = 30.0f;
        ssr.thickness        = 0.2f;
        ssr.roughnessCutoff  = 0.5f;
        ssr.fadeStart        = 0.15f;
        ssr.maxSteps         = 128u;
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    const SSRData& ssr = docIn.renderSettings().ssr;
    EXPECT_EQ      (ssr.enabled,         true);
    EXPECT_FLOAT_EQ(ssr.maxDistance,     30.0f);
    EXPECT_FLOAT_EQ(ssr.thickness,       0.2f);
    EXPECT_FLOAT_EQ(ssr.roughnessCutoff, 0.5f);
    EXPECT_FLOAT_EQ(ssr.fadeStart,       0.15f);
    EXPECT_EQ      (ssr.maxSteps,        128u);
}

// ─── DoF settings roundtrip ───────────────────────────────────────────────────

TEST(EmapRenderSettings, DoFRoundtrip)
{
    TempEmapFile tmp("test_rs_dof.emap");

    EditMapDocument docOut;
    {
        DoFData& dof       = docOut.renderSettings().dof;
        dof.enabled        = true;
        dof.focusDistance  = 8.0f;
        dof.focusRange     = 3.0f;
        dof.bokehRadius    = 12.0f;
        dof.nearTransition = 1.5f;
        dof.farTransition  = 4.0f;
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    const DoFData& dof = docIn.renderSettings().dof;
    EXPECT_EQ      (dof.enabled,        true);
    EXPECT_FLOAT_EQ(dof.focusDistance,  8.0f);
    EXPECT_FLOAT_EQ(dof.focusRange,     3.0f);
    EXPECT_FLOAT_EQ(dof.bokehRadius,    12.0f);
    EXPECT_FLOAT_EQ(dof.nearTransition, 1.5f);
    EXPECT_FLOAT_EQ(dof.farTransition,  4.0f);
}

// ─── Motion blur settings roundtrip ──────────────────────────────────────────

TEST(EmapRenderSettings, MotionBlurRoundtrip)
{
    TempEmapFile tmp("test_rs_mb.emap");

    EditMapDocument docOut;
    {
        MotionBlurData& mb = docOut.renderSettings().motionBlur;
        mb.enabled         = true;
        mb.shutterAngle    = 0.75f;
        mb.numSamples      = 16u;
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    const MotionBlurData& mb = docIn.renderSettings().motionBlur;
    EXPECT_EQ      (mb.enabled,      true);
    EXPECT_FLOAT_EQ(mb.shutterAngle, 0.75f);
    EXPECT_EQ      (mb.numSamples,   16u);
}

// ─── Colour grading + LUT path roundtrip ─────────────────────────────────────

TEST(EmapRenderSettings, ColorGradingAndLutPathRoundtrip)
{
    TempEmapFile tmp("test_rs_cg.emap");

    EditMapDocument docOut;
    {
        ColorGradingData& cg = docOut.renderSettings().colorGrading;
        cg.enabled   = true;
        cg.intensity = 0.8f;
        cg.lutPath   = "luts/cinematic.png";
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    const ColorGradingData& cg = docIn.renderSettings().colorGrading;
    EXPECT_EQ      (cg.enabled,   true);
    EXPECT_FLOAT_EQ(cg.intensity, 0.8f);
    EXPECT_EQ      (cg.lutPath,   "luts/cinematic.png");
}

// ─── Optional FX settings roundtrip ─────────────────────────────────────────

TEST(EmapRenderSettings, OptionalFxRoundtrip)
{
    TempEmapFile tmp("test_rs_fx.emap");

    EditMapDocument docOut;
    {
        OptionalFxData& fx    = docOut.renderSettings().optionalFx;
        fx.enabled            = true;
        fx.caAmount           = 0.003f;
        fx.vignetteIntensity  = 0.5f;
        fx.vignetteRadius     = 0.35f;
        fx.grainAmount        = 0.06f;
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    const OptionalFxData& fx = docIn.renderSettings().optionalFx;
    EXPECT_EQ      (fx.enabled,           true);
    EXPECT_FLOAT_EQ(fx.caAmount,          0.003f);
    EXPECT_FLOAT_EQ(fx.vignetteIntensity, 0.5f);
    EXPECT_FLOAT_EQ(fx.vignetteRadius,    0.35f);
    EXPECT_FLOAT_EQ(fx.grainAmount,       0.06f);
}

// ─── Upscaling roundtrip ─────────────────────────────────────────────────────

TEST(EmapRenderSettings, UpscalingFxaaRoundtrip)
{
    TempEmapFile tmp("test_rs_up.emap");

    EditMapDocument docOut;
    docOut.renderSettings().upscaling.fxaaEnabled = false;
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    EXPECT_EQ(docIn.renderSettings().upscaling.fxaaEnabled, false);
}

// ─── Backward compatibility: v1 file loads without render_settings ────────────

TEST(EmapRenderSettings, V1FileLoadsWithDefaultRenderSettings)
{
    // Write a v1-style file by hand — just the top-level keys a v1 file would have.
    TempEmapFile tmp("test_rs_v1compat.emap");

    // Build via a saved doc, then manually strip render_settings from the JSON.
    // This is the simplest way to synthesise a v1 file without reaching into
    // nlohmann internals from outside the TU.
    {
        EditMapDocument doc;
        ASSERT_TRUE(saveEmap(doc, tmp.path).has_value());
    }

    // Patch the JSON on disk: remove the render_settings key (keep current version).
    {
        std::ifstream ifs(tmp.path);
        ASSERT_TRUE(ifs.is_open());
        nlohmann::json root;
        ifs >> root;
        root.erase("render_settings");
        std::ofstream ofs(tmp.path);
        ofs << root.dump(4) << '\n';
    }

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    // All render settings should be at their defaults.
    const RenderSettingsData& rs = docIn.renderSettings();
    EXPECT_EQ  (rs.fog.enabled,             false);
    EXPECT_EQ  (rs.ssr.enabled,             false);
    EXPECT_EQ  (rs.dof.enabled,             false);
    EXPECT_EQ  (rs.motionBlur.enabled,      false);
    EXPECT_EQ  (rs.colorGrading.enabled,    false);
    EXPECT_EQ  (rs.colorGrading.lutPath,    std::string{});
    EXPECT_EQ  (rs.optionalFx.enabled,      false);
    EXPECT_EQ  (rs.upscaling.fxaaEnabled,   true);
}
