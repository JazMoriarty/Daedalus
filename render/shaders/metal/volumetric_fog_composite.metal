// volumetric_fog_composite.metal
// Pass 6.6 — Fog composite.
//
// One compute thread per screen pixel.  Reads the pixel's NDC depth, converts
// it to a froxel W coordinate [0,1] via the same exponential mapping used by
// fog_integrate, and samples the pre-integrated 3D volume with trilinear
// filtering.  The HDR colour buffer is modified in-place:
//
//   hdr_out = hdr_in × transmittance + accumulated_scatter
//
// hdrTex is bound as access::read_write.  Since each thread reads and writes
// exclusively its own pixel (gid), there are no cross-thread dependencies and
// Metal's memory model guarantees a consistent result.
//
// Inputs / outputs:
//   texture(0) : hdrTex       RGBA16Float 2D (read_write) — in-place HDR colour
//   texture(1) : froxelResult RGBA16Float 3D (sample)    — integrated volume from fog_integrate
//   texture(2) : gDepthCopy   R32Float    2D (read)      — depth copy for NDC→linear conversion
//   buffer(0)  : FrameConstants
//   buffer(1)  : VolumetricFogConstants
//   sampler(0) : linear clamp

#include "common.h"

kernel void fog_composite(
    texture2d<float, access::read_write>  hdrTex        [[texture(0)]],
    texture3d<float, access::sample>      froxelResult  [[texture(1)]],
    texture2d<float, access::read>        gDepthCopy    [[texture(2)]],
    constant FrameConstants&              frame         [[buffer(0)]],
    constant VolumetricFogConstants&      fog           [[buffer(1)]],
    sampler                               linearSamp    [[sampler(0)]],
    uint2                                 gid           [[thread_position_in_grid]])
{
    const uint fw = hdrTex.get_width();
    const uint fh = hdrTex.get_height();
    if (gid.x >= fw || gid.y >= fh) return;

    float2 uv = (float2(gid) + 0.5f) / float2(fw, fh);

    // NDC depth from the pre-pass R32Float copy.
    float ndcDepth = gDepthCopy.read(gid).r;

    // Convert NDC depth to linear view-space depth.
    float fogNear = fog.ambientFog.w;
    float fogFar  = fog.fogFar;

    float linearDepth = ndc_to_linear_depth(ndcDepth, frame.proj);

    // Sky pixels (depth ~= 1, very large linearDepth) clamp to fogFar so they
    // still receive full fog attenuation.
    linearDepth = clamp(linearDepth, fogNear, fogFar);

    // Convert linear depth → froxel W ∈ [0, 1] via exponential inverse.
    float w = log(linearDepth / fogNear) / log(fogFar / fogNear);

    // Sample the integrated froxel volume with trilinear filtering.
    float4 fogSample         = froxelResult.sample(linearSamp, float3(uv.x, uv.y, w));
    float3 accumulated_scatter = fogSample.rgb;
    float  transmittance       = fogSample.a;

    // Apply fog to the existing HDR colour.
    float4 hdrColor = hdrTex.read(gid);
    hdrTex.write(float4(hdrColor.rgb * transmittance + accumulated_scatter,
                        hdrColor.a), gid);
}
