// ssao.metal
// Screen-space ambient occlusion (compute pass).
//
// Inputs:
//   texture(0) = gDepth         Depth32Float (read)
//   texture(1) = gNormalMetal   RGBA16Float  (read, xyz = world normal)
//   texture(2) = aoOut          R16Float     (write)
//   buffer(0)  = FrameConstants

#include "common.h"

// ─── SSAO kernel ─────────────────────────────────────────────────────────────
// 16-tap hemisphere kernel in view space, then re-projected to sample depth.

kernel void ssao_main(
    texture2d<float, access::read>  gDepth       [[texture(0)]],
    texture2d<float, access::read>  gNormalMetal [[texture(1)]],
    texture2d<float, access::write> aoOut        [[texture(2)]],
    constant FrameConstants&        frame        [[buffer(0)]],
    uint2                           gid          [[thread_position_in_grid]])
{
    const uint w = gDepth.get_width();
    const uint h = gDepth.get_height();
    if (gid.x >= w || gid.y >= h) return;

    float2 uv    = (float2(gid) + 0.5f) / float2(w, h);
    float  depth = gDepth.read(gid).r;

    // Sky / background: no occlusion
    if (depth >= 0.9999f)
    {
        aoOut.write(float4(1.0f, 0, 0, 1), gid);
        return;
    }

    float3 worldPos = reconstruct_world_pos(depth, uv, frame.invViewProj);
    float3 N        = normalize(gNormalMetal.read(gid).xyz);

    // Build a tangent frame around the normal
    float3 up     = (abs(N.y) < 0.999f) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T      = normalize(cross(up, N));
    float3 B      = cross(N, T);

    const int   SAMPLES = 16;
    const float RADIUS  = 0.35f;
    const float BIAS    = 0.015f;

    float occlusion = 0.0f;

    for (int i = 0; i < SAMPLES; ++i)
    {
        // Fibonacci hemisphere sampling
        float fi     = float(i);
        float phi    = fi * 2.399963f;        // golden angle
        float cosT   = 1.0f - (fi + 0.5f) / float(SAMPLES);
        float sinT   = sqrt(1.0f - cosT * cosT);
        float r      = RADIUS * (fi / float(SAMPLES - 1) * 0.75f + 0.25f);

        float3 sampleDir = T * (cos(phi) * sinT)
                         + B * (sin(phi) * sinT)
                         + N * cosT;
        float3 samplePos = worldPos + sampleDir * r;

        // Project sample into screen space
        float4 sClip = frame.viewProj * float4(samplePos, 1.0f);
        float2 sUV   = ndc_to_uv(sClip.xy / sClip.w);

        if (sUV.x < 0 || sUV.x > 1 || sUV.y < 0 || sUV.y > 1) continue;

        uint2  sGid    = uint2(clamp(sUV * float2(w, h), float2(0), float2(w-1, h-1)));
        float  sDepth  = gDepth.read(sGid).r;
        float3 sWorld  = reconstruct_world_pos(sDepth, sUV, frame.invViewProj);

        // Range-bounded occlusion
        float rangeCheck = smoothstep(0.0f, 1.0f, RADIUS / max(length(worldPos - sWorld), 0.0001f));
        float sViewZ     = (frame.view * float4(sWorld, 1.0f)).z;
        float oViewZ     = (frame.view * float4(samplePos, 1.0f)).z;
        occlusion       += (sViewZ <= oViewZ - BIAS ? 1.0f : 0.0f) * rangeCheck;
    }

    float ao = 1.0f - (occlusion / float(SAMPLES));
    aoOut.write(float4(ao, 0, 0, 1), gid);
}
