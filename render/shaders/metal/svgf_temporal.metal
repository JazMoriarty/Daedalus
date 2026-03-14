// svgf_temporal.metal
// SVGF pass 1 — temporal accumulation with motion-vector reprojection.
// Blends the current noisy path-traced output with the previous frame's
// denoised result.  Tracks luminance moments for variance estimation.

#include "common.h"

kernel void svgf_temporal_main(
    constant FrameConstants& frame       [[buffer(0)]],
    constant SVGFConstants&  svgf        [[buffer(1)]],
    texture2d<float, access::read>  inColor     [[texture(0)]],  // current noisy HDR
    texture2d<float, access::read>  inHistory   [[texture(1)]],  // previous denoised
    texture2d<float, access::read>  inMotion    [[texture(2)]],  // motion vectors (UV delta)
    texture2d<float, access::read>  inDepth     [[texture(3)]],  // current linear depth
    texture2d<float, access::read>  inPrevDepth [[texture(4)]],  // previous linear depth
    texture2d<float, access::read>  inNormal    [[texture(5)]],  // current normal (encoded)
    texture2d<float, access::read>  inPrevNormal[[texture(6)]],  // previous normal (encoded)
    texture2d<float, access::write> outColor    [[texture(7)]],  // accumulated colour
    texture2d<float, access::write> outMoments  [[texture(8)]],  // luminance moments (mean, mean²)
    uint2 gid [[thread_position_in_grid]])
{
    const uint2 screenSize = uint2(frame.screenSize.xy);
    if (gid.x >= screenSize.x || gid.y >= screenSize.y) return;

    float4 color = inColor.read(gid);
    float  lum   = luminance(color.rgb);

    // ─── Reprojection ────────────────────────────────────────────────────────
    float2 motion  = inMotion.read(gid).xy;
    float2 prevUV  = (float2(gid) + 0.5f) / float2(screenSize) - motion;
    int2   prevPx  = int2(prevUV * float2(screenSize));

    bool valid = prevPx.x >= 0 && prevPx.y >= 0
              && prevPx.x < int(screenSize.x) && prevPx.y < int(screenSize.y);

    if (valid)
    {
        uint2 pp = uint2(prevPx);

        // Depth consistency check.
        float curDepth  = inDepth.read(gid).r;
        float prevDepth = inPrevDepth.read(pp).r;
        float depthDiff = abs(curDepth - prevDepth) / max(curDepth, 0.0001f);
        if (depthDiff > 0.1f) valid = false;

        // Normal consistency check.
        float3 curN  = decode_normal(inNormal.read(gid).xy);
        float3 prevN = decode_normal(inPrevNormal.read(pp).xy);
        if (dot(curN, prevN) < 0.8f) valid = false;
    }

    float alpha = svgf.alpha;

    if (valid)
    {
        uint2 pp = uint2(prevPx);
        float4 history = inHistory.read(pp);

        // Exponential moving average.
        float3 accumulated = mix(history.rgb, color.rgb, alpha);
        outColor.write(float4(accumulated, 1.0f), gid);

        // Track luminance moments.
        float histLum = luminance(history.rgb);
        float mean  = mix(histLum, lum, svgf.momentsAlpha);
        float mean2 = mix(histLum * histLum, lum * lum, svgf.momentsAlpha);
        outMoments.write(float4(mean, mean2, 0.0f, 0.0f), gid);
    }
    else
    {
        // Disoccluded pixel — no history, use current noisy sample directly.
        outColor.write(color, gid);
        outMoments.write(float4(lum, lum * lum, 0.0f, 0.0f), gid);
    }
}
