// vox_types.h
// CPU-side data types for the MagicaVoxel (.vox) asset pipeline.
//
// Separation of concerns:
//   VoxData       — intermediate representation produced by the binary .vox parser.
//   VoxMeshResult — final output of IAssetLoader::loadVox(): a baked mesh + raw
//                   palette bytes ready for GPU upload as a 256×1 texture.
//
// The voxel asset pipeline:
//   .vox file  ─► parseVox()  ─► VoxData  ─► greedyMeshVoxels()  ─► MeshData
//                                            └─► paletteRGBA (256 × RGBA8)
//
// Both MeshData and paletteRGBA are returned together as VoxMeshResult so the
// caller can upload them to GPU with a single device call each.

#pragma once

#include "daedalus/render/mesh_data.h"  // MeshData (avoids circular dep with i_asset_loader.h)
#include "daedalus/core/types.h"

#include <array>
#include <vector>

namespace daedalus::render
{

// ─── VoxData ──────────────────────────────────────────────────────────────────
// Parsed contents of one MagicaVoxel model.
//
// Coordinate system: MagicaVoxel uses right-hand Z-up.  The parser does NOT
// remap axes — the mesher and the application should account for this if needed
// (e.g. swap Y/Z to align with engine Y-up convention).
//
// voxels layout: flat array of u8 colour indices.
//   index = x + sizeX * (y + sizeY * z)
//   value = 0         → empty cell
//   value = 1..255    → filled; palette[value] carries the RGBA colour.

struct VoxData
{
    u32 sizeX = 0;  ///< Dimension along X.
    u32 sizeY = 0;  ///< Dimension along Y (Z-up in MagicaVoxel, pre-remap).
    u32 sizeZ = 0;  ///< Dimension along Z.

    /// Flat colour-index array (sizeX × sizeY × sizeZ bytes).
    std::vector<u8> voxels;

    /// RGBA palette — 256 entries × 4 bytes.
    /// Entry 0 is reserved/unused; entries 1-255 map to colour indices in voxels.
    std::array<u8, 256 * 4> palette{};

    /// Returns the colour index at (x, y, z), or 0 if out of bounds.
    [[nodiscard]] u8 at(u32 x, u32 y, u32 z) const noexcept
    {
        if (x >= sizeX || y >= sizeY || z >= sizeZ) { return 0u; }
        return voxels[x + sizeX * (y + sizeY * z)];
    }
};

// ─── VoxMeshResult ────────────────────────────────────────────────────────────
// Output of IAssetLoader::loadVox(): everything needed to create GPU resources.
//
// The caller uploads mesh to VBO/IBO and paletteRGBA to a 256×1 RGBA8 texture.
// That texture becomes the albedo map on the VoxelObjectComponent's Material.
// Vertices UV into the palette texture: u = (colourIndex + 0.5) / 256, v = 0.5.

struct VoxMeshResult
{
    MeshData               mesh;         ///< Greedy-meshed geometry (interleaved StaticMeshVertex).
    std::array<u8, 256*4>  paletteRGBA;  ///< 256 × RGBA8 rows ready for createTexture.
};

} // namespace daedalus::render
