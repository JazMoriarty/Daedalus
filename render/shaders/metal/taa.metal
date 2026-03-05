// taa.metal
// Temporal anti-aliasing (fullscreen render pass).
//
// Inputs (fragment stage):
//   texture(0) = currentHDR    RGBA16Float (current jittered frame)
//   texture(1) = historyHDR    RGBA16Float (TAA-resolved previous frame)
//   texture(2) = motionVectors RG16Float   (UV-space velocity)
//   sampler(0) = linear clamp
//   buffer(0)  = FrameConstants
//
// Output:
//   color(0)   = RGBA16Float (resolved, de-jittered)

#include "common.h"

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Variance-based
inline float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 sample)
{
    float3 p_clip = 0.5 * (aabb_max + aabb_min);
    float3 e_clip = 0.5 * (aabb_max - aabb_min) + 1e-5;
    float3 v_clip = sample - p_clip;
    float3 v_unit = v_clip / e_clip;
    float3 a_unit = abs(v_unit);
    float  ma_unit = max(max(a_unit.x, a_unit.y), a_unit.z);
    return (ma_unit > 1.0) ? (p_clip + v_clip / ma_unit) : sample;
}

// ─── Vertex (fullscreen triangle) ────────────────────────────────────────────

struct TAAVertOut
{
    float4 position [[position]];
    float2 uv;
};

vertex TAAVertOut taa_vert(uint vid [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -3.0),
        float2(-1.0,  1.0),
        float2( 3.0,  1.0)
    };
    TAAVertOut out;
    out.position = float4(positions[vid], 0.5, 1.0);
    out.uv       = ndc_to_uv(positions[vid]);
    return out;
}

// ─── Fragment ─────────────────────────────────────────────────────────────────

fragment float4 taa_frag(
    TAAVertOut               in      [[stage_in]],
    texture2d<float>         current [[texture(0)]],
    texture2d<float>         history [[texture(1)]],
    texture2d<float>         motion  [[texture(2)]],
    sampler                  samp    [[sampler(0)]],
    constant FrameConstants& frame   [[buffer(0)]])
{
    float2 uv = in.uv;

    // De-jittered UV for current frame sample
    float2 uvCurr = uv - frame.jitter * frame.screenSize.zw;
    uvCurr = clamp(uvCurr, float2(0), float2(1));

    float3 curr = current.sample(samp, uvCurr).rgb;

    // Reproject using motion vectors
    float2 vel    = motion.sample(samp, uv).rg;
    float2 uvPrev = uv - vel;
    uvPrev        = clamp(uvPrev, float2(0), float2(1));

    float3 hist = history.sample(samp, uvPrev).rgb;

    // ─── 3×3 neighbourhood variance clamp ────────────────────────────────────
    float3 m1 = float3(0), m2 = float3(0);
    float3 nMin = float3(1e9), nMax = float3(-1e9);
    float2 texel = frame.screenSize.zw;

    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        float2 sUV  = uvCurr + float2(x, y) * texel;
        float3 sCol = current.sample(samp, sUV).rgb;
        m1    += sCol;
        m2    += sCol * sCol;
        nMin   = min(nMin, sCol);
        nMax   = max(nMax, sCol);
    }
    m1 /= 9.0; m2 /= 9.0;
    float3 sigma = sqrt(max(m2 - m1 * m1, float3(0)));
    float3 cMin  = m1 - 1.25 * sigma;
    float3 cMax  = m1 + 1.25 * sigma;

    hist = clip_aabb(cMin, cMax, hist);

    // ─── Luminance-weighted blend ─────────────────────────────────────────────
    float blendAlpha = 0.1;  // 10% current, 90% history

    // Increase blend on first frame (history is invalid)
    if (frame.frameIndex < 1.5) blendAlpha = 1.0;

    // Increase blend when velocity is large
    float speed = length(vel);
    blendAlpha  = mix(blendAlpha, 0.5, saturate(speed * 100.0));

    float3 result = mix(hist, curr, blendAlpha);
    return float4(result, 1.0);
}
