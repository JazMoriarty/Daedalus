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
#include "daedalus/render/vox_types.h"
#include "daedalus/render/vox_mesher.h"
#include "daedalus/render/rhi/rhi_types.h"
#include "daedalus/core/assert.h"

#include <cmath>
#include <cstring>
#include <array>
#include <fstream>

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

    // ─── Vox loading ──────────────────────────────────────────────────────────

    [[nodiscard]] std::expected<VoxMeshResult, AssetError>
    loadVox(const std::filesystem::path& path) override
    {
        // ── Read file into memory ────────────────────────────────────────────
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
            return std::unexpected(AssetError::FileNotFound);

        const auto fileSize = static_cast<std::size_t>(f.tellg());
        f.seekg(0, std::ios::beg);
        std::vector<u8> buf(fileSize);
        if (!f.read(reinterpret_cast<char*>(buf.data()),
                    static_cast<std::streamsize>(fileSize)))
            return std::unexpected(AssetError::ParseError);

        const u8* p   = buf.data();
        const u8* end = p + buf.size();

        // ── Read helpers ────────────────────────────────────────────────────
        auto readU32 = [](const u8*& ptr) -> u32
        {
            u32 v = 0;
            std::memcpy(&v, ptr, 4);
            ptr += 4;
            return v;
        };

        // ── Validate magic + version ─────────────────────────────────────────
        if (end - p < 8 ||
            p[0] != 'V' || p[1] != 'O' || p[2] != 'X' || p[3] != ' ')
            return std::unexpected(AssetError::ParseError);
        p += 4;
        const u32 version = readU32(p);
        (void)version;  // 150 or 200 — we do not need to branch on this

        // ── Expect MAIN chunk ────────────────────────────────────────────────
        if (end - p < 12 ||
            p[0] != 'M' || p[1] != 'A' || p[2] != 'I' || p[3] != 'N')
            return std::unexpected(AssetError::ParseError);
        p += 4;
        const u32 mainContent  = readU32(p);
        const u32 mainChildren = readU32(p);
        p += mainContent;  // MAIN content is always 0 bytes in practice

        const u8* childEnd = p + mainChildren;

        // ── Walk child chunks ────────────────────────────────────────────────
        VoxData vox;
        bool    hasPalette = false;
        std::array<u8, 256 * 4> rgba{};

        while (p + 12 <= childEnd)
        {
            char id[4];
            std::memcpy(id, p, 4); p += 4;
            const u32 contentSize  = readU32(p);
            const u32 childrenSize = readU32(p);
            const u8* chunkEnd     = p + contentSize;

            if (std::memcmp(id, "SIZE", 4) == 0 && contentSize >= 12)
            {
                vox.sizeX = readU32(p);
                vox.sizeY = readU32(p);
                vox.sizeZ = readU32(p);
            }
            else if (std::memcmp(id, "XYZI", 4) == 0 && contentSize >= 4)
            {
                const u32 numVoxels = readU32(p);
                vox.voxels.assign(
                    static_cast<std::size_t>(vox.sizeX) *
                    static_cast<std::size_t>(vox.sizeY) *
                    static_cast<std::size_t>(vox.sizeZ), 0u);

                const u32 avail = (contentSize - 4) / 4;
                const u32 count = (numVoxels < avail) ? numVoxels : avail;
                for (u32 i = 0; i < count; ++i)
                {
                    const u8 vx = *p++;
                    const u8 vy = *p++;
                    const u8 vz = *p++;
                    const u8 ci = *p++;
                    if (vx < vox.sizeX && vy < vox.sizeY && vz < vox.sizeZ && ci != 0)
                        vox.voxels[vx + vox.sizeX * (vy + vox.sizeY * vz)] = ci;
                }
            }
            else if (std::memcmp(id, "RGBA", 4) == 0 && contentSize >= 256 * 4)
            {
                std::memcpy(rgba.data(), p, 256 * 4);
                hasPalette = true;
            }
            // else: unknown chunk — skip

            p = chunkEnd;
            p += childrenSize;  // skip sub-chunks (e.g. nGRP, nSHP in VOX 200)
        }

        if (vox.sizeX == 0 || vox.voxels.empty())
            return std::unexpected(AssetError::ParseError);

        // ── Default palette fallback (HSV rainbow, 255 entries, index 0 unused) ──
        if (!hasPalette)
        {
            for (int i = 1; i < 256; ++i)
            {
                const float t  = static_cast<float>(i - 1) / 254.0f;  // 0..1
                const float h6 = t * 6.0f;
                const int   sec = static_cast<int>(h6);
                const float fr  = h6 - static_cast<float>(sec);
                const u8    f_  = static_cast<u8>(fr * 255.0f + 0.5f);
                u8 r = 0, g = 0, b = 0;
                switch (sec % 6)
                {
                    case 0: r=255;    g=f_;     b=0;      break;
                    case 1: r=255-f_; g=255;    b=0;      break;
                    case 2: r=0;      g=255;    b=f_;     break;
                    case 3: r=0;      g=255-f_; b=255;    break;
                    case 4: r=f_;     g=0;      b=255;    break;
                    default:r=255;    g=0;      b=255-f_; break;
                }
                rgba[i * 4 + 0] = r;
                rgba[i * 4 + 1] = g;
                rgba[i * 4 + 2] = b;
                rgba[i * 4 + 3] = 255u;
            }
        }

        VoxMeshResult res;
        res.mesh       = greedyMeshVoxels(vox);
        res.paletteRGBA = rgba;
        return res;
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

        // ── Albedo path (baseColor texture URI from first primitive's material) ──
        if (prim.material)
        {
            const cgltf_material& mat = *prim.material;
            if (mat.has_pbr_metallic_roughness)
            {
                const cgltf_texture* tex =
                    mat.pbr_metallic_roughness.base_color_texture.texture;
                if (tex && tex->image && tex->image->uri)
                    result.albedoPath = tex->image->uri;
            }
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
