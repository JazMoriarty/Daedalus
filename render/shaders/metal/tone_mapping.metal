// tone_mapping.metal
// Final post-process: bloom composite + ACES tone mapping + gamma correction.
//
// Inputs (fragment):
//   texture(0) = taaOut   RGBA16Float (TAA-resolved HDR frame)
//   texture(1) = bloomTex RGBA16Float (bloom result)
//   sampler(0) = linear clamp
//   buffer(0)  = FrameConstants
//
// Output: swapchain (BGRA8Unorm)

#include "common.h"

struct TonemapVertOut
{
    float4 position [[position]];
    float2 uv;
};

vertex TonemapVertOut tonemap_vert(uint vid [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -3.0),
        float2(-1.0,  1.0),
        float2( 3.0,  1.0)
    };
    TonemapVertOut out;
    out.position = float4(positions[vid], 0.5, 1.0);
    out.uv       = ndc_to_uv(positions[vid]);
    return out;
}

constant float BLOOM_STRENGTH = 0.18;
constant float EXPOSURE       = 1.0;

fragment float4 tonemap_frag(
    TonemapVertOut           in    [[stage_in]],
    texture2d<float>         hdr   [[texture(0)]],
    texture2d<float>         bloom [[texture(1)]],
    sampler                  samp  [[sampler(0)]],
    constant FrameConstants& frame [[buffer(0)]])
{
    float3 colour    = hdr.sample(samp, in.uv).rgb;
    float3 bloomCol  = bloom.sample(samp, in.uv).rgb;

    // Bloom composite (additive)
    colour += bloomCol * BLOOM_STRENGTH;

    // Exposure
    colour *= EXPOSURE;

    // ACES tone mapping
    colour = aces_film(colour);

    // Gamma correction (linear → sRGB approximation)
    colour = pow(max(colour, float3(0)), float3(1.0 / 2.2));

    return float4(colour, 1.0);
}
