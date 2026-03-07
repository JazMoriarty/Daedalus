// color_grade_frag.metal
// Colour Grading — Pass 19: 3D LUT colour transform.
//
// Applies a 32×32×32 RGBA8Unorm 3D Look-Up Table to the tonemapped frame.
// The result is blended back against the original by `intensity` so that
// intensity=0 gives a passthrough and intensity=1 gives full LUT grade.
//
// LUT addressing follows the standard neutral-position convention:
//   lutCoord = src.rgb * (31.0/32.0) + (0.5/32.0)
// This centres the sample on the texel rather than the boundary edge,
// so an identity LUT (r[i]=i/31, g[j]=j/31, b[k]=k/31) round-trips perfectly.
//
// Vertex stage: reuses tonemap_vert (fullscreen triangle).
//
// Bindings
// --------
//   texture(0)  tonemapped  BGRA8    tonemapped input (output of Pass 17)
//   texture(1)  lut3d       RGBA8    32×32×32 3D LUT (sample)
//   sampler(0)  lutSamp     linear clamp (for smooth LUT interpolation)
//   buffer(1)   ColorGradingConstants
//
// Output: swapchain (BGRA8Unorm)

#include "common.h"

// Vertex output type shared with tonemap_vert (defined in tone_mapping.metal).
struct TonemapVertOut
{
    float4 position [[position]];
    float2 uv;
};

fragment float4 color_grade_frag(
    TonemapVertOut              in         [[stage_in]],
    texture2d<float>            tonemapped [[texture(0)]],
    texture3d<float>            lut3d      [[texture(1)]],
    sampler                     lutSamp    [[sampler(0)]],
    constant ColorGradingConstants& cg     [[buffer(1)]])
{
    const float3 src = tonemapped.sample(lutSamp, in.uv).rgb;

    // Map to LUT space: shift by half-texel so sample centres land exactly
    // on each LUT entry rather than interpolating across boundaries.
    const float3 lutCoord = src * (31.0f / 32.0f) + (0.5f / 32.0f);
    const float3 graded   = lut3d.sample(lutSamp, lutCoord).rgb;

    // Blend between original and graded by intensity.
    const float3 result = mix(src, graded, cg.intensity);

    return float4(result, 1.0f);
}
