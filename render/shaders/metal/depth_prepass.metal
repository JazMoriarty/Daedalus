// depth_prepass.metal
// Writes depth only; no fragment function needed (pipeline sets fragmentFunction=nil).

#include "common.h"

struct DepthVertIn
{
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 tangent  [[attribute(3)]];
};

struct DepthVertOut
{
    float4 position [[position]];
};

vertex DepthVertOut depth_prepass_vert(
    DepthVertIn                 in    [[stage_in]],
    constant FrameConstants&  frame [[buffer(0)]],
    constant ModelConstants&  model [[buffer(1)]])
{
    DepthVertOut out;
    float4 worldPos  = model.model * float4(in.position, 1.0);
    // TAA jitter is already baked into frame.viewProj by the CPU-side jitter matrix.
    out.position = frame.viewProj * worldPos;
    return out;
}
