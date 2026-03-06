// test_vox_mesher.cpp
// Unit tests for greedyMeshVoxels() in vox_mesher.h.
//
// These tests are pure CPU logic — no Metal device required.
//
// Test cases:
//   EmptyVox_NoGeometry          — all-zero voxel data → empty MeshData
//   SingleVoxel_SixFaces         — 1×1×1 volume → 24 vertices, 36 indices
//   InternalFaceCulling          — 2 adjacent voxels → shared face removed
//   SameColorMerge               — 3×1×1 same-colour strip → fewer quads
//   DifferentColorNoMerge        — 2 adjacent different-colour → no merge
//   UV_PointsToPaletteTexel      — UV.u = (colorIndex + 0.5) / 256
//   NormalsPerFace               — six face groups carry correct outward normals

#include "daedalus/render/vox_mesher.h"
#include "daedalus/render/vox_types.h"

#include <gtest/gtest.h>
#include <cmath>
#include <array>

using namespace daedalus;
using namespace daedalus::render;

static constexpr float k_eps = 1e-5f;

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// Build a VoxData with the given extents, all cells empty.
static VoxData makeVox(u32 sx, u32 sy, u32 sz)
{
    VoxData v;
    v.sizeX = sx; v.sizeY = sy; v.sizeZ = sz;
    v.voxels.resize(static_cast<std::size_t>(sx) * sy * sz, 0u);
    return v;
}

/// Set voxel (x, y, z) in v to colorIndex ci.
static void setVoxel(VoxData& v, u32 x, u32 y, u32 z, u8 ci)
{
    v.voxels[x + v.sizeX * (y + v.sizeY * z)] = ci;
}

// ─── 1. EmptyVox_NoGeometry ───────────────────────────────────────────────────

TEST(GreedyMeshVoxels, EmptyVox_NoGeometry)
{
    // A volume where every cell is 0 (empty) should produce no geometry.
    VoxData vox = makeVox(4, 4, 4);  // all zeros by default
    const MeshData mesh = greedyMeshVoxels(vox);

    EXPECT_EQ(mesh.vertices.size(), 0u);
    EXPECT_EQ(mesh.indices.size(),  0u);
}

// ─── 2. SingleVoxel_SixFaces ─────────────────────────────────────────────────

TEST(GreedyMeshVoxels, SingleVoxel_SixFaces)
{
    // One 1×1×1 voxel, no neighbours.
    // Expected: 6 faces × 4 vertices = 24 verts, 6 × 6 = 36 indices.
    VoxData vox = makeVox(1, 1, 1);
    setVoxel(vox, 0, 0, 0, 1u);

    const MeshData mesh = greedyMeshVoxels(vox);

    EXPECT_EQ(mesh.vertices.size(), 24u);
    EXPECT_EQ(mesh.indices.size(),  36u);
}

// ─── 3. InternalFaceCulling ───────────────────────────────────────────────────

TEST(GreedyMeshVoxels, InternalFaceCulling)
{
    // Two side-by-side voxels along X with DIFFERENT colours so merging is
    // suppressed.  Without culling: 2 × 6 faces = 12 faces = 48 verts.
    // The shared internal face is culled → 10 faces = 40 verts.
    VoxData vox = makeVox(2, 1, 1);
    setVoxel(vox, 0, 0, 0, 1u);
    setVoxel(vox, 1, 0, 0, 2u);

    const MeshData mesh = greedyMeshVoxels(vox);

    // Should NOT equal the no-culling count.
    EXPECT_NE(mesh.vertices.size(), 48u) << "internal face was not culled";
    // Should equal the culled count (10 faces).
    EXPECT_EQ(mesh.vertices.size(), 40u);
    EXPECT_EQ(mesh.indices.size(),  60u);
}

// ─── 4. SameColorMerge ────────────────────────────────────────────────────────

TEST(GreedyMeshVoxels, SameColorMerge)
{
    // A 3×1×1 strip of identical-colour voxels.
    // Without merging (same as InternalFaceCulling × 3): visible faces would
    // be 2 end-caps + 4 side faces × 3 cells = 2 + 12 = 14 quads = 56 verts.
    // With greedy merging: the 4 side-facing rows each merge into one 3×1
    // quad, plus the 2 end-caps = 6 quads = 24 verts.
    VoxData vox = makeVox(3, 1, 1);
    setVoxel(vox, 0, 0, 0, 5u);
    setVoxel(vox, 1, 0, 0, 5u);
    setVoxel(vox, 2, 0, 0, 5u);

    const MeshData mesh = greedyMeshVoxels(vox);

    // Merging should reduce the vertex count well below the no-merge count.
    EXPECT_LT(mesh.vertices.size(), 56u) << "same-colour faces should be merged";
    EXPECT_EQ(mesh.vertices.size(), 24u) << "expected 6 merged quads";
    EXPECT_EQ(mesh.indices.size(),  36u);
}

