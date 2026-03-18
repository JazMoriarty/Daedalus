// mipmap_generator.h
// CPU-side mipmap generation utilities.
//
// Generates a full mip chain from a source RGBA8 image using a simple box
// filter (2×2 average). This is sufficient quality for real-time rendering
// and fast enough to run on-demand during asset import.
//
// The mip chain is laid out sequentially in memory: mip 0 (full res) followed
// by mip 1 (half res), mip 2 (quarter res), etc., down to 1×1.

#pragma once

#include "daedalus/core/types.h"
#include <vector>

namespace daedalus::render
{

/// Mipmap chain result containing all mip levels laid out sequentially.
struct MipmapChain
{
    u32                width;        ///< Base mip (level 0) width
    u32                height;       ///< Base mip (level 0) height
    u32                mipCount;     ///< Total mip levels (including base)
    std::vector<u8>    data;         ///< All mips sequentially: RGBA8 format
    std::vector<usize> mipOffsets;   ///< Byte offset of each mip level in data[]
    std::vector<u32>   mipWidths;    ///< Width of each mip level
    std::vector<u32>   mipHeights;   ///< Height of each mip level
};

/// Generate a full mipmap chain from RGBA8 source data using box filter.
///
/// @param srcPixels  Source image in RGBA8 format (4 bytes per pixel)
/// @param width      Source image width (must be > 0)
/// @param height     Source image height (must be > 0)
/// @return           Complete mipmap chain with all levels down to 1×1
///
/// Mip count = floor(log2(max(width, height))) + 1.
/// Example: 512×512 → 10 mips (512, 256, 128, 64, 32, 16, 8, 4, 2, 1).
MipmapChain generateMipmapChain(const u8* srcPixels, u32 width, u32 height);

/// Calculate the total number of mip levels for a given texture size.
///
/// @param width   Texture width
/// @param height  Texture height
/// @return        Mip count: floor(log2(max(width, height))) + 1
constexpr u32 calculateMipCount(u32 width, u32 height) noexcept
{
    u32 maxDim = (width > height) ? width : height;
    u32 mips = 1;
    while (maxDim > 1)
    {
        maxDim >>= 1;
        ++mips;
    }
    return mips;
}

/// Calculate the size in bytes of a single mip level (RGBA8 format).
constexpr usize calculateMipSize(u32 width, u32 height) noexcept
{
    return static_cast<usize>(width) * static_cast<usize>(height) * 4u;
}

} // namespace daedalus::render
