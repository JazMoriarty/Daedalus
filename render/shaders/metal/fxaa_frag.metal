// fxaa_frag.metal
// FXAA — Pass 21: fast approximate anti-aliasing (mobile variant).
//
// Samples 9 taps (centre + 4 cardinal + 4 diagonal), computes per-tap luma,
// detects aliased edges, and blends along the sub-pixel edge direction.
//
// Vertex stage: reuses tonemap_vert (fullscreen triangle, TonemapVertOut).
//
// Bindings
// --------
//   texture(0)  srcTex     BGRA8    LDR input (from OptFx, CG, or Tonemap)
//   sampler(0)  samp       linear clamp
//   buffer(0)   FrameConstants     (screenSize.zw used for texel size)
//
// Output: swapchain (BGRA8Unorm) — this is always the last pass in the chain

#include "common.h"

// Shared vertex output struct from tone_mapping.metal
struct TonemapVertOut
{
    float4 position [[position]];
    float2 uv;
};

// Minimum contrast threshold below which FXAA is skipped entirely.
// Calibrated for typical LDR content: 1/16 is the NVIDIA suggested floor.
constant float FXAA_THRESHOLD     = 0.0625f;
// Maximum relative threshold: pixels are only processed when
//   lumaRange > lumaMax * FXAA_REL_THRESHOLD
// Raises the bar in very bright regions to avoid over-blurring highlights.
constant float FXAA_REL_THRESHOLD = 0.125f;
// Edge-step blend cap: how far we step perpendicular to the detected edge.
// 0.5 = full-pixel step in the gradient direction.
constant float FXAA_EDGE_STEP     = 0.5f;
// Sub-pixel blend quality: fraction of the local luma contrast used to
// push the sample point toward the detected edge mid-line.
constant float FXAA_SUBPIXEL_TRIM = 0.75f;

fragment float4 fxaa_frag(
    TonemapVertOut          in     [[stage_in]],
    texture2d<float>        srcTex [[texture(0)]],
    sampler                 samp   [[sampler(0)]],
    constant FrameConstants& frame [[buffer(0)]])
{
    // texelSize = (1/width, 1/height)
    const float2 t = frame.screenSize.zw;

    // ── 9-tap neighbourhood ───────────────────────────────────────────────────
    const float3 colorC  = srcTex.sample(samp, in.uv).rgb;
    const float3 colorN  = srcTex.sample(samp, in.uv + float2( 0, -1) * t).rgb;
    const float3 colorS  = srcTex.sample(samp, in.uv + float2( 0,  1) * t).rgb;
    const float3 colorW  = srcTex.sample(samp, in.uv + float2(-1,  0) * t).rgb;
    const float3 colorE  = srcTex.sample(samp, in.uv + float2( 1,  0) * t).rgb;
    const float3 colorNW = srcTex.sample(samp, in.uv + float2(-1, -1) * t).rgb;
    const float3 colorNE = srcTex.sample(samp, in.uv + float2( 1, -1) * t).rgb;
    const float3 colorSW = srcTex.sample(samp, in.uv + float2(-1,  1) * t).rgb;
    const float3 colorSE = srcTex.sample(samp, in.uv + float2( 1,  1) * t).rgb;

    // ── Per-tap luma ──────────────────────────────────────────────────────────
    const float lumaC  = luminance(colorC);
    const float lumaN  = luminance(colorN);
    const float lumaS  = luminance(colorS);
    const float lumaW  = luminance(colorW);
    const float lumaE  = luminance(colorE);
    const float lumaNW = luminance(colorNW);
    const float lumaNE = luminance(colorNE);
    const float lumaSW = luminance(colorSW);
    const float lumaSE = luminance(colorSE);

    // ── Contrast test ─────────────────────────────────────────────────────────
    const float lumaMin   = min(lumaC, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    const float lumaMax   = max(lumaC, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    const float lumaRange = lumaMax - lumaMin;

    // Skip pixels with insufficient contrast — return original colour
    if (lumaRange < max(FXAA_THRESHOLD, lumaMax * FXAA_REL_THRESHOLD))
        return float4(colorC, 1.0f);

    // ── Edge orientation ──────────────────────────────────────────────────────
    // Use a Sobel-like gradient that weights cross taps more heavily than diagonals.
    // edgeH: luma difference in the vertical direction (detects horizontal edges)
    // edgeV: luma difference in the horizontal direction (detects vertical edges)
    const float edgeH = abs(lumaN  + lumaS  - 2.0f * lumaC) * 2.0f
                      + abs(lumaNW + lumaSW - 2.0f * lumaW)
                      + abs(lumaNE + lumaSE - 2.0f * lumaE);
    const float edgeV = abs(lumaW  + lumaE  - 2.0f * lumaC) * 2.0f
                      + abs(lumaNW + lumaNE - 2.0f * lumaN)
                      + abs(lumaSW + lumaSE - 2.0f * lumaS);

    const bool isHoriz = edgeH >= edgeV;

    // ── Sub-pixel blend offset ────────────────────────────────────────────────
    // Determine which side of the centre pixel has the stronger gradient and
    // build a blend UV shifted perpendicular to the detected edge.
    const float lumaNeg = isHoriz ? lumaN : lumaW;
    const float lumaPos = isHoriz ? lumaS : lumaE;

    const float gradNeg = abs(lumaNeg - lumaC);
    const float gradPos = abs(lumaPos - lumaC);

    // Step toward the stronger gradient side
    const float pixelOffset = (gradNeg >= gradPos) ? -FXAA_EDGE_STEP : FXAA_EDGE_STEP;
    float2 blendUV = in.uv;
    if (isHoriz) blendUV.y += pixelOffset * t.y;
    else         blendUV.x += pixelOffset * t.x;

    // ── Sub-pixel quality pass ────────────────────────────────────────────────
    // rgbA: the blended single-pixel-offset colour
    const float3 colorA = srcTex.sample(samp, blendUV).rgb;
    const float  lumaA  = luminance(colorA);

    // rgbB: blend A with a wider ±1.5-pixel sample along the edge axis
    // for additional sub-pixel smoothing (mimics FXAA's quality pass)
    float2 edgeDir = isHoriz ? float2(1.5f * t.x, 0.0f) : float2(0.0f, 1.5f * t.y);
    const float3 colorB = (
        colorA
        + srcTex.sample(samp, blendUV + edgeDir).rgb
        + srcTex.sample(samp, blendUV - edgeDir).rgb
    ) / 3.0f;
    const float lumaB = luminance(colorB);

    // ── Sub-pixel luma blend ──────────────────────────────────────────────────
    // Compute a sub-pixel blend factor from the cross-neighbourhood luma average
    // and use it to smoothly push the result toward the edge mid-line.
    const float lumaAvg   = (lumaN + lumaS + lumaW + lumaE
                             + lumaNW + lumaNE + lumaSW + lumaSE) * (1.0f / 8.0f);
    const float subPixel  = saturate(abs(lumaAvg - lumaC) / lumaRange - FXAA_SUBPIXEL_TRIM);
    const float subBlend  = subPixel * subPixel * 0.75f;

    // If rgbB luma falls outside [min,max], reject it and use rgbA instead.
    // This avoids over-blending into clearly different regions.
    const float3 chosen = (lumaB < lumaMin || lumaB > lumaMax) ? colorA : colorB;

    // Final blend: mix original centre with the edge-shifted colour
    return float4(mix(colorC, chosen, subBlend + 0.5f), 1.0f);
}
