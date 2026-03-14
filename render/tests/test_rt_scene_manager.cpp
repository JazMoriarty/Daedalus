// test_rt_scene_manager.cpp
// Unit tests for ray tracing data structures and SceneView RT defaults.
//
// All tests are pure CPU / data-layout checks — no GPU objects are created.
// GPU struct sizes and offsets are verified against the MSL constant buffer
// layouts defined in common.h.

#include "daedalus/render/scene_data.h"
#include "daedalus/render/scene_view.h"

#include <gtest/gtest.h>

#include <cstddef>

using namespace daedalus::render;

// ─── RTConstantsGPU layout ───────────────────────────────────────────────────

TEST(RTConstantsGPU, SizeIs16Bytes)
{
    static_assert(sizeof(RTConstantsGPU) == 16,
                  "RTConstantsGPU must be 16 bytes");
    EXPECT_EQ(sizeof(RTConstantsGPU), 16u);
}

TEST(RTConstantsGPU, AlignmentIs16Bytes)
{
    static_assert(alignof(RTConstantsGPU) == 16,
                  "RTConstantsGPU must be 16-byte aligned");
    EXPECT_EQ(alignof(RTConstantsGPU), 16u);
}

TEST(RTConstantsGPU, FieldOffsets)
{
    // [0] maxBounces (u32), [4] samplesPerPixel (u32), [8-12] pad
    EXPECT_EQ(offsetof(RTConstantsGPU, maxBounces),      0u);
    EXPECT_EQ(offsetof(RTConstantsGPU, samplesPerPixel),  4u);
    EXPECT_EQ(offsetof(RTConstantsGPU, pad0),             8u);
    EXPECT_EQ(offsetof(RTConstantsGPU, pad1),             12u);
}

TEST(RTConstantsGPU, Defaults)
{
    RTConstantsGPU c{};
    EXPECT_EQ(c.maxBounces, 2u);
    EXPECT_EQ(c.samplesPerPixel, 1u);
}

// ─── RTMaterialGPU layout ────────────────────────────────────────────────────

TEST(RTMaterialGPU, SizeIs80Bytes)
{
    static_assert(sizeof(RTMaterialGPU) == 80,
                  "RTMaterialGPU must be 80 bytes");
    EXPECT_EQ(sizeof(RTMaterialGPU), 80u);
}

TEST(RTMaterialGPU, AlignmentIs16Bytes)
{
    static_assert(alignof(RTMaterialGPU) == 16,
                  "RTMaterialGPU must be 16-byte aligned");
    EXPECT_EQ(alignof(RTMaterialGPU), 16u);
}

TEST(RTMaterialGPU, FieldOffsets)
{
    EXPECT_EQ(offsetof(RTMaterialGPU, albedoTextureIndex),   0u);
    EXPECT_EQ(offsetof(RTMaterialGPU, normalTextureIndex),   4u);
    EXPECT_EQ(offsetof(RTMaterialGPU, emissiveTextureIndex), 8u);
    EXPECT_EQ(offsetof(RTMaterialGPU, roughness),            12u);
    EXPECT_EQ(offsetof(RTMaterialGPU, metalness),            16u);
    EXPECT_EQ(offsetof(RTMaterialGPU, primitiveDataOffset),  20u);
    EXPECT_EQ(offsetof(RTMaterialGPU, tint),                 32u);
    EXPECT_EQ(offsetof(RTMaterialGPU, uvOffset),             48u);
    EXPECT_EQ(offsetof(RTMaterialGPU, uvScale),              56u);
    EXPECT_EQ(offsetof(RTMaterialGPU, sectorAmbient),        64u);
}

TEST(RTMaterialGPU, DefaultTextureIndicesAreZero)
{
    RTMaterialGPU m{};
    EXPECT_EQ(m.albedoTextureIndex, 0u);
    EXPECT_EQ(m.normalTextureIndex, 0u);
    EXPECT_EQ(m.emissiveTextureIndex, 0u);
    EXPECT_EQ(m.primitiveDataOffset, 0u);
}

TEST(RTMaterialGPU, DefaultUvScaleIsOne)
{
    RTMaterialGPU m{};
    EXPECT_FLOAT_EQ(m.uvScale.x, 1.0f);
    EXPECT_FLOAT_EQ(m.uvScale.y, 1.0f);
}

