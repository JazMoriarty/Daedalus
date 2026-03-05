// shadow_depth.metal
// Depth-only vertex shader for the sun shadow map pass.
//
// Input:  position [[attribute(0)]] from the room VBO (same stride/layout as G-buffer).
// Output: clip-space position transformed by frame.sunViewProj.
// No fragment stage — hardware writes depth automatically.

#include "common.h"

struct ShadowVIn
{
    float3 position [[attribute(0)]];
};

struct ShadowVOut
{
    float4 position [[position]];
};

vertex ShadowVOut shadow_depth_vert(
    ShadowVIn                in    [[stage_in]],
    constant FrameConstants& frame [[buffer(0)]],
    constant ModelConstants& model [[buffer(1)]])
{
    ShadowVOut out;
    float4 worldPos = model.model * float4(in.position, 1.0);
    out.position    = frame.sunViewProj * worldPos;
    return out;
}
