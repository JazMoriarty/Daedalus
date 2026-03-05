// i_asset_loader.h
// Pure interface for loading source assets (textures, meshes) into GPU-resident
// resources.  Concrete implementations are hidden in src/ and obtained through
// makeAssetLoader().
//
// Error handling follows the project standard: std::expected<T, AssetError>.
// No exceptions are thrown; all failures are explicit and typed.
//
// Reference: design_spec.md §Interface-First Design — IAssetLoader is a named
// required interface alongside IRenderDevice, IPhysicsWorld, IAudioEngine.

#pragma once

#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/vertex_types.h"
#include "daedalus/core/types.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <vector>

namespace daedalus::render
{

// ─── AssetError ───────────────────────────────────────────────────────────────
// Typed error code returned by all IAssetLoader operations.

enum class AssetError : u32
{
    FileNotFound,   ///< The file path does not exist or cannot be opened.
    ParseError,     ///< The file could not be decoded (corrupt data, unsupported format).
    UploadError,    ///< GPU resource creation failed.
};

// ─── MeshData ─────────────────────────────────────────────────────────────────
// CPU-side mesh data returned by IAssetLoader::loadMesh().
// The caller is responsible for uploading this to GPU buffers.

struct MeshData
{
    std::vector<StaticMeshVertex> vertices;
    std::vector<u32>              indices;
};

// ─── IAssetLoader ─────────────────────────────────────────────────────────────
// Loads source assets from disk into GPU-ready or CPU-ready data structures.
// Obtain a concrete instance via makeAssetLoader().

class IAssetLoader
{
public:
    virtual ~IAssetLoader() = default;

    IAssetLoader(const IAssetLoader&)            = delete;
    IAssetLoader& operator=(const IAssetLoader&) = delete;

    // ─── Texture loading ──────────────────────────────────────────────────────

    /// Load a PNG / JPEG / TGA image from disk and upload it to a GPU texture.
    ///
    /// @param device  RHI device used to create the texture resource.
    /// @param path    Absolute or relative path to the image file.
    /// @param sRGB    If true, the texture is created with an sRGB format so
    ///                the hardware linearises values on sample.
    /// @return        Owning pointer to the uploaded texture, or AssetError.
    [[nodiscard]] virtual std::expected<std::unique_ptr<rhi::ITexture>, AssetError>
    loadTexture(rhi::IRenderDevice&          device,
                const std::filesystem::path& path,
                bool                         sRGB) = 0;

    // ─── Mesh loading ─────────────────────────────────────────────────────────

    /// Load a glTF 2.0 (.gltf / .glb) file and return the first mesh primitive
    /// as interleaved StaticMeshVertex data ready for GPU upload.
    ///
    /// Normals and tangents are taken from the file if present; tangents are
    /// computed from UVs if absent.  All primitives are triangulated.
    ///
    /// @param path    Absolute or relative path to the glTF / GLB file.
    /// @return        CPU-side mesh data, or AssetError.
    [[nodiscard]] virtual std::expected<MeshData, AssetError>
    loadMesh(const std::filesystem::path& path) = 0;

protected:
    IAssetLoader() = default;
};

// ─── Factory ──────────────────────────────────────────────────────────────────

/// Construct the platform-independent AssetLoader implementation.
[[nodiscard]] std::unique_ptr<IAssetLoader> makeAssetLoader();

} // namespace daedalus::render
