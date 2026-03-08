// test_post_fx.cpp
// Unit tests for Depth of Field, Motion Blur, and Colour Grading data structures.
//
// All tests are pure CPU / data-layout checks — no GPU objects are created.
// GPU struct sizes and offsets are verified against the MSL constant buffer
// layouts defined in common.h.  Shader algorithm correctness is validated
// by visual inspection of rendered output.

#include "daedalus/render/scene_data.h"
#include "daedalus/render/scene_view.h"

#include <gtest/gtest.h>

#include <cstddef>

using namespace daedalus::render;

static constexpr float k_eps = 1e-6f;

// ─── DoFConstantsGPU layout ──────────────────────────────────────────────────

TEST(DoFConstantsGPU, SizeIs32Bytes)
{
    static_assert(sizeof(DoFConstantsGPU) == 32,
                  "DoFConstantsGPU must be 32 bytes");
    EXPECT_EQ(sizeof(DoFConstantsGPU), 32u);
}

TEST(DoFConstantsGPU, AlignmentIs16Bytes)
{
    static_assert(alignof(DoFConstantsGPU) == 16,
                  "DoFConstantsGPU must be 16-byte aligned");
    EXPECT_EQ(alignof(DoFConstantsGPU), 16u);
}

TEST(DoFConstantsGPU, FieldOffsets)
{
    // Must match DoFConstants in common.h exactly.
    // [0] focusDistance, [4] focusRange, [8] bokehRadius,
    // [12] nearTransition, [16] farTransition, [20-28] pad0/1/2
    EXPECT_EQ(offsetof(DoFConstantsGPU, focusDistance),  0u);
    EXPECT_EQ(offsetof(DoFConstantsGPU, focusRange),     4u);
    EXPECT_EQ(offsetof(DoFConstantsGPU, bokehRadius),    8u);
    EXPECT_EQ(offsetof(DoFConstantsGPU, nearTransition), 12u);
    EXPECT_EQ(offsetof(DoFConstantsGPU, farTransition),  16u);
    EXPECT_EQ(offsetof(DoFConstantsGPU, pad0),           20u);
    EXPECT_EQ(offsetof(DoFConstantsGPU, pad1),           24u);
    EXPECT_EQ(offsetof(DoFConstantsGPU, pad2),           28u);
}

// ─── MotionBlurConstantsGPU layout ───────────────────────────────────────────

TEST(MotionBlurConstantsGPU, SizeIs16Bytes)
{
    static_assert(sizeof(MotionBlurConstantsGPU) == 16,
                  "MotionBlurConstantsGPU must be 16 bytes");
    EXPECT_EQ(sizeof(MotionBlurConstantsGPU), 16u);
}

TEST(MotionBlurConstantsGPU, AlignmentIs16Bytes)
{
    static_assert(alignof(MotionBlurConstantsGPU) == 16,
                  "MotionBlurConstantsGPU must be 16-byte aligned");
    EXPECT_EQ(alignof(MotionBlurConstantsGPU), 16u);
}

TEST(MotionBlurConstantsGPU, FieldOffsets)
{
    // [0] shutterAngle (f32), [4] numSamples (u32), [8-12] pad0/1
    EXPECT_EQ(offsetof(MotionBlurConstantsGPU, shutterAngle), 0u);
    EXPECT_EQ(offsetof(MotionBlurConstantsGPU, numSamples),   4u);
    EXPECT_EQ(offsetof(MotionBlurConstantsGPU, pad0),         8u);
    EXPECT_EQ(offsetof(MotionBlurConstantsGPU, pad1),         12u);
}

// ─── ColorGradingConstantsGPU layout ─────────────────────────────────────────

TEST(ColorGradingConstantsGPU, SizeIs16Bytes)
{
    static_assert(sizeof(ColorGradingConstantsGPU) == 16,
                  "ColorGradingConstantsGPU must be 16 bytes");
    EXPECT_EQ(sizeof(ColorGradingConstantsGPU), 16u);
}

