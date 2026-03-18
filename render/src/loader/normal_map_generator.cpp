// normal_map_generator.cpp
// Implementation of CPU-side normal map generation.

#include "daedalus/render/normal_map_generator.h"
#include "daedalus/core/assert.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace daedalus::render
{

// ─── Helper: Convert RGBA8 pixel to luminance (perceived brightness) ───────
static inline float rgbaToLuminance(const u8* rgba)
{
    // Rec. 709 luma coefficients (perceptually weighted)
    return 0.2126f * rgba[0] + 0.7152f * rgba[1] + 0.0722f * rgba[2];
}

// ─── Helper: Clamp sample coordinates to texture bounds ────────────────────
static inline u32 clamp(u32 val, u32 maxVal)
{
    return (val < maxVal) ? val : (maxVal - 1);
}

// ─── Helper: Sample height map with edge clamping ───────────────────────────
static inline float sampleHeight(const std::vector<float>& heightMap,
                                  u32 width, u32 height,
                                  int x, int y)
{
    const u32 cx = clamp(static_cast<u32>(std::max(0, x)), width);
    const u32 cy = clamp(static_cast<u32>(std::max(0, y)), height);
    return heightMap[cy * width + cx];
}

// ─── Step 1: Convert albedo RGBA8 to floating-point luminance height map ───
static std::vector<float> albedoToHeightMap(const u8* albedoRGBA,
                                             u32 width, u32 height,
                                             bool invert)
{
    std::vector<float> heightMap(width * height);
    
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const u32    idx  = y * width + x;
            const u8*    rgba = albedoRGBA + (idx * 4);
            const float  lum  = rgbaToLuminance(rgba) / 255.0f;  // [0,1]
            heightMap[idx] = invert ? (1.0f - lum) : lum;
        }
    }
    
    return heightMap;
}

// ─── Step 2: Optional box blur (simple NxN average) ─────────────────────────
static void applyBoxBlur(std::vector<float>& heightMap,
                         u32 width, u32 height,
                         u32 radius)
{
    if (radius == 0) return;
    
    std::vector<float> temp = heightMap;  // scratch buffer
    const int r = static_cast<int>(radius);
    
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            float sum   = 0.0f;
            int   count = 0;
            
            for (int dy = -r; dy <= r; ++dy)
            {
                for (int dx = -r; dx <= r; ++dx)
                {
                    sum += sampleHeight(heightMap, width, height,
                                        static_cast<int>(x) + dx,
                                        static_cast<int>(y) + dy);
                    ++count;
                }
            }
            
            temp[y * width + x] = sum / static_cast<float>(count);
        }
    }
    
    heightMap = std::move(temp);
}

// ─── Step 3: Downsample to mip N for frequency filtering ────────────────────
// Simplified: halve dimensions N times via box filter, then upsample back.
// This acts as a low-pass filter removing high-frequency detail.
static void applyDetailLevelFilter(std::vector<float>& heightMap,
                                    u32& width, u32& height,
                                    u32 detailLevel)
{
    if (detailLevel == 0) return;
    
    // Downsample N times (halve each time)
    u32 w = width;
    u32 h = height;
    std::vector<float> mip = heightMap;
    
    for (u32 level = 0; level < detailLevel && w > 1 && h > 1; ++level)
    {
        const u32 newW = std::max(w / 2, 1u);
        const u32 newH = std::max(h / 2, 1u);
        std::vector<float> downsampled(newW * newH);
        
        for (u32 y = 0; y < newH; ++y)
        {
            for (u32 x = 0; x < newW; ++x)
            {
                // 2x2 box filter
                const u32 sx0 = std::min(x * 2,     w - 1);
                const u32 sx1 = std::min(x * 2 + 1, w - 1);
                const u32 sy0 = std::min(y * 2,     h - 1);
                const u32 sy1 = std::min(y * 2 + 1, h - 1);
                
                const float s00 = mip[sy0 * w + sx0];
                const float s10 = mip[sy0 * w + sx1];
                const float s01 = mip[sy1 * w + sx0];
                const float s11 = mip[sy1 * w + sx1];
                
                downsampled[y * newW + x] = (s00 + s10 + s01 + s11) * 0.25f;
            }
        }
        
        mip = std::move(downsampled);
        w = newW;
        h = newH;
    }
    
    // Upsample back to original resolution (simple nearest-neighbor)
    std::vector<float> upsampled(width * height);
    const float scaleX = static_cast<float>(w) / static_cast<float>(width);
    const float scaleY = static_cast<float>(h) / static_cast<float>(height);
    
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const u32 sx = std::min(static_cast<u32>(x * scaleX), w - 1);
            const u32 sy = std::min(static_cast<u32>(y * scaleY), h - 1);
            upsampled[y * width + x] = mip[sy * w + sx];
        }
    }
    
    heightMap = std::move(upsampled);
}

