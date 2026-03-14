// rt_remodulate.metal
// Post-denoiser albedo remodulation.  Multiplies the denoised irradiance
// (lighting only) by the primary-hit albedo to restore texture colour.
// This runs after the SVGF à-trous passes (or directly after the path
// tracer when denoising is disabled).

#include "common.h"

kernel void rt_remodulate_main(
    constant FrameConstants&         frame       [[buffer(0)]],
    texture2d<float, access::read>   inIrradiance [[texture(0)]],
    texture2d<float, access::read>   inAlbedo     [[texture(1)]],
    texture2d<float, access::write>  outHDR       [[texture(2)]],
    uint2 gid [[thread_position_in_grid]])
{
    const uint2 screenSize = uint2(frame.screenSize.xy);
    if (gid.x >= screenSize.x || gid.y >= screenSize.y) return;

    float3 irradiance = inIrradiance.read(gid).rgb;
    float3 albedo     = inAlbedo.read(gid).rgb;

    outHDR.write(float4(irradiance * albedo, 1.0f), gid);
}
