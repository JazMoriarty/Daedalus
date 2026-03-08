// vox_writer.h
// Writes a voxel grid to a MagicaVoxel .vox binary file (version 150).
//
// Usage:
//   VoxGrid grid;
//   grid.sizeX = grid.sizeY = grid.sizeZ = 32;
//   grid.palette[1] = {255, 128, 0, 255};   // RGBA palette entry
//   grid.voxels.push_back({0, 0, 0, 1});    // x, y, z, palette index
//   bool ok = write_vox("output.vox", grid);
//
// Format reference: https://github.com/ephtracy/voxel-model/blob/master/MagicaVoxel-file-format-vox.txt

#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace daedalus::tools
{

// ─── Data types ──────────────────────────────────────────────────────────────

/// Single voxel: integer grid position + palette index (1-255; 0 = empty).
struct Voxel
{
    uint8_t x, y, z;
    uint8_t colorIndex;  ///< 1-based palette index (0 = transparent/empty).
};

/// RGBA colour entry for the .vox palette (256 entries total).
struct VoxColor
{
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

/// Complete voxel grid description passed to write_vox().
struct VoxGrid
{
    uint32_t sizeX = 32;
    uint32_t sizeY = 32;
    uint32_t sizeZ = 32;

    /// Filled voxels (colorIndex must be in [1, 255]).
    std::vector<Voxel> voxels;

    /// 256-entry RGBA palette.  Entry 0 is traditionally clear;
    /// entries 1–255 are user-defined colours.
    VoxColor palette[256] = {};
};

// ─── Internal helpers ─────────────────────────────────────────────────────────

namespace detail
{

static void write_u32(std::ostream& out, uint32_t v)
{
    out.write(reinterpret_cast<const char*>(&v), 4);
}

static void write_chunk(std::ostream& out, const char id[4], const std::vector<uint8_t>& data)
{
    out.write(id, 4);
    write_u32(out, static_cast<uint32_t>(data.size()));  // chunk content size
    write_u32(out, 0u);                                   // children bytes (none)
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
}

static void append_u32(std::vector<uint8_t>& buf, uint32_t v)
{
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    buf.insert(buf.end(), p, p + 4);
}

} // namespace detail

// ─── write_vox ────────────────────────────────────────────────────────────────

/// Serialise `grid` to a MagicaVoxel .vox file at `path`.
/// Returns true on success, false if the file could not be opened.
inline bool write_vox(const std::string& path, const VoxGrid& grid)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    // ── File header: "VOX " magic + version 150 ───────────────────────────────
    out.write("VOX ", 4);
    detail::write_u32(out, 150u);

    // ── MAIN chunk (wraps all child chunks) ───────────────────────────────────
    // We need to know the total children byte count up front.
    // Build each child chunk into a buffer first, then write MAIN.

    // SIZE chunk: 12 bytes
    std::vector<uint8_t> sizeData;
    detail::append_u32(sizeData, grid.sizeX);
    detail::append_u32(sizeData, grid.sizeY);
    detail::append_u32(sizeData, grid.sizeZ);

    // XYZI chunk: 4 bytes count + N × 4 bytes
    std::vector<uint8_t> xyziData;
    detail::append_u32(xyziData, static_cast<uint32_t>(grid.voxels.size()));
    for (const Voxel& v : grid.voxels)
    {
        xyziData.push_back(v.x);
        xyziData.push_back(v.y);
        xyziData.push_back(v.z);
        xyziData.push_back(v.colorIndex);
    }

    // RGBA chunk: 256 × 4 bytes
    std::vector<uint8_t> rgbaData;
    rgbaData.reserve(256 * 4);
    for (const VoxColor& c : grid.palette)
    {
        rgbaData.push_back(c.r);
        rgbaData.push_back(c.g);
        rgbaData.push_back(c.b);
        rgbaData.push_back(c.a);
    }

    // Each child chunk: 4 (id) + 4 (content sz) + 4 (children sz) + content
    auto chunkBytes = [](const std::vector<uint8_t>& data) -> uint32_t
    {
        return 12u + static_cast<uint32_t>(data.size());
    };

    const uint32_t childrenBytes = chunkBytes(sizeData)
                                 + chunkBytes(xyziData)
                                 + chunkBytes(rgbaData);

    // MAIN chunk header: id + content=0 + children byte count
    out.write("MAIN", 4);
    detail::write_u32(out, 0u);               // no direct content
    detail::write_u32(out, childrenBytes);    // child chunks follow

    // Write child chunks
    detail::write_chunk(out, "SIZE", sizeData);
    detail::write_chunk(out, "XYZI", xyziData);
    detail::write_chunk(out, "RGBA", rgbaData);

    return out.good();
}

// ─── VoxelizerConfig ─────────────────────────────────────────────────────────
// Configuration for surface_voxelizer.h::voxelize_gltf().
// Lives here so test_vox_bake.cpp can check defaults without including cgltf.

struct VoxelizerConfig
{
    uint32_t resolution = 64u;   ///< Voxel grid size on each axis (1–255).
    float    worldScale = 1.0f;  ///< Uniform scale applied to mesh before voxelisation.
};

} // namespace daedalus::tools
