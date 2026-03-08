// test_vox_bake.cpp
// Unit tests for vox_writer.h and the VoxGrid data structures.
//
// Tests are pure CPU operations: no GPU, no file I/O beyond temp file write.

#include "vox_writer.h"  // also defines VoxelizerConfig

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace daedalus::tools;
namespace fs = std::filesystem;

// ─── VoxGrid defaults ────────────────────────────────────────────────────────

TEST(VoxGrid, DefaultSize)
{
    VoxGrid g;
    EXPECT_EQ(g.sizeX, 32u);
    EXPECT_EQ(g.sizeY, 32u);
    EXPECT_EQ(g.sizeZ, 32u);
}

TEST(VoxGrid, DefaultVoxelsIsEmpty)
{
    VoxGrid g;
    EXPECT_TRUE(g.voxels.empty());
}

TEST(VoxGrid, PaletteEntry0DefaultAlpha)
{
    // Entry 0 is traditionally left clear (alpha can be 0 or 255; our default is 255).
    VoxGrid g;
    EXPECT_EQ(g.palette[0].r, 0u);
    EXPECT_EQ(g.palette[0].g, 0u);
    EXPECT_EQ(g.palette[0].b, 0u);
}

// ─── VoxColor defaults ────────────────────────────────────────────────────────

TEST(VoxColor, DefaultRGBIsZero)
{
    VoxColor c;
    EXPECT_EQ(c.r, 0u);
    EXPECT_EQ(c.g, 0u);
    EXPECT_EQ(c.b, 0u);
}

TEST(VoxColor, DefaultAlphaIs255)
{
    VoxColor c;
    EXPECT_EQ(c.a, 255u);
}

// ─── Voxel field layout ───────────────────────────────────────────────────────

TEST(Voxel, SizeIsFourBytes)
{
    // Each voxel must be exactly 4 bytes for the XYZI chunk packing.
    static_assert(sizeof(Voxel) == 4, "Voxel must be 4 bytes");
    EXPECT_EQ(sizeof(Voxel), 4u);
}

TEST(Voxel, FieldOffsets)
{
    EXPECT_EQ(offsetof(Voxel, x),          0u);
    EXPECT_EQ(offsetof(Voxel, y),          1u);
    EXPECT_EQ(offsetof(Voxel, z),          2u);
    EXPECT_EQ(offsetof(Voxel, colorIndex), 3u);
}

// ─── write_vox round-trip ────────────────────────────────────────────────────

TEST(WriteVox, WritesValidMagicHeader)
{
    // Create a minimal grid and write it to a temp file, then verify the magic.
    VoxGrid g;
    g.sizeX = g.sizeY = g.sizeZ = 8;
    g.palette[1] = { 255, 0, 0, 255 };
    g.voxels.push_back({ 1, 2, 3, 1 });

    const std::string tmpPath = (fs::temp_directory_path() / "test_vox_bake.vox").string();
    ASSERT_TRUE(write_vox(tmpPath, g));

    std::ifstream in(tmpPath, std::ios::binary);
    ASSERT_TRUE(in.is_open());

    char magic[4] = {};
    in.read(magic, 4);
    EXPECT_EQ(std::string(magic, 4), "VOX ");
}

TEST(WriteVox, WritesVersion150)
{
    VoxGrid g;
    g.sizeX = g.sizeY = g.sizeZ = 4;

    const std::string tmpPath = (fs::temp_directory_path() / "test_vox_version.vox").string();
    ASSERT_TRUE(write_vox(tmpPath, g));

    std::ifstream in(tmpPath, std::ios::binary);
    ASSERT_TRUE(in.is_open());

    char magic[4];
    in.read(magic, 4);         // "VOX "
    uint32_t version = 0;
    in.read(reinterpret_cast<char*>(&version), 4);
    EXPECT_EQ(version, 150u);
}

TEST(WriteVox, ReturnsFalseForBadPath)
{
    VoxGrid g;
    // Try to write into a non-existent deep path without creating directories.
    const bool ok = write_vox("/this/path/definitely/does/not/exist/out.vox", g);
    EXPECT_FALSE(ok);
}

TEST(WriteVox, EmptyGridWritesSuccessfully)
{
    // An empty voxel list is valid — just SIZE + XYZI(0) + RGBA.
    VoxGrid g;
    g.sizeX = g.sizeY = g.sizeZ = 2;

    const std::string tmpPath = (fs::temp_directory_path() / "test_vox_empty.vox").string();
    EXPECT_TRUE(write_vox(tmpPath, g));
}

// ─── VoxelizerConfig defaults (no cgltf I/O) ─────────────────────────────────

TEST(VoxelizerConfig, DefaultResolution)
{
    VoxelizerConfig cfg;
    EXPECT_EQ(cfg.resolution, 64u);
}

TEST(VoxelizerConfig, DefaultWorldScale)
{
    VoxelizerConfig cfg;
    EXPECT_FLOAT_EQ(cfg.worldScale, 1.0f);
}
