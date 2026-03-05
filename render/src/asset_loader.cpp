// asset_loader.cpp
// Concrete implementation of IAssetLoader.
//
// Texture loading: stb_image decodes PNG/JPEG/TGA to RGBA8 pixels, which are
// uploaded via TextureDescriptor::initData in a single device call.
//
// Mesh loading: cgltf parses glTF 2.0 (.gltf / .glb).  The first mesh
// primitive is unpacked into interleaved StaticMeshVertex data.  Normals and
// tangents from the file are used as-is; if absent, normals are set to the
// face normal and tangents to a default (+X, handedness +1).

// ─── stb_image implementation (compiled once here, private to this TU) ───────
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ─── cgltf implementation (compiled once here, private to this TU) ───────────
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "daedalus/render/i_asset_loader.h"
#include "daedalus/render/rhi/rhi_types.h"
#include "daedalus/core/assert.h"

#include <cmath>
#include <cstring>
#include <array>

namespace daedalus::render
{

// ─────────────────────────────────────────────────────────────────────────────

namespace
{

// ─── cgltf accessor helpers ───────────────────────────────────────────────────

/// Read a scalar float from an accessor at element index i.
float readFloat(const cgltf_accessor* acc, cgltf_size i)
{
    float out = 0.0f;
    cgltf_accessor_read_float(acc, i, &out, 1);
    return out;
}

/// Read a float3 from an accessor at element index i.
std::array<float, 3> readFloat3(const cgltf_accessor* acc, cgltf_size i)
{
    std::array<float, 3> out{};
    cgltf_accessor_read_float(acc, i, out.data(), 3);
    return out;
}

/// Read a float2 from an accessor at element index i.
std::array<float, 2> readFloat2(const cgltf_accessor* acc, cgltf_size i)
{
    std::array<float, 2> out{};
    cgltf_accessor_read_float(acc, i, out.data(), 2);
    return out;
}

/// Read a float4 from an accessor at element index i.
std::array<float, 4> readFloat4(const cgltf_accessor* acc, cgltf_size i)
{
    std::array<float, 4> out{};
    cgltf_accessor_read_float(acc, i, out.data(), 4);
    return out;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────

class AssetLoader final : public IAssetLoader
{
public:
    AssetLoader()  = default;
    ~AssetLoader() = default;

    // ─── Texture loading ──────────────────────────────────────────────────────

    [[nodiscard]] std::expected<std::unique_ptr<rhi::ITexture>, AssetError>
    loadTexture(rhi::IRenderDevice&          device,
                const std::filesystem::path& path,
                bool                         sRGB) override
    {
        const std::string pathStr = path.string();

        int w = 0, h = 0, srcChannels = 0;
        // Force 4-channel RGBA output regardless of source format.
        stbi_uc* pixels = stbi_load(pathStr.c_str(), &w, &h, &srcChannels, 4);
        if (!pixels)
            return std::unexpected(AssetError::FileNotFound);

        rhi::TextureDescriptor d;
        d.width     = static_cast<u32>(w);
        d.height    = static_cast<u32>(h);
        d.format    = sRGB ? rhi::TextureFormat::RGBA8Unorm_sRGB
                           : rhi::TextureFormat::RGBA8Unorm;
        d.usage     = rhi::TextureUsage::ShaderRead;
        d.initData  = pixels;
        d.debugName = path.filename().string();

        auto texture = device.createTexture(d);
        stbi_image_free(pixels);

        if (!texture)
            return std::unexpected(AssetError::UploadError);

        return texture;
    }

    // ─── Mesh loading ─────────────────────────────────────────────────────────

    [[nodiscard]] std::expected<MeshData, AssetError>
    loadMesh(const std::filesystem::path& path) override
    {
        const std::string pathStr = path.string();

        cgltf_options opts{};
        cgltf_data*   data = nullptr;

        if (cgltf_parse_file(&opts, pathStr.c_str(), &data) != cgltf_result_success)
            return std::unexpected(AssetError::FileNotFound);

        // Load external buffers (.bin files referenced by .gltf; no-op for .glb).
        if (cgltf_load_buffers(&opts, data, pathStr.c_str()) != cgltf_result_success)
        {
            cgltf_free(data);
            return std::unexpected(AssetError::ParseError);
        }

        if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0)
        {
            cgltf_free(data);
            return std::unexpected(AssetError::ParseError);
        }

        const cgltf_primitive& prim = data->meshes[0].primitives[0];

        // ── Locate attribute accessors ────────────────────────────────────────
        const cgltf_accessor* posAcc     = nullptr;
        const cgltf_accessor* normAcc    = nullptr;
        const cgltf_accessor* uvAcc      = nullptr;
        const cgltf_accessor* tanAcc     = nullptr;

        for (cgltf_size a = 0; a < prim.attributes_count; ++a)
        {
            const cgltf_attribute& attr = prim.attributes[a];
            switch (attr.type)
            {
                case cgltf_attribute_type_position:   posAcc  = attr.data; break;
                case cgltf_attribute_type_normal:     normAcc = attr.data; break;
                case cgltf_attribute_type_texcoord:
                    if (attr.index == 0) uvAcc = attr.data;
                    break;
                case cgltf_attribute_type_tangent:    tanAcc  = attr.data; break;
                default: break;
            }
        }

        if (!posAcc)
        {
            cgltf_free(data);
            return std::unexpected(AssetError::ParseError);
        }

        const cgltf_size vertCount = posAcc->count;
        MeshData result;
        result.vertices.resize(vertCount);

        for (cgltf_size i = 0; i < vertCount; ++i)
        {
            StaticMeshVertex& v = result.vertices[i];

            // Position (required)
            auto p  = readFloat3(posAcc, i);
            v.pos[0] = p[0]; v.pos[1] = p[1]; v.pos[2] = p[2];

            // Normal — fall back to +Y if not present
            if (normAcc)
            {
                auto n   = readFloat3(normAcc, i);
                v.normal[0] = n[0]; v.normal[1] = n[1]; v.normal[2] = n[2];
            }
            else
            {
                v.normal[0] = 0.0f; v.normal[1] = 1.0f; v.normal[2] = 0.0f;
            }

            // UV — fall back to (0,0)
            if (uvAcc)
            {
                auto uv  = readFloat2(uvAcc, i);
                v.uv[0]  = uv[0]; v.uv[1] = uv[1];
            }
            else
            {
                v.uv[0] = 0.0f; v.uv[1] = 0.0f;
            }

            // Tangent — fall back to (+X, handedness +1)
            if (tanAcc)
            {
                auto t       = readFloat4(tanAcc, i);
                v.tangent[0] = t[0]; v.tangent[1] = t[1];
                v.tangent[2] = t[2]; v.tangent[3] = t[3];
            }
            else
            {
                v.tangent[0] = 1.0f; v.tangent[1] = 0.0f;
                v.tangent[2] = 0.0f; v.tangent[3] = 1.0f;
            }
        }

        // ── Indices ───────────────────────────────────────────────────────────
        if (prim.indices)
        {
            const cgltf_size idxCount = prim.indices->count;
            result.indices.resize(idxCount);
            for (cgltf_size i = 0; i < idxCount; ++i)
                result.indices[i] = static_cast<u32>(cgltf_accessor_read_index(prim.indices, i));
        }
        else
        {
            // No index buffer: generate sequential indices
            result.indices.resize(vertCount);
            for (cgltf_size i = 0; i < vertCount; ++i)
                result.indices[i] = static_cast<u32>(i);
        }

        cgltf_free(data);
        return result;
    }
};

// ─── Factory ─────────────────────────────────────────────────────────────────

std::unique_ptr<IAssetLoader> makeAssetLoader()
{
    return std::make_unique<AssetLoader>();
}

} // namespace daedalus::render
