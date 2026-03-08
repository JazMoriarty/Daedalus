// surface_voxelizer.h
// Surface voxelisation: loads a glTF 2.0 mesh with cgltf and rasterises each
// triangle into a VoxGrid using conservative axis-aligned bounding-box fill.
//
// The algorithm:
//   1. Iterate every triangle in every primitive of every mesh in the glTF.
//   2. Transform each triangle vertex into voxel-grid space.
//   3. Conservative AABB: all voxels whose centres lie within the triangle AABB
//      plus one voxel of padding are marked as filled.
//      (Full triangle–voxel intersection is a planned upgrade; the AABB
//       approximation is sufficient for solid or thick-walled meshes.)
//   4. Colour is taken from the material's baseColorFactor, quantised to the
//      nearest entry in a dynamically-built palette (up to 255 entries).

#pragma once

// CGLTF_IMPLEMENTATION must be defined in exactly one translation unit.
// For DaedalusVoxBake, this is done in cgltf_impl.cpp.
// Do NOT include surface_voxelizer.h in the same TU that defines CGLTF_IMPLEMENTATION.
#include <cgltf.h>

#include "vox_writer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace daedalus::tools
{

// VoxelizerConfig is defined in vox_writer.h (so tests can check defaults
// without pulling in cgltf headers).

// ─── voxelize_gltf

/// Load `gltfPath`, voxelise all meshes into `grid` using `cfg`.
/// Returns an empty string on success, or an error message on failure.
inline std::string voxelize_gltf(const std::string& gltfPath,
                                  VoxGrid&           grid,
                                  const VoxelizerConfig& cfg = {})
{
    cgltf_options opts{};
    cgltf_data* data = nullptr;

    if (cgltf_parse_file(&opts, gltfPath.c_str(), &data) != cgltf_result_success)
        return "cgltf: failed to parse " + gltfPath;

    if (cgltf_load_buffers(&opts, data, gltfPath.c_str()) != cgltf_result_success)
    {
        cgltf_free(data);
        return "cgltf: failed to load buffers for " + gltfPath;
    }

    if (cgltf_validate(data) != cgltf_result_success)
    {
        cgltf_free(data);
        return "cgltf: validation failed for " + gltfPath;
    }

    // ── Determine world AABB for normalisation ────────────────────────────────
    glm::vec3 worldMin(  1e30f);
    glm::vec3 worldMax( -1e30f);

    // Helper: read one float3 from an accessor at index i.
    auto readPos = [](const cgltf_accessor* acc, size_t i) -> glm::vec3
    {
        float v[3] = {};
        cgltf_accessor_read_float(acc, i, v, 3);
        return glm::vec3(v[0], v[1], v[2]);
    };

    for (size_t mi = 0; mi < data->meshes_count; ++mi)
    {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives_count; ++pi)
        {
            const cgltf_primitive& prim = mesh.primitives[pi];
            for (size_t ai = 0; ai < prim.attributes_count; ++ai)
            {
                if (prim.attributes[ai].type != cgltf_attribute_type_position) continue;
                const cgltf_accessor* acc = prim.attributes[ai].data;
                for (size_t vi = 0; vi < acc->count; ++vi)
                {
                    const glm::vec3 pos = readPos(acc, vi) * cfg.worldScale;
                    worldMin = glm::min(worldMin, pos);
                    worldMax = glm::max(worldMax, pos);
                }
            }
        }
    }

    const glm::vec3 worldExtent = worldMax - worldMin;
    const float     maxExtent   = std::max({ worldExtent.x, worldExtent.y, worldExtent.z });
    const float     voxelSize   = maxExtent / static_cast<float>(cfg.resolution);

    grid.sizeX = cfg.resolution;
    grid.sizeY = cfg.resolution;
    grid.sizeZ = cfg.resolution;
    grid.voxels.clear();

    // ── Palette builder (lazy: up to 255 unique RGBA entries) ─────────────────
    // Maps packed uint32_t RGBA → 1-based palette index.
    std::unordered_map<uint32_t, uint8_t> paletteMap;
    uint8_t nextPaletteIdx = 1;

    auto paletteIndex = [&](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint8_t
    {
        if (nextPaletteIdx > 255)
            return 1;  // overflow: reuse first colour
        const uint32_t key = (uint32_t(r) << 24) | (uint32_t(g) << 16) |
                             (uint32_t(b) <<  8) | uint32_t(a);
        auto it = paletteMap.find(key);
        if (it != paletteMap.end())
            return it->second;
        const uint8_t idx = nextPaletteIdx++;
        paletteMap[key] = idx;
        grid.palette[idx] = { r, g, b, a };
        return idx;
    };

    // ── Voxel set (prevent duplicates) ────────────────────────────────────────
    // Key = packed (x << 16 | y << 8 | z) — assumes grid ≤ 256³.
    std::unordered_map<uint32_t, uint8_t> voxelSet;

    auto addVoxel = [&](uint32_t x, uint32_t y, uint32_t z, uint8_t colorIdx)
    {
        if (x >= grid.sizeX || y >= grid.sizeY || z >= grid.sizeZ) return;
        const uint32_t key = (x << 16) | (y << 8) | z;
        if (voxelSet.count(key)) return;
        voxelSet[key] = colorIdx;
        grid.voxels.push_back({ (uint8_t)x, (uint8_t)y, (uint8_t)z, colorIdx });
    };

    // ── Rasterise triangles ───────────────────────────────────────────────────
    for (size_t mi = 0; mi < data->meshes_count; ++mi)
    {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives_count; ++pi)
        {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            // Material colour → palette
            uint8_t r = 200, g = 200, b = 200, a = 255;
            if (prim.material && prim.material->has_pbr_metallic_roughness)
            {
                const auto& bf = prim.material->pbr_metallic_roughness.base_color_factor;
                r = static_cast<uint8_t>(bf[0] * 255.0f + 0.5f);
                g = static_cast<uint8_t>(bf[1] * 255.0f + 0.5f);
                b = static_cast<uint8_t>(bf[2] * 255.0f + 0.5f);
                a = static_cast<uint8_t>(bf[3] * 255.0f + 0.5f);
            }
            const uint8_t colorIdx = paletteIndex(r, g, b, a);

            // Find POSITION accessor
            const cgltf_accessor* posAcc = nullptr;
            for (size_t ai = 0; ai < prim.attributes_count; ++ai)
            {
                if (prim.attributes[ai].type == cgltf_attribute_type_position)
                {
                    posAcc = prim.attributes[ai].data;
                    break;
                }
            }
            if (!posAcc) continue;

            // Iterate triangles (indexed or non-indexed)
            const size_t triCount = prim.indices ? (prim.indices->count / 3)
                                                 : (posAcc->count / 3);
            for (size_t ti = 0; ti < triCount; ++ti)
            {
                glm::vec3 v[3];
                for (int k = 0; k < 3; ++k)
                {
                    size_t vi;
                    if (prim.indices)
                        vi = cgltf_accessor_read_index(prim.indices, ti * 3 + k);
                    else
                        vi = ti * 3 + k;

                    const glm::vec3 worldPos = readPos(posAcc, vi) * cfg.worldScale;
                    // Map to voxel grid: shift by worldMin so AABB starts at 0
                    v[k] = (worldPos - worldMin) / voxelSize;
                }

                // Conservative triangle AABB (with 1-voxel padding)
                // glm::min/max do not accept initializer lists; chain two-arg overloads.
                const glm::vec3 triMin = glm::max(
                    glm::floor(glm::min(glm::min(v[0], v[1]), v[2])) - 1.0f,
                    glm::vec3(0.0f));
                const glm::vec3 triMax = glm::min(
                    glm::ceil(glm::max(glm::max(v[0], v[1]), v[2])) + 1.0f,
                    glm::vec3(static_cast<float>(cfg.resolution) - 1.0f));

                for (uint32_t vz = (uint32_t)triMin.z; vz <= (uint32_t)triMax.z; ++vz)
                for (uint32_t vy = (uint32_t)triMin.y; vy <= (uint32_t)triMax.y; ++vy)
                for (uint32_t vx = (uint32_t)triMin.x; vx <= (uint32_t)triMax.x; ++vx)
                    addVoxel(vx, vy, vz, colorIdx);
            }
        }
    }

    cgltf_free(data);
    return {};  // success
}

} // namespace daedalus::tools