// ─── RTPrimitiveDataGPU layout ───────────────────────────────────────────────

TEST(RTPrimitiveDataGPU, SizeIs112Bytes)
{
    static_assert(sizeof(RTPrimitiveDataGPU) == 112,
                  "RTPrimitiveDataGPU must be 112 bytes");
    EXPECT_EQ(sizeof(RTPrimitiveDataGPU), 112u);
}

TEST(RTPrimitiveDataGPU, FieldOffsets)
{
    EXPECT_EQ(offsetof(RTPrimitiveDataGPU, uv0),      0u);
    EXPECT_EQ(offsetof(RTPrimitiveDataGPU, uv1),      8u);
    EXPECT_EQ(offsetof(RTPrimitiveDataGPU, uv2),      16u);
    EXPECT_EQ(offsetof(RTPrimitiveDataGPU, normal0),  24u);
    EXPECT_EQ(offsetof(RTPrimitiveDataGPU, normal1),  36u);
    EXPECT_EQ(offsetof(RTPrimitiveDataGPU, normal2),  48u);
    EXPECT_EQ(offsetof(RTPrimitiveDataGPU, tangent0), 60u);
    EXPECT_EQ(offsetof(RTPrimitiveDataGPU, tangent1), 76u);
    EXPECT_EQ(offsetof(RTPrimitiveDataGPU, tangent2), 92u);
    EXPECT_EQ(offsetof(RTPrimitiveDataGPU, pad),      108u);
}

// ─── SVGFConstantsGPU layout ─────────────────────────────────────────────────

TEST(SVGFConstantsGPU, SizeIs32Bytes)
{
    static_assert(sizeof(SVGFConstantsGPU) == 32,
                  "SVGFConstantsGPU must be 32 bytes");
    EXPECT_EQ(sizeof(SVGFConstantsGPU), 32u);
}

TEST(SVGFConstantsGPU, AlignmentIs16Bytes)
{
    static_assert(alignof(SVGFConstantsGPU) == 16,
                  "SVGFConstantsGPU must be 16-byte aligned");
    EXPECT_EQ(alignof(SVGFConstantsGPU), 16u);
}

TEST(SVGFConstantsGPU, FieldOffsets)
{
    EXPECT_EQ(offsetof(SVGFConstantsGPU, alpha),        0u);
    EXPECT_EQ(offsetof(SVGFConstantsGPU, momentsAlpha), 4u);
    EXPECT_EQ(offsetof(SVGFConstantsGPU, phiColor),     8u);
    EXPECT_EQ(offsetof(SVGFConstantsGPU, phiNormal),    12u);
    EXPECT_EQ(offsetof(SVGFConstantsGPU, phiDepth),     16u);
    EXPECT_EQ(offsetof(SVGFConstantsGPU, stepWidth),    20u);
}

TEST(SVGFConstantsGPU, DefaultValues)
{
    SVGFConstantsGPU c{};
    EXPECT_FLOAT_EQ(c.alpha, 0.05f);
    EXPECT_FLOAT_EQ(c.momentsAlpha, 0.2f);
    EXPECT_FLOAT_EQ(c.phiColor, 10.0f);
    EXPECT_FLOAT_EQ(c.phiNormal, 128.0f);
    EXPECT_FLOAT_EQ(c.phiDepth, 1.0f);
    EXPECT_EQ(c.stepWidth, 1u);
}

// ─── SceneView RT defaults ──────────────────────────────────────────────────

TEST(SceneViewRT, DefaultRenderModeIsRasterized)
{
    SceneView scene;
    EXPECT_EQ(scene.renderMode, RenderMode::Rasterized);
}

TEST(SceneViewRT, DefaultRTParams)
{
    SceneView scene;
    EXPECT_EQ(scene.rt.maxBounces, 2u);
    EXPECT_EQ(scene.rt.samplesPerPixel, 1u);
    EXPECT_TRUE(scene.rt.denoise);
}

TEST(SceneViewRT, RenderModeEnumValues)
{
    EXPECT_EQ(static_cast<uint32_t>(RenderMode::Rasterized), 0u);
    EXPECT_EQ(static_cast<uint32_t>(RenderMode::RayTraced),  1u);
}
