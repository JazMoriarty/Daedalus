// test_volumetric_fog.cpp
// Unit tests for volumetric fog data structures and defaults.
//
// All tests are pure CPU / data-layout checks — no GPU objects are created.
// Shader logic is validated by visual inspection; the froxel math correctness
// is covered separately by the integration build.

#include "daedalus/render/scene_data.h"
#include "daedalus/render/scene_view.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include <cstddef>

using namespace daedalus::render;

static constexpr float k_eps = 1e-6f;

// ─── VolumetricFogConstantsGPU layout ────────────────────────────────────────

TEST(VolumetricFogConstantsGPU, SizeIs32Bytes)
{
    static_assert(sizeof(VolumetricFogConstantsGPU) == 32,
                  "VolumetricFogConstantsGPU must be 32 bytes");
    EXPECT_EQ(sizeof(VolumetricFogConstantsGPU), 32u);
}

TEST(VolumetricFogConstantsGPU, AlignmentIs16Bytes)
{
    static_assert(alignof(VolumetricFogConstantsGPU) == 16,
                  "VolumetricFogConstantsGPU must be 16-byte aligned");
    EXPECT_EQ(alignof(VolumetricFogConstantsGPU), 16u);
}

TEST(VolumetricFogConstantsGPU, FieldOffsets)
{
    // Verify the GPU layout matches the MSL VolumetricFogConstants struct exactly.
    // First four floats occupy bytes 0–15; ambientFog occupies bytes 16–31.
    EXPECT_EQ(offsetof(VolumetricFogConstantsGPU, density),    0u);
    EXPECT_EQ(offsetof(VolumetricFogConstantsGPU, anisotropy), 4u);
    EXPECT_EQ(offsetof(VolumetricFogConstantsGPU, scattering), 8u);
    EXPECT_EQ(offsetof(VolumetricFogConstantsGPU, fogFar),     12u);
    EXPECT_EQ(offsetof(VolumetricFogConstantsGPU, ambientFog), 16u);
}

TEST(VolumetricFogConstantsGPU, AmbientFogWStoresFogNear)
{
    // The CPU packs fogNear into ambientFog.w so the MSL shader reads one vec4.
    VolumetricFogConstantsGPU c{};
    c.ambientFog = glm::vec4(0.04f, 0.05f, 0.06f, 0.5f);  // w = fogNear

    EXPECT_NEAR(c.ambientFog.w, 0.5f, k_eps);
}

// ─── VolumetricFogParams defaults ────────────────────────────────────────────

TEST(VolumetricFogParams, DefaultEnabledIsFalse)
{
    VolumetricFogParams p;
    EXPECT_FALSE(p.enabled);
}

TEST(VolumetricFogParams, DefaultDensity)
{
    VolumetricFogParams p;
    EXPECT_NEAR(p.density, 0.02f, k_eps);
}

TEST(VolumetricFogParams, DefaultAnisotropy)
{
    VolumetricFogParams p;
    EXPECT_NEAR(p.anisotropy, 0.3f, k_eps);
}

TEST(VolumetricFogParams, DefaultScattering)
{
    VolumetricFogParams p;
    EXPECT_NEAR(p.scattering, 0.8f, k_eps);
}

TEST(VolumetricFogParams, DefaultFogFar)
{
    VolumetricFogParams p;
    EXPECT_NEAR(p.fogFar, 80.0f, k_eps);
}

TEST(VolumetricFogParams, DefaultFogNear)
{
    VolumetricFogParams p;
    EXPECT_NEAR(p.fogNear, 0.5f, k_eps);
}

TEST(VolumetricFogParams, DefaultAmbientFogColour)
{
    VolumetricFogParams p;
    EXPECT_NEAR(p.ambientFog.r, 0.04f, k_eps);
    EXPECT_NEAR(p.ambientFog.g, 0.05f, k_eps);
    EXPECT_NEAR(p.ambientFog.b, 0.06f, k_eps);
}

// ─── SceneView::fog default ───────────────────────────────────────────────────

TEST(SceneViewFog, DefaultFogIsDisabled)
{
    // A freshly-constructed SceneView must have fog.enabled = false so that
    // FrameRenderer skips all three fog passes by default.
    SceneView scene;
    EXPECT_FALSE(scene.fog.enabled);
}

TEST(SceneViewFog, DefaultFogDensityIsReasonable)
{
    SceneView scene;
    // 0.02 = 2 % extinction per metre — a light indoor haze.
    EXPECT_NEAR(scene.fog.density, 0.02f, k_eps);
}

// ─── Round-trip: VolumetricFogParams → VolumetricFogConstantsGPU ─────────────

TEST(VolumetricFogConstantsGPU, RoundTripFromParams)
{
    // Replicate the packing in frame_renderer.cpp renderFrame().
    VolumetricFogParams p;
    p.enabled    = true;
    p.density    = 0.015f;
    p.anisotropy = 0.3f;
    p.scattering = 0.8f;
    p.fogFar     = 80.0f;
    p.fogNear    = 0.5f;
    p.ambientFog = glm::vec3(0.04f, 0.05f, 0.06f);

    VolumetricFogConstantsGPU c{};
    c.density    = p.density;
    c.anisotropy = p.anisotropy;
    c.scattering = p.scattering;
    c.fogFar     = p.fogFar;
    c.ambientFog = glm::vec4(p.ambientFog, p.fogNear);

    EXPECT_NEAR(c.density,       0.015f, k_eps);
    EXPECT_NEAR(c.anisotropy,    0.3f,   k_eps);
    EXPECT_NEAR(c.scattering,    0.8f,   k_eps);
    EXPECT_NEAR(c.fogFar,        80.0f,  k_eps);
    EXPECT_NEAR(c.ambientFog.x,  0.04f,  k_eps);
    EXPECT_NEAR(c.ambientFog.y,  0.05f,  k_eps);
    EXPECT_NEAR(c.ambientFog.z,  0.06f,  k_eps);
    EXPECT_NEAR(c.ambientFog.w,  0.5f,   k_eps);  // fogNear packed into w
}
