// bloom.metal
// Bloom: bright-pass extraction + two-pass separable Gaussian blur.
//
// Three entry points:
//   bloom_extract_frag  — extracts bright regions from taaOut → bloomA
//   bloom_blur_h_frag   — horizontal Gaussian blur: bloomA → bloomB
//   bloom_blur_v_frag   — vertical Gaussian blur:   bloomB → bloomA
//
// All use the same fullscreen vertex shader (bloom_vert).
//
// Inputs always: texture(0) = source, sampler(0) = linear clamp
// buffer(0) = FrameConstants (for screenSize)

#include "common.h"

// ─── Fullscreen vertex shader ─────────────────────────────────────────────────

struct BloomVertOut
{
    float4 position [[position]];
    float2 uv;
};

vertex BloomVertOut bloom_vert(uint vid [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -3.0),
        float2(-1.0,  1.0),
        float2( 3.0,  1.0)
    };
    BloomVertOut out;
    out.position = float4(positions[vid], 0.5, 1.0);
    out.uv       = ndc_to_uv(positions[vid]);
    return out;
}

// ─── Bright pass ──────────────────────────────────────────────────────────────

constant float BLOOM_THRESHOLD = 1.0;   // HDR value above which bloom starts
constant float BLOOM_KNEE      = 0.5;   // Soft knee width

fragment float4 bloom_extract_frag(
    BloomVertOut             in   [[stage_in]],
    texture2d<float>         src  [[texture(0)]],
    sampler                  samp [[sampler(0)]],
    constant FrameConstants& frame [[buffer(0)]])
{
    float3 colour = src.sample(samp, in.uv).rgb;
    float  lum    = luminance(colour);

    // Quadratic soft-knee threshold
    float  knee   = BLOOM_THRESHOLD * BLOOM_KNEE;
    float  rq     = clamp(lum - (BLOOM_THRESHOLD - knee), 0.0, 2.0 * knee);
    rq            = (rq * rq) / (4.0 * knee + 1e-5);
    float  weight = max(lum - BLOOM_THRESHOLD, rq) / max(lum, 1e-5);

    return float4(colour * weight, 1.0);
}

// ─── Gaussian blur (separable 13-tap) ────────────────────────────────────────

// Weights and offsets for a 13-tap Gaussian (σ ≈ 2.0)
constant float kGW[7] = { 0.1752, 0.1585, 0.1200, 0.0794, 0.0459, 0.0232, 0.0103 };

inline float3 blur_dir(texture2d<float> src, sampler samp,
                        float2 uv, float2 dir)
{
    float3 result = src.sample(samp, uv).rgb * kGW[0];
    for (int i = 1; i < 7; ++i)
    {
        float2 offset = float2(i) * dir;
        result += src.sample(samp, uv + offset).rgb * kGW[i];
        result += src.sample(samp, uv - offset).rgb * kGW[i];
    }
    return result;
}

fragment float4 bloom_blur_h_frag(
    BloomVertOut             in   [[stage_in]],
    texture2d<float>         src  [[texture(0)]],
    sampler                  samp [[sampler(0)]],
    constant FrameConstants& frame [[buffer(0)]])
{
    float2 texel = frame.screenSize.zw;
    return float4(blur_dir(src, samp, in.uv, float2(texel.x, 0.0)), 1.0);
}

fragment float4 bloom_blur_v_frag(
    BloomVertOut             in   [[stage_in]],
    texture2d<float>         src  [[texture(0)]],
    sampler                  samp [[sampler(0)]],
    constant FrameConstants& frame [[buffer(0)]])
{
    float2 texel = frame.screenSize.zw;
    return float4(blur_dir(src, samp, in.uv, float2(0.0, texel.y)), 1.0);
}