TEST(ColorGradingConstantsGPU, AlignmentIs16Bytes)
{
    static_assert(alignof(ColorGradingConstantsGPU) == 16,
                  "ColorGradingConstantsGPU must be 16-byte aligned");
    EXPECT_EQ(alignof(ColorGradingConstantsGPU), 16u);
}

TEST(ColorGradingConstantsGPU, FieldOffsets)
{
    // [0] intensity (f32), [4-12] pad0/1/2
    EXPECT_EQ(offsetof(ColorGradingConstantsGPU, intensity), 0u);
    EXPECT_EQ(offsetof(ColorGradingConstantsGPU, pad0),      4u);
    EXPECT_EQ(offsetof(ColorGradingConstantsGPU, pad1),      8u);
    EXPECT_EQ(offsetof(ColorGradingConstantsGPU, pad2),      12u);
}

// ─── DoFParams defaults ───────────────────────────────────────────────────────

TEST(DoFParams, DefaultEnabledIsFalse)
{
    DoFParams p;
    EXPECT_FALSE(p.enabled);
}

TEST(DoFParams, DefaultFocusDistance)
{
    DoFParams p;
    EXPECT_NEAR(p.focusDistance, 5.0f, k_eps);
}

TEST(DoFParams, DefaultFocusRange)
{
    DoFParams p;
    EXPECT_NEAR(p.focusRange, 2.0f, k_eps);
}

TEST(DoFParams, DefaultBokehRadius)
{
    DoFParams p;
    EXPECT_NEAR(p.bokehRadius, 8.0f, k_eps);
}

TEST(DoFParams, DefaultNearTransition)
{
    DoFParams p;
    EXPECT_NEAR(p.nearTransition, 1.0f, k_eps);
}

TEST(DoFParams, DefaultFarTransition)
{
    DoFParams p;
    EXPECT_NEAR(p.farTransition, 3.0f, k_eps);
}

// ─── MotionBlurParams defaults ────────────────────────────────────────────────

TEST(MotionBlurParams, DefaultEnabledIsFalse)
{
    MotionBlurParams p;
    EXPECT_FALSE(p.enabled);
}

TEST(MotionBlurParams, DefaultShutterAngle)
{
    MotionBlurParams p;
    EXPECT_NEAR(p.shutterAngle, 0.5f, k_eps);
}

TEST(MotionBlurParams, DefaultNumSamples)
{
    MotionBlurParams p;
    EXPECT_EQ(p.numSamples, 8u);
}

// ─── ColorGradingParams defaults ─────────────────────────────────────────────

TEST(ColorGradingParams, DefaultEnabledIsFalse)
{
    ColorGradingParams p;
    EXPECT_FALSE(p.enabled);
}

TEST(ColorGradingParams, DefaultIntensity)
{
    ColorGradingParams p;
    EXPECT_NEAR(p.intensity, 1.0f, k_eps);
}

TEST(ColorGradingParams, DefaultLutTextureIsNull)
{
    ColorGradingParams p;
    EXPECT_EQ(p.lutTexture, nullptr);
}

// ─── SceneView member defaults ────────────────────────────────────────────────

TEST(SceneViewPostFX, DefaultDofIsDisabled)
{
    // A freshly-constructed SceneView must have dof.enabled = false so that
    // FrameRenderer skips all three DoF compute passes.
    SceneView scene;
    EXPECT_FALSE(scene.dof.enabled);
}

TEST(SceneViewPostFX, DefaultMotionBlurIsDisabled)
{
    // A freshly-constructed SceneView must have motionBlur.enabled = false so
    // that FrameRenderer skips the motion blur compute pass.
    SceneView scene;
    EXPECT_FALSE(scene.motionBlur.enabled);
}

