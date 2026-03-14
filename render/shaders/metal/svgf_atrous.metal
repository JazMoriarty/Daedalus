// svgf_atrous.metal
// SVGF pass 3 — à-trous wavelet spatial filter.
// Run multiple iterations at increasing step widths (1, 2, 4, …) to achieve
// large-radius edge-preserving filtering.  Edge-stopping guided by depth,
// normal, and luminance variance.

#include "common.h"

// 5×5 à-trous kernel weights (B3 spline).
constant float kWeights[3] = { 1.0f, 2.0f / 3.0f, 1.0f / 6.0f };

kernel void svgf_atrous_main(
    constant FrameConstants& frame    [[buffer(0)]],
    constant SVGFConstants&  svgf     [[buffer(1)]],
    texture2d<float, access::read>  inColor   [[texture(0)]],  // input colour
    texture2d<float, access::read>  inNormal  [[texture(1)]],  // normal (encoded)
    texture2d<float, access::read>  inDepth   [[texture(2)]],  // linear depth
    texture2d<float, access::read>  inVar     [[texture(3)]],  // variance estimate
    texture2d<float, access::write> outColor  [[texture(4)]],  // filtered colour
    texture2d<float, access::read>  inAlbedo  [[texture(5)]],  // primary-hit albedo (for edge-stopping)
    uint2 gid [[thread_position_in_grid]])
{
    const uint2 screenSize = uint2(frame.screenSize.xy);
    if (gid.x >= screenSize.x || gid.y >= screenSize.y) return;

    const int step = int(svgf.stepWidth);

    float4 centerColor     = inColor.read(gid);
    float  centerLum       = luminance(centerColor.rgb);
    float  centerDepth     = inDepth.read(gid).r;
    float3 centerNormal    = decode_normal_from_tex(inNormal.read(gid).xy);
    float  variance        = inVar.read(gid).r;
    float  centerAlbedoLum = luminance(inAlbedo.read(gid).rgb);

    // Luminance edge-stopping sigma driven by local variance.
    float phiL = svgf.phiColor * sqrt(max(variance, 1e-6f));

    float3 sumColor  = centerColor.rgb;
    float  weightSum = 1.0f;

    // 5×5 à-trous kernel with step-width spacing.
    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            if (dx == 0 && dy == 0) continue;

            int2 p = int2(gid) + int2(dx, dy) * step;
            if (p.x < 0 || p.y < 0 || p.x >= int(screenSize.x) || p.y >= int(screenSize.y))
                continue;

            uint2 up = uint2(p);

            // Spatial kernel weight (B3 spline).
            float kw = kWeights[abs(dx)] * kWeights[abs(dy)];

            // Depth weight.
            float d  = inDepth.read(up).r;
            float dw = exp(-abs(centerDepth - d) / max(svgf.phiDepth * centerDepth, 1e-6f));

            // Normal weight.
            float3 n = decode_normal_from_tex(inNormal.read(up).xy);
            float nw = pow(max(dot(centerNormal, n), 0.0f), svgf.phiNormal);

            // Luminance weight.
            float4 c  = inColor.read(up);
            float  l  = luminance(c.rgb);
            float lw = exp(-abs(centerLum - l) / max(phiL, 1e-6f));

            // Albedo weight — prevents blurring across texture boundaries.
            // Compares the primary-hit albedo of the center vs neighbor pixel;
            // pixels from different texture regions receive a lower weight.
            float neighborAlbedoLum = luminance(inAlbedo.read(up).rgb);
            float aw = exp(-abs(centerAlbedoLum - neighborAlbedoLum)
                          / max(svgf.phiAlbedo, 1e-6f));

            float w = kw * dw * nw * lw * aw;
            sumColor  += c.rgb * w;
            weightSum += w;
        }
    }

    outColor.write(float4(sumColor / weightSum, 1.0f), gid);
}