// ─── 5. DifferentColorNoMerge ────────────────────────────────────────────────

TEST(GreedyMeshVoxels, DifferentColorNoMerge)
{
    // Two side-by-side voxels along X with different colours.
    // The culled-but-unmerged count is 10 faces = 40 verts (see test 3).
    // Same-colour merging of the top/bottom/front/back would give 6 × 4 = 24.
    // Since colours differ the strip faces should NOT be merged → 40 verts.
    VoxData vox = makeVox(2, 1, 1);
    setVoxel(vox, 0, 0, 0, 3u);
    setVoxel(vox, 1, 0, 0, 7u);

    const MeshData mesh = greedyMeshVoxels(vox);

    // Must be more than the merged count (different colours → no merge).
    EXPECT_GT(mesh.vertices.size(), 24u) << "different-colour faces must not merge";
    EXPECT_EQ(mesh.vertices.size(), 40u);
}

// ─── 6. UV_PointsToPaletteTexel ──────────────────────────────────────────────

TEST(GreedyMeshVoxels, UV_PointsToPaletteTexel)
{
    // All vertices of a single-voxel mesh with colorIndex ci must have
    // UV.u = (ci + 0.5) / 256 and UV.v = 0.5.
    constexpr u8 ci = 42u;
    const float expectedU = (static_cast<float>(ci) + 0.5f) / 256.0f;
    constexpr float expectedV = 0.5f;

    VoxData vox = makeVox(1, 1, 1);
    setVoxel(vox, 0, 0, 0, ci);

    const MeshData mesh = greedyMeshVoxels(vox);
    ASSERT_EQ(mesh.vertices.size(), 24u);

    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        EXPECT_NEAR(mesh.vertices[i].uv[0], expectedU, k_eps)
            << "wrong UV.u at vertex " << i;
        EXPECT_NEAR(mesh.vertices[i].uv[1], expectedV, k_eps)
            << "wrong UV.v at vertex " << i;
    }
}

// ─── 7. NormalsPerFace ────────────────────────────────────────────────────────

TEST(GreedyMeshVoxels, NormalsPerFace)
{
    // One 1×1×1 voxel → 6 face groups of 4 vertices each (24 total).
    // The mesher processes: axis 0 sign+1 (+X), axis 0 sign-1 (-X),
    //                       axis 1 sign+1 (+Y), axis 1 sign-1 (-Y),
    //                       axis 2 sign+1 (+Z), axis 2 sign-1 (-Z).
    // Each group of 4 consecutive vertices shares the same normal.
    VoxData vox = makeVox(1, 1, 1);
    setVoxel(vox, 0, 0, 0, 1u);

    const MeshData mesh = greedyMeshVoxels(vox);
    ASSERT_EQ(mesh.vertices.size(), 24u);

    // Expected normals in processing order:
    //   verts  0- 3: +X face → normal (1, 0, 0)
    //   verts  4- 7: -X face → normal (-1, 0, 0)
    //   verts  8-11: +Y face → normal (0, 1, 0)
    //   verts 12-15: -Y face → normal (0, -1, 0)
    //   verts 16-19: +Z face → normal (0, 0, 1)
    //   verts 20-23: -Z face → normal (0, 0, -1)
    const std::array<std::array<float, 3>, 6> expectedNormals =
    {{
        { 1.0f,  0.0f,  0.0f},
        {-1.0f,  0.0f,  0.0f},
        { 0.0f,  1.0f,  0.0f},
        { 0.0f, -1.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f},
        { 0.0f,  0.0f, -1.0f},
    }};

    for (int face = 0; face < 6; ++face)
    {
        for (int vi = 0; vi < 4; ++vi)
        {
            const auto& v = mesh.vertices[face * 4 + vi];
            EXPECT_NEAR(v.normal[0], expectedNormals[face][0], k_eps)
                << "face " << face << " vert " << vi << " normal.x";
            EXPECT_NEAR(v.normal[1], expectedNormals[face][1], k_eps)
                << "face " << face << " vert " << vi << " normal.y";
            EXPECT_NEAR(v.normal[2], expectedNormals[face][2], k_eps)
                << "face " << face << " vert " << vi << " normal.z";
        }
    }
}
