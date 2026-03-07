// dof_composite.metal
// Depth of Field — Pass 15.2: near/far blur composite.
//
// Blends the blurred near and far layers back over the sharp scene colour
// using the CoC as the blend weight for each field.
//
//   farBlend  = saturate(max(0, coc)  / bokehRadius)
//   nearBlend = saturate(max(0, -coc) / bokehRadius)
//   result    = mix(mix(sceneColour, farBlur, farBlend), nearBlur, nearBlend)
//
// Near-field blur is applied last and dominates because foreground
// defocus should overlay background layers.
//
// Bindings
// --------
//   texture(0)  sceneColor  RGBA16F  sharp source HDR frame (sample)
//   texture(1)  dofNearTex  RGBA16F  near-field blur result (read)
//   texture(2)  dofFarTex   RGBA16F  far-field blur result (read)
//   texture(3)  cocTex      R32F     signed CoC (read)
//   texture(4)  dofOut      RGBA16F  composited output (write)
//   buffer(0)   FrameConstants
//   buffer(1)   DoFConstants
//   sampler(0)  linSamp     linear clamp

#include "common.h"

kernel void dof_composite(
    texture2d<float, access::sample>  sceneColor [[texture(0)]],
    texture2d<float, access::read>    dofNearTex [[texture(1)]],
    texture2d<float, access::read>    dofFarTex  [[texture(2)]],
    texture2d<float, access::read>    cocTex     [[texture(3)]],
    texture2d<float, access::write>   dofOut     [[texture(4)]],
    constant FrameConstants&          frame      [[buffer(0)]],
    constant DoFConstants&            dof        [[buffer(1)]],
    sampler                           linSamp    [[sampler(0)]],
    uint2                             tid        [[thread_position_in_grid]])
{
    const uint2 dims = uint2(frame.screenSize.xy);
    if (tid.x >= dims.x || tid.y >= dims.y) return;

    const float2 uv = (float2(tid) + 0.5f) * frame.screenSize.zw;

    const float  coc      = cocTex.read(tid).r;
    const float4 scene    = sceneColor.sample(linSamp, uv);
    const float4 nearBlur = dofNearTex.read(tid);
    const float4 farBlur  = dofFarTex.read(tid);

    // Far blend: ramps from 0 (in focus) to 1 (full far bokeh).
    const float farBlend  = saturate(max(0.0f,  coc) / max(dof.bokehRadius, 1e-4f));

    // Near blend: ramps from 0 (in focus) to 1 (full near bokeh).
    const float nearBlend = saturate(max(0.0f, -coc) / max(dof.bokehRadius, 1e-4f));

    // Blend: apply far first, then near overtakes (foreground is in front).
    float4 result = mix(scene,   farBlur,  farBlend);
    result        = mix(result,  nearBlur, nearBlend);

    dofOut.write(result, tid);
}