TEST(SceneViewPostFX, DefaultColorGradingIsDisabled)
{
    // A freshly-constructed SceneView must have colorGrading.enabled = false so
    // that Tonemap writes directly to the swapchain with zero extra cost.
    SceneView scene;
    EXPECT_FALSE(scene.colorGrading.enabled);
}

TEST(SceneViewPostFX, DefaultColorGradingLutTextureIsNull)
{
    SceneView scene;
    EXPECT_EQ(scene.colorGrading.lutTexture, nullptr);
}

// ─── Round-trip: DoFParams → DoFConstantsGPU ─────────────────────────────────

TEST(DoFConstantsGPU, RoundTripFromParams)
{
    // Replicate the packing in frame_renderer.cpp renderFrame().
    DoFParams p;
    p.enabled        = true;
    p.focusDistance  = 4.0f;
    p.focusRange     = 2.0f;
    p.bokehRadius    = 6.0f;
    p.nearTransition = 1.0f;
    p.farTransition  = 3.0f;

    DoFConstantsGPU c{};
    c.focusDistance  = p.focusDistance;
    c.focusRange     = p.focusRange;
    c.bokehRadius    = p.bokehRadius;
    c.nearTransition = p.nearTransition;
    c.farTransition  = p.farTransition;

    EXPECT_NEAR(c.focusDistance,  4.0f, k_eps);
    EXPECT_NEAR(c.focusRange,     2.0f, k_eps);
    EXPECT_NEAR(c.bokehRadius,    6.0f, k_eps);
    EXPECT_NEAR(c.nearTransition, 1.0f, k_eps);
    EXPECT_NEAR(c.farTransition,  3.0f, k_eps);
    EXPECT_NEAR(c.pad0, 0.0f, k_eps);
    EXPECT_NEAR(c.pad1, 0.0f, k_eps);
    EXPECT_NEAR(c.pad2, 0.0f, k_eps);
}

// ─── Round-trip: MotionBlurParams → MotionBlurConstantsGPU ───────────────────

TEST(MotionBlurConstantsGPU, RoundTripFromParams)
{
    MotionBlurParams p;
    p.enabled      = true;
    p.shutterAngle = 0.25f;
    p.numSamples   = 8u;

    MotionBlurConstantsGPU c{};
    c.shutterAngle = p.shutterAngle;
    c.numSamples   = p.numSamples;

    EXPECT_NEAR(c.shutterAngle, 0.25f, k_eps);
    EXPECT_EQ  (c.numSamples,   8u);
    EXPECT_NEAR(c.pad0, 0.0f, k_eps);
    EXPECT_NEAR(c.pad1, 0.0f, k_eps);
}

// ─── OptionalFxConstantsGPU layout ───────────────────────────────────────────

TEST(OptionalFxConstantsGPU, SizeIs32Bytes)
{
    static_assert(sizeof(OptionalFxConstantsGPU) == 32,
                  "OptionalFxConstantsGPU must be 32 bytes");
    EXPECT_EQ(sizeof(OptionalFxConstantsGPU), 32u);
}

TEST(OptionalFxConstantsGPU, AlignmentIs16Bytes)
{
    static_assert(alignof(OptionalFxConstantsGPU) == 16,
                  "OptionalFxConstantsGPU must be 16-byte aligned");
    EXPECT_EQ(alignof(OptionalFxConstantsGPU), 16u);
}

