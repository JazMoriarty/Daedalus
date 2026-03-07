// test_ssr.cpp
// Unit tests for Screen-Space Reflection data structures and defaults.
//
// All tests are pure CPU / data-layout checks — no GPU objects are created.
// The shader algorithm is validated by visual inspection of rendered output;
// correctness of individual math operations (Fresnel, edge fade, etc.) is
// verified by reviewing ssr_main.metal directly.

#include "daedalus/render/scene_data.h"
#include "daedalus/render/scene_view.h"

#include <gtest/gtest.h>

#include <cstddef>

using namespace daedalus::render;

static constexpr float k_eps = 1e-6f;

// ─── SSRConstantsGPU layout ──────────────────────────────────────────────────

TEST(SSRConstantsGPU, SizeIs32Bytes)
{
    static_assert(sizeof(SSRConstantsGPU) == 32,
                  "SSRConstantsGPU must be 32 bytes");
    EXPECT_EQ(sizeof(SSRConstantsGPU), 32u);
}

TEST(SSRConstantsGPU, AlignmentIs16Bytes)
{
    static_assert(alignof(SSRConstantsGPU) == 16,
                  "SSRConstantsGPU must be 16-byte aligned");
    EXPECT_EQ(alignof(SSRConstantsGPU), 16u);
}

TEST(SSRConstantsGPU, FieldOffsets)
{
    // Verify the GPU layout matches the MSL SSRConstants struct exactly.
    // Bytes 0-15: four floats (maxDistance, thickness, roughnessCutoff, fadeStart)
    // Bytes 16-19: uint maxSteps
    // Bytes 20-31: three float pads
    EXPECT_EQ(offsetof(SSRConstantsGPU, maxDistance),     0u);
    EXPECT_EQ(offsetof(SSRConstantsGPU, thickness),       4u);
    EXPECT_EQ(offsetof(SSRConstantsGPU, roughnessCutoff), 8u);
    EXPECT_EQ(offsetof(SSRConstantsGPU, fadeStart),       12u);
    EXPECT_EQ(offsetof(SSRConstantsGPU, maxSteps),        16u);
    EXPECT_EQ(offsetof(SSRConstantsGPU, pad0),            20u);
    EXPECT_EQ(offsetof(SSRConstantsGPU, pad1),            24u);
    EXPECT_EQ(offsetof(SSRConstantsGPU, pad2),            28u);
}

// ─── SSRParams defaults ───────────────────────────────────────────────────────

TEST(SSRParams, DefaultEnabledIsFalse)
{
    SceneView::SSRParams p;
    EXPECT_FALSE(p.enabled);
}

TEST(SSRParams, DefaultMaxDistance)
{
    SceneView::SSRParams p;
    EXPECT_NEAR(p.maxDistance, 20.0f, k_eps);
}

TEST(SSRParams, DefaultThickness)
{
    SceneView::SSRParams p;
    EXPECT_NEAR(p.thickness, 0.15f, k_eps);
}

TEST(SSRParams, DefaultRoughnessCutoff)
{
    SceneView::SSRParams p;
    EXPECT_NEAR(p.roughnessCutoff, 0.6f, k_eps);
}

TEST(SSRParams, DefaultFadeStart)
{
    SceneView::SSRParams p;
    EXPECT_NEAR(p.fadeStart, 0.1f, k_eps);
}

TEST(SSRParams, DefaultMaxSteps)
{
    SceneView::SSRParams p;
    EXPECT_EQ(p.maxSteps, 64u);
}

// ─── SceneView::ssr default ───────────────────────────────────────────────────

TEST(SceneViewSSR, DefaultSSRIsDisabled)
{
    // A freshly-constructed SceneView must have ssr.enabled = false so that
    // FrameRenderer skips the SSR compute pass and routes Bloom/Tonemap
    // directly from the TAA output with zero GPU cost.
    SceneView scene;
    EXPECT_FALSE(scene.ssr.enabled);
}

TEST(SceneViewSSR, DefaultSSRMaxDistanceIsReasonable)
{
    SceneView scene;
    // 20 m covers most interior scenes without excessive compute cost.
    EXPECT_NEAR(scene.ssr.maxDistance, 20.0f, k_eps);
}

// ─── Round-trip: SSRParams → SSRConstantsGPU ─────────────────────────────────

TEST(SSRConstantsGPU, RoundTripFromParams)
{
    // Replicate the packing in frame_renderer.cpp renderFrame().
    SceneView::SSRParams p;
    p.enabled         = true;
    p.maxDistance     = 15.0f;
    p.thickness       = 0.15f;
    p.roughnessCutoff = 0.5f;
    p.fadeStart       = 0.1f;
    p.maxSteps        = 64u;

    SSRConstantsGPU c{};
    c.maxDistance     = p.maxDistance;
    c.thickness       = p.thickness;
    c.roughnessCutoff = p.roughnessCutoff;
    c.fadeStart       = p.fadeStart;
    c.maxSteps        = p.maxSteps;

    EXPECT_NEAR(c.maxDistance,     15.0f, k_eps);
    EXPECT_NEAR(c.thickness,        0.15f, k_eps);
    EXPECT_NEAR(c.roughnessCutoff,  0.5f,  k_eps);
    EXPECT_NEAR(c.fadeStart,        0.1f,  k_eps);
    EXPECT_EQ  (c.maxSteps,         64u);
    // Padding fields must be zero-initialised.
    EXPECT_NEAR(c.pad0, 0.0f, k_eps);
    EXPECT_NEAR(c.pad1, 0.0f, k_eps);
    EXPECT_NEAR(c.pad2, 0.0f, k_eps);
}
