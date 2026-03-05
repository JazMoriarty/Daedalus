// ssao.metal
// Screen-space ambient occlusion — two compute kernels:
//
//  ssao_main  — 16-tap hemisphere kernel in world space.
//               texture(0) = gDepth         Depth32Float (read)
//               texture(1) = gNormal        RGBA8Unorm   (read, oct-encoded normal in RG)
//               texture(2) = aoOut          R32Float     (write, raw AO)
//               buffer(0)  = FrameConstants
//
//  ssao_blur  — bilateral 5x5 depth-aware filter.
//               texture(0) = aoIn           R32Float     (read, raw AO from ssao_main)
//               texture(1) = gDepth         Depth32Float (read, for edge-preservation)
//               texture(2) = aoOut          R32Float     (write, smoothed AO)
//               buffer(0)  = FrameConstants

#include "common.h"

// ─── Per-pixel hash → random rotation angle in [0, 2π) ───────────────────────
// Rotates the hemisphere tangent frame per pixel to break fixed-pattern banding
// that would otherwise appear when using deterministic Fibonacci sampling.
inline float ssao_noise(uint2 gid)
{
    uint h = gid.x * 1664525u + gid.y * 1013904223u;
    h ^= (h >> 16);
    h *= 0x45d9f3bu;
    h ^= (h >> 15);
    return float(h & 0xFFFFu) * (2.0f * M_PI_F / 65536.0f);
}

// ─── ssao_main ───────────────────────────────────────────────────────────────

kernel void ssao_main(
    texture2d<float, access::read>  gDepth  [[texture(0)]],
    texture2d<float, access::read>  gNormal [[texture(1)]],
    texture2d<float, access::write> aoOut   [[texture(2)]],
    constant FrameConstants&        frame   [[buffer(0)]],
    uint2                           gid     [[thread_position_in_grid]])
{
    const uint w = gDepth.get_width();
    const uint h = gDepth.get_height();
    if (gid.x >= w || gid.y >= h) return;

    float2 uv    = (float2(gid) + 0.5f) / float2(w, h);
    float  depth = gDepth.read(gid).r;

    if (depth >= 0.9999f)
    {
        aoOut.write(float4(1.0f, 0, 0, 1), gid);
        return;
    }

    float3 worldPos = reconstruct_world_pos(depth, uv, frame.invViewProj);

    // Decode octahedral normal from RT1 (stored as [0,1] in RG channels)
    float2 octN = gNormal.read(gid).rg * 2.0f - 1.0f;
    float3 N    = decode_normal(octN);

    // Tangent frame around N, rotated per-pixel to break banding
    float3 up = (abs(N.y) < 0.999f) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T  = normalize(cross(up, N));
    float3 B  = cross(N, T);

    float noiseA = ssao_noise(gid);
    float cosA = cos(noiseA), sinA = sin(noiseA);
    float3 Tr = T * cosA + B * sinA;   // rotated tangent
    float3 Br = -T * sinA + B * cosA;  // rotated bitangent

    const int   SAMPLES = 16;
    const float RADIUS  = 0.5f;   // world-space hemisphere radius
    const float BIAS    = 0.015f; // prevents self-occlusion on flat surfaces

    float occlusion = 0.0f;

    for (int i = 0; i < SAMPLES; ++i)
    {
        // Fibonacci hemisphere sampling
        float fi   = float(i);
        float phi  = fi * 2.399963f;  // golden angle
        float cosT = 1.0f - (fi + 0.5f) / float(SAMPLES);
        float sinT = sqrt(1.0f - cosT * cosT);
        float r    = RADIUS * (fi / float(SAMPLES - 1) * 0.75f + 0.25f);

        float3 sampleDir = Tr * (cos(phi) * sinT)
                         + Br * (sin(phi) * sinT)
                         + N  * cosT;
        float3 samplePos = worldPos + sampleDir * r;

        float4 sClip = frame.viewProj * float4(samplePos, 1.0f);
        float2 sUV   = ndc_to_uv(sClip.xy / sClip.w);
        if (sUV.x < 0 || sUV.x > 1 || sUV.y < 0 || sUV.y > 1) continue;

        uint2  sGid   = uint2(clamp(sUV * float2(w, h), float2(0), float2(w-1, h-1)));
        float  sDepth = gDepth.read(sGid).r;
        float3 sWorld = reconstruct_world_pos(sDepth, sUV, frame.invViewProj);

        float rangeCheck = smoothstep(0.0f, 1.0f, RADIUS / max(length(worldPos - sWorld), 0.0001f));
        float sViewZ     = (frame.view * float4(sWorld,     1.0f)).z;
        float oViewZ     = (frame.view * float4(samplePos,  1.0f)).z;
        occlusion       += (sViewZ <= oViewZ - BIAS ? 1.0f : 0.0f) * rangeCheck;
    }

    float ao = 1.0f - (occlusion / float(SAMPLES));
    aoOut.write(float4(ao, 0, 0, 1), gid);
}

// ─── ssao_blur ───────────────────────────────────────────────────────────────
// Bilateral 5×5 Gaussian filter: smooths the noisy raw AO while preserving
// depth edges so occlusion does not bleed across surface boundaries.

kernel void ssao_blur(
    texture2d<float, access::read>  aoIn   [[texture(0)]],
    texture2d<float, access::read>  gDepth [[texture(1)]],
    texture2d<float, access::write> aoOut  [[texture(2)]],
    constant FrameConstants&        frame  [[buffer(0)]],
    uint2                           gid    [[thread_position_in_grid]])
{
    const uint w = aoOut.get_width();
    const uint h = aoOut.get_height();
    if (gid.x >= w || gid.y >= h) return;

    float centerDepth = gDepth.read(gid).r;

    if (centerDepth >= 0.9999f)
    {
        aoOut.write(float4(1.0f, 0, 0, 1), gid);
        return;
    }

    // Bilateral 5×5 filter:
    //   depthSigma   — controls edge sharpness; smaller = harder edges
    //   spatialSigma — controls the spatial Gaussian spread (in pixels)
    const float depthSigma   = 0.003f;
    const float spatialSigma = 2.0f;

    float aoSum     = 0.0f;
    float weightSum = 0.0f;

    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            int2 coord = int2(gid) + int2(dx, dy);
            if (coord.x < 0 || coord.y < 0 ||
                uint(coord.x) >= w || uint(coord.y) >= h)
                continue;

            uint2 tc         = uint2(coord);
            float sampleAO   = aoIn.read(tc).r;
            float sampleD    = gDepth.read(tc).r;

            float depthDiff   = sampleD - centerDepth;
            float depthWeight = exp(-(depthDiff * depthDiff) /
                                    (2.0f * depthSigma * depthSigma));

            float dist2        = float(dx * dx + dy * dy);
            float spatialWeight = exp(-dist2 / (2.0f * spatialSigma * spatialSigma));

            float weight  = depthWeight * spatialWeight;
            aoSum        += sampleAO * weight;
            weightSum    += weight;
        }
    }

    float ao = (weightSum > 1e-5f) ? aoSum / weightSum : aoIn.read(gid).r;
    aoOut.write(float4(ao, 0, 0, 1), gid);
}
