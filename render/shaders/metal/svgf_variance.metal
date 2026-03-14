// svgf_variance.metal
// SVGF pass 2 — spatial variance estimation.
// 5×5 bilateral filter guided by depth and normal that estimates per-pixel
// luminance variance from the moment data produced by the temporal pass.

#include "common.h"

kernel void svgf_variance_main(
    constant FrameConstants& frame       [[buffer(0)]],
    constant SVGFConstants&  svgf        [[buffer(1)]],
    texture2d<float, access::read>  inMoments [[texture(0)]],  // luminance moments
    texture2d<float, access::read>  inNormal  [[texture(1)]],  // current normal (encoded)
    texture2d<float, access::read>  inDepth   [[texture(2)]],  // current linear depth
    texture2d<float, access::write> outVar    [[texture(3)]],  // per-pixel variance estimate
    texture2d<float, access::read>  inAlbedo  [[texture(4)]],  // primary-hit albedo (for edge-stopping)
    uint2 gid [[thread_position_in_grid]])
{
    const uint2 screenSize = uint2(frame.screenSize.xy);
    if (gid.x >= screenSize.x || gid.y >= screenSize.y) return;

    float  centerDepth     = inDepth.read(gid).r;
    float3 centerNormal    = decode_normal_from_tex(inNormal.read(gid).xy);
    float  centerAlbedoLum = luminance(inAlbedo.read(gid).rgb);

    float sumMean  = 0.0f;
    float sumMean2 = 0.0f;
    float weightSum = 0.0f;

    // 5×5 bilateral kernel.
    const int R = 2;
    for (int dy = -R; dy <= R; ++dy)
    {
        for (int dx = -R; dx <= R; ++dx)
        {
            int2 p = int2(gid) + int2(dx, dy);
            if (p.x < 0 || p.y < 0 || p.x >= int(screenSize.x) || p.y >= int(screenSize.y))
                continue;

            uint2 up = uint2(p);

            // Depth weight.
            float d = inDepth.read(up).r;
            float dw = exp(-abs(centerDepth - d) / max(svgf.phiDepth * centerDepth, 1e-6f));

            // Normal weight.
            float3 n = decode_normal_from_tex(inNormal.read(up).xy);
            float nw = pow(max(dot(centerNormal, n), 0.0f), svgf.phiNormal);

            // Albedo weight — prevents moments from bleeding across texture boundaries.
            float neighborAlbedoLum = luminance(inAlbedo.read(up).rgb);
            float aw = exp(-abs(centerAlbedoLum - neighborAlbedoLum)
                          / max(svgf.phiAlbedo, 1e-6f));

            float w = dw * nw * aw;
            float2 moments = inMoments.read(up).xy;
            sumMean  += moments.x * w;
            sumMean2 += moments.y * w;
            weightSum += w;
        }
    }

    if (weightSum > 0.0f)
    {
        sumMean  /= weightSum;
        sumMean2 /= weightSum;
    }

    // Variance = E[X²] - E[X]².
    float variance = max(sumMean2 - sumMean * sumMean, 0.0f);
    outVar.write(float4(variance, 0.0f, 0.0f, 0.0f), gid);
}