TEST(OptionalFxConstantsGPU, FieldOffsets)
{
    // Must match OptionalFxConstants in common.h exactly.
    // [0] caAmount, [4] vignetteIntensity, [8] vignetteRadius,
    // [12] grainAmount, [16] grainSeed, [20-28] pad0/1/2
    EXPECT_EQ(offsetof(OptionalFxConstantsGPU, caAmount),          0u);
    EXPECT_EQ(offsetof(OptionalFxConstantsGPU, vignetteIntensity), 4u);
    EXPECT_EQ(offsetof(OptionalFxConstantsGPU, vignetteRadius),    8u);
    EXPECT_EQ(offsetof(OptionalFxConstantsGPU, grainAmount),       12u);
    EXPECT_EQ(offsetof(OptionalFxConstantsGPU, grainSeed),         16u);
    EXPECT_EQ(offsetof(OptionalFxConstantsGPU, pad0),              20u);
    EXPECT_EQ(offsetof(OptionalFxConstantsGPU, pad1),              24u);
    EXPECT_EQ(offsetof(OptionalFxConstantsGPU, pad2),              28u);
}

// ─── OptionalFxParams defaults ───────────────────────────────────────────────

TEST(OptionalFxParams, DefaultEnabledIsFalse)
{
    OptionalFxParams p;
    EXPECT_FALSE(p.enabled);
}

TEST(OptionalFxParams, DefaultCaAmountIsZero)
{
    OptionalFxParams p;
    EXPECT_NEAR(p.caAmount, 0.0f, k_eps);
}

TEST(OptionalFxParams, DefaultVignetteIntensity)
{
    OptionalFxParams p;
    EXPECT_NEAR(p.vignetteIntensity, 0.30f, k_eps);
}

TEST(OptionalFxParams, DefaultVignetteRadius)
{
    OptionalFxParams p;
    EXPECT_NEAR(p.vignetteRadius, 0.40f, k_eps);
}

TEST(OptionalFxParams, DefaultGrainAmount)
{
    OptionalFxParams p;
    EXPECT_NEAR(p.grainAmount, 0.04f, k_eps);
}

// ─── UpscalingParams defaults ─────────────────────────────────────────────────

TEST(UpscalingParams, DefaultModeIsFXAA)
{
    // FXAA is on by default: callers must explicitly set mode=None to disable it.
    UpscalingParams p;
    EXPECT_EQ(p.mode, UpscalingMode::FXAA);
}

// ─── MirrorDraw defaults ──────────────────────────────────────────────────────

TEST(MirrorDraw, DefaultRenderTargetIsNull)
{
    MirrorDraw m;
    EXPECT_EQ(m.renderTarget, nullptr);
}

TEST(MirrorDraw, DefaultReflectedDrawsIsEmpty)
{
    MirrorDraw m;
    EXPECT_TRUE(m.reflectedDraws.empty());
}

TEST(MirrorDraw, DefaultDimensions)
{
    MirrorDraw m;
    EXPECT_EQ(m.rtWidth,  512u);
    EXPECT_EQ(m.rtHeight, 512u);
}

// ─── SceneView Phase-1D member defaults ──────────────────────────────────────

TEST(SceneViewPostFX, DefaultOptionalFxIsDisabled)
{
    SceneView scene;
    EXPECT_FALSE(scene.optionalFx.enabled);
}

TEST(SceneViewPostFX, DefaultUpscalingIsFXAA)
{
    SceneView scene;
    EXPECT_EQ(scene.upscaling.mode, UpscalingMode::FXAA);
}

TEST(SceneViewPostFX, DefaultMirrorsIsEmpty)
{
    SceneView scene;
    EXPECT_TRUE(scene.mirrors.empty());
}

// ─── Round-trip: ColorGradingParams → ColorGradingConstantsGPU ───────────────

TEST(ColorGradingConstantsGPU, RoundTripFromParams)
{
    ColorGradingParams p;
    p.enabled    = true;
    p.intensity  = 1.0f;
    p.lutTexture = nullptr;

    ColorGradingConstantsGPU c{};
    c.intensity = p.intensity;

    EXPECT_NEAR(c.intensity, 1.0f, k_eps);
    EXPECT_NEAR(c.pad0, 0.0f, k_eps);
    EXPECT_NEAR(c.pad1, 0.0f, k_eps);
    EXPECT_NEAR(c.pad2, 0.0f, k_eps);
}
