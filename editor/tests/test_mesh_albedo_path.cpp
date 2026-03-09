// test_mesh_albedo_path.cpp
// Tests that IAssetLoader::loadMesh() populates MeshData::albedoPath
// from the glTF baseColorTexture URI.

#include "daedalus/render/i_asset_loader.h"
#include "daedalus/render/mesh_data.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

using namespace daedalus::render;

// ─── Helpers ─────────────────────────────────────────────────────────────────

struct TempGltfFile
{
    std::filesystem::path path;
    explicit TempGltfFile(const char* name)
        : path(std::filesystem::temp_directory_path() / name) {}
    ~TempGltfFile() { std::filesystem::remove(path); }
};

/// Write a minimal glTF 2.0 JSON to disk.  The single triangle primitive has a
/// PBR material whose baseColorTexture points to the given URI string.
/// The buffer embeds 36 zeroed bytes (3 × vec3f32) as base64 so cgltf
/// can parse without external files.  We care only about albedoPath, not
/// geometry accuracy.
static void writeMinimalGltf(const std::filesystem::path& gltfPath,
                              const std::string&           albedoUri)
{
    // base64(36 zero bytes) = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
    const std::string json = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [{ "mesh": 0 }],
  "meshes": [{
    "primitives": [{
      "attributes": { "POSITION": 0 },
      "material": 0
    }]
  }],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0 }
    }
  }],
  "textures": [{ "source": 0 }],
  "images": [{ "uri": ")" + albedoUri + R"(" }],
  "accessors": [{
    "bufferView": 0,
    "componentType": 5126,
    "count": 3,
    "type": "VEC3",
    "min": [0.0, 0.0, 0.0],
    "max": [1.0, 1.0, 0.0]
  }],
  "bufferViews": [{ "buffer": 0, "byteOffset": 0, "byteLength": 36 }],
  "buffers": [{
    "byteLength": 36,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
  }]
})";
    std::ofstream ofs(gltfPath);
    ofs << json;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

TEST(MeshAlbedoPath, ExtractedFromGltfMaterial)
{
    TempGltfFile tmp("test_albedo_path.gltf");
    writeMinimalGltf(tmp.path, "textures/albedo.png");

    auto loader = makeAssetLoader();
    auto result = loader->loadMesh(tmp.path);

    // The mesh should load successfully (even if positions are zero-vectors).
    ASSERT_TRUE(result.has_value()) << "loadMesh failed unexpectedly";
    EXPECT_EQ(result->albedoPath, "textures/albedo.png");
}

TEST(MeshAlbedoPath, EmptyWhenNoMaterial)
{
    TempGltfFile tmp("test_no_material.gltf");

    // Write a minimal glTF with no material.
    const std::string gltfJson = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [{ "mesh": 0 }],
  "meshes": [{
    "primitives": [{
      "attributes": { "POSITION": 0 }
    }]
  }],
  "accessors": [{
    "bufferView": 0,
    "componentType": 5126,
    "count": 3,
    "type": "VEC3",
    "min": [0.0, 0.0, 0.0],
    "max": [1.0, 1.0, 0.0]
  }],
  "bufferViews": [{ "buffer": 0, "byteOffset": 0, "byteLength": 36 }],
  "buffers": [{
    "byteLength": 36,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
  }]
})";
    {
        std::ofstream ofs(tmp.path);
        ofs << gltfJson;
    } // flush + close before reading

    auto loader = makeAssetLoader();
    auto result = loader->loadMesh(tmp.path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->albedoPath, "");
}

TEST(MeshAlbedoPath, FlatUriPreserved)
{
    TempGltfFile tmp("test_flat_uri.gltf");
    writeMinimalGltf(tmp.path, "albedo.png");

    auto loader = makeAssetLoader();
    auto result = loader->loadMesh(tmp.path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->albedoPath, "albedo.png");
}
