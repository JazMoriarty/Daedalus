// normal_map_generator.h
// CPU-side normal map generation from albedo textures.
//
// Generates tangent-space normal maps from albedo/diffuse textures using a
// Sobel-based height-from-luminance algorithm. Provides simple controls for
// artists to quickly create serviceable normal maps without manual authoring.
//
// Algorithm: luminance → height map → Sobel gradients → tangent-space normals
// Output format: RGBA8 with normals encoded as (R,G,B) = (X,Y,Z) * 0.5 + 0.5
// Flat surface (0,0,1) encodes as (128, 128, 255).

#pragma once

#include "daedalus/core/types.h"
#include <vector>

namespace daedalus::render
{

/// Parameters controlling normal map generation from albedo textures.
struct NormalMapParams
{
    /// Overall bump intensity. Higher values = more pronounced relief.
    /// Range: 0.0 (flat) to 2.0 (exaggerated).
    float heightScale = 1.0f;

    /// Frequency filtering level via mip chain offset.
    /// 0 = full detail (use base mip), 10 = smooth broad features only.
    /// Acts as a low-pass filter to ignore fine noise.
    u32 detailLevel = 0;

    /// Pre-blur smoothing radius in pixels (box filter).
    /// 0 = no blur, 8 = heavy smoothing. Reduces noise artifacts.
    u32 blurRadius = 1;

    /// Emphasize concave vs convex features by biasing Sobel kernel weights.
    /// Negative = emphasize dark→low (grout, cracks, indents).
    /// Positive = emphasize bright→high (raised bricks, bolts).
    /// Range: -1.0 to +1.0.
    float edgeBias = 0.0f;

    /// Flip the luminance→height assumption (bright=low, dark=high).
    /// Useful when albedo contains baked ambient occlusion (dark corners).
    bool invert = false;
};

/// Result of normal map generation containing RGBA8 pixel data.
struct NormalMapResult
{
    u32             width;   ///< Generated normal map width (matches input)
    u32             height;  ///< Generated normal map height (matches input)
    std::vector<u8> data;    ///< RGBA8 pixel data (tangent-space normals)
};

/// Generate a tangent-space normal map from an albedo texture.
///
/// @param albedoRGBA  Source albedo texture in RGBA8 format (4 bytes/pixel)
/// @param width       Source texture width (must be > 0)
/// @param height      Source texture height (must be > 0)
/// @param params      Generation parameters controlling the algorithm
/// @return            Normal map result with RGBA8 data, or empty on error
///
/// The algorithm converts albedo to luminance (perceived brightness), treats
/// luminance as a height field, applies optional blur and frequency filtering,
/// computes gradients via Sobel filter, and encodes the resulting tangent-space
/// normals as RGBA8 with RGB = (normal * 0.5 + 0.5) * 255.
///
/// A flat surface with normal (0, 0, 1) encodes as RGB (128, 128, 255).
/// The alpha channel is unused (set to 255).
[[nodiscard]] NormalMapResult generateNormalMap(const u8*              albedoRGBA,
                                                 u32                    width,
                                                 u32                    height,
                                                 const NormalMapParams& params);

} // namespace daedalus::render
