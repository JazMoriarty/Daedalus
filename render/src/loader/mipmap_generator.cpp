// mipmap_generator.cpp
// Implementation of CPU-side mipmap generation.

#include "mipmap_generator.h"
#include "daedalus/core/assert.h"

#include <cstring>
#include <algorithm>

namespace daedalus::render
{

MipmapChain generateMipmapChain(const u8* srcPixels, u32 width, u32 height)
{
    DAEDALUS_ASSERT(srcPixels != nullptr, "Source pixels cannot be null");
    DAEDALUS_ASSERT(width > 0 && height > 0, "Texture dimensions must be > 0");

    MipmapChain result;
    result.width    = width;
    result.height   = height;
    result.mipCount = calculateMipCount(width, height);

    // Pre-allocate storage for all mip levels.
    usize totalSize = 0;
    u32 mipW = width;
    u32 mipH = height;
    for (u32 m = 0; m < result.mipCount; ++m)
    {
        totalSize += calculateMipSize(mipW, mipH);
        mipW = std::max(mipW >> 1, 1u);
        mipH = std::max(mipH >> 1, 1u);
    }

    result.data.resize(totalSize);
    result.mipOffsets.resize(result.mipCount);
    result.mipWidths.resize(result.mipCount);
    result.mipHeights.resize(result.mipCount);

    // Copy mip 0 (base level) directly from source.
    {
        const usize mip0Size = calculateMipSize(width, height);
        result.mipOffsets[0] = 0;
        result.mipWidths[0]  = width;
        result.mipHeights[0] = height;
        std::memcpy(result.data.data(), srcPixels, mip0Size);
    }

    // Generate subsequent mips via box filter (2×2 average).
    usize writeOffset = calculateMipSize(width, height);
    u32   prevW = width;
    u32   prevH = height;

    for (u32 m = 1; m < result.mipCount; ++m)
    {
        const u32 currW = std::max(prevW >> 1, 1u);
        const u32 currH = std::max(prevH >> 1, 1u);

        result.mipOffsets[m] = writeOffset;
        result.mipWidths[m]  = currW;
        result.mipHeights[m] = currH;

        const u8* prevMip = result.data.data() + result.mipOffsets[m - 1];
        u8*       currMip = result.data.data() + writeOffset;

        // Box filter: each destination pixel is the average of a 2×2 block from
        // the previous mip level.  For non-power-of-two textures, the last row
        // or column may sample beyond bounds — clamp to the edge.
        for (u32 y = 0; y < currH; ++y)
        {
            for (u32 x = 0; x < currW; ++x)
            {
                const u32 sx0 = std::min(x * 2,     prevW - 1);
                const u32 sx1 = std::min(x * 2 + 1, prevW - 1);
                const u32 sy0 = std::min(y * 2,     prevH - 1);
                const u32 sy1 = std::min(y * 2 + 1, prevH - 1);

                // Read 4 RGBA8 samples from the previous mip.
                auto sample = [&](u32 sx, u32 sy) -> const u8*
                {
                    return prevMip + ((sy * prevW + sx) * 4);
                };

                const u8* s00 = sample(sx0, sy0);
                const u8* s10 = sample(sx1, sy0);
                const u8* s01 = sample(sx0, sy1);
                const u8* s11 = sample(sx1, sy1);

                // Average all 4 channels (RGBA) separately.
                // Use u32 to avoid overflow, then divide.
                u32 r = s00[0] + s10[0] + s01[0] + s11[0];
                u32 g = s00[1] + s10[1] + s01[1] + s11[1];
                u32 b = s00[2] + s10[2] + s01[2] + s11[2];
                u32 a = s00[3] + s10[3] + s01[3] + s11[3];

                const usize dstIdx = (y * currW + x) * 4;
                currMip[dstIdx + 0] = static_cast<u8>((r + 2) >> 2);  // +2 for rounding
                currMip[dstIdx + 1] = static_cast<u8>((g + 2) >> 2);
                currMip[dstIdx + 2] = static_cast<u8>((b + 2) >> 2);
                currMip[dstIdx + 3] = static_cast<u8>((a + 2) >> 2);
            }
        }

        writeOffset += calculateMipSize(currW, currH);
        prevW = currW;
        prevH = currH;
    }

    return result;
}

} // namespace daedalus::render