// ─── Step 4: Sobel filter to compute gradients ──────────────────────────────
// Standard Sobel kernels with optional edge bias adjustment.
static void computeNormals(const std::vector<float>& heightMap,
                           u32 width, u32 height,
                           float heightScale,
                           float edgeBias,
                           std::vector<u8>& outNormals)
{
    outNormals.resize(width * height * 4);
    
    // Edge bias adjusts the Sobel kernel weighting:
    //   negative = emphasize concave (darks become deeper)
    //   positive = emphasize convex (brights become higher)
    // We scale the center row/column coefficients asymmetrically.
    const float biasFactor = 1.0f + edgeBias;  // [0, 2] when edgeBias in [-1, 1]
    
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const int ix = static_cast<int>(x);
            const int iy = static_cast<int>(y);
            
            // Sobel Gx (horizontal gradient): [[-1,0,1], [-2,0,2], [-1,0,1]]
            // Apply bias by scaling the center column (x±0) differently
            const float gx =
                - sampleHeight(heightMap, width, height, ix - 1, iy - 1)
                + sampleHeight(heightMap, width, height, ix + 1, iy - 1)
                - sampleHeight(heightMap, width, height, ix - 1, iy    ) * 2.0f * biasFactor
                + sampleHeight(heightMap, width, height, ix + 1, iy    ) * 2.0f * biasFactor
                - sampleHeight(heightMap, width, height, ix - 1, iy + 1)
                + sampleHeight(heightMap, width, height, ix + 1, iy + 1);
            
            // Sobel Gy (vertical gradient): [[-1,-2,-1], [0,0,0], [1,2,1]]
            // Apply bias by scaling the center row (y±0) differently
            const float gy =
                - sampleHeight(heightMap, width, height, ix - 1, iy - 1)
                - sampleHeight(heightMap, width, height, ix,     iy - 1) * 2.0f * biasFactor
                - sampleHeight(heightMap, width, height, ix + 1, iy - 1)
                + sampleHeight(heightMap, width, height, ix - 1, iy + 1)
                + sampleHeight(heightMap, width, height, ix,     iy + 1) * 2.0f * biasFactor
                + sampleHeight(heightMap, width, height, ix + 1, iy + 1);
            
            // Construct tangent-space normal: (-gx, -gy, 1.0) scaled by heightScale.
            // The negation ensures that bright→high and dark→low (standard convention).
            const float nx = -gx * heightScale;
            const float ny = -gy * heightScale;
            const float nz = 1.0f;
            
            // Normalize the normal vector
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            const float invLen = (len > 1e-6f) ? (1.0f / len) : 0.0f;
            
            const float normX = nx * invLen;
            const float normY = ny * invLen;
            const float normZ = nz * invLen;
            
            // Encode to RGBA8: normal components [-1,1] → [0,255]
            const u32 outIdx = (y * width + x) * 4;
            outNormals[outIdx + 0] = static_cast<u8>(std::clamp((normX * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            outNormals[outIdx + 1] = static_cast<u8>(std::clamp((normY * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            outNormals[outIdx + 2] = static_cast<u8>(std::clamp((normZ * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            outNormals[outIdx + 3] = 255;  // Alpha unused
        }
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

NormalMapResult generateNormalMap(const u8*              albedoRGBA,
                                   u32                    width,
                                   u32                    height,
                                   const NormalMapParams& params)
{
    DAEDALUS_ASSERT(albedoRGBA != nullptr, "Albedo pixels cannot be null");
    DAEDALUS_ASSERT(width > 0 && height > 0, "Texture dimensions must be > 0");
    
    NormalMapResult result;
    result.width  = width;
    result.height = height;
    
    // Step 1: Convert albedo to luminance height map
    std::vector<float> heightMap = albedoToHeightMap(albedoRGBA, width, height,
                                                      params.invert);
    
    // Step 2: Apply detail level filtering (frequency low-pass)
    u32 w = width;
    u32 h = height;
    applyDetailLevelFilter(heightMap, w, h, params.detailLevel);
    
    // Step 3: Apply blur if requested
    applyBoxBlur(heightMap, width, height, params.blurRadius);
    
    // Step 4: Compute normals via Sobel filter
    computeNormals(heightMap, width, height, params.heightScale, params.edgeBias,
                   result.data);
    
    return result;
}

} // namespace daedalus::render
