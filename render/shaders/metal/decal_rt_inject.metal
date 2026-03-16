// decal_rt_inject.metal
// Compute pass: inject decal albedo into the RT albedo buffer (m_rtAlbedo).
//
// Runs PER DECAL, once per screen pixel, AFTER path trace + SVGF denoiser,
// BEFORE rt_remodulate.  RTRemodulate then multiplies the patched albedo by
// the denoised irradiance, so the decal receives full path-traced lighting.
//
// For each pixel this kernel:
//   1. Samples gDepthCopyTex to reconstruct the world-space surface position.
//   2. Transforms the position into decal local space and tests the unit-cube OBB.
//   3. Projects to UV, samples the decal albedo texture.
//   4. Alpha-composites the decal colour into rtAlbedo (read_write RGBA8Unorm).
//
// Resource bindings:
//   buffer(0)                         = FrameConstants       (constant)
//   buffer(1)                         = DecalConstants       (constant)
//   texture(0)  R32Float   read       = gDepthCopyTex        (depth from path tracer)
//   texture(1)  RGBA8Unorm read_write = rtAlbedoTex          (first-hit surface albedo)
//   texture(2)  RGBA8Unorm sample     = decalAlbedoTex       (decal colour + alpha mask)
//   sampler(0)                        = linear clamp sampler

#include "common.h"

kernel void decal_albedo_inject_main(
    uint2                                     gid          [[thread_position_in_grid]],
    constant FrameConstants&                  frame        [[buffer(0)]],
    constant DecalConstants&                  decal        [[buffer(1)]],
    texture2d<float, access::read>            depthTex     [[texture(0)]],
    texture2d<float, access::read_write>      rtAlbedo     [[texture(1)]],
    texture2d<float>                          decalAlbedo  [[texture(2)]],
    sampler                                   samp         [[sampler(0)]])
{
    const uint2 dims = uint2(frame.screenSize.xy);
    if (gid.x >= dims.x || gid.y >= dims.y) { return; }

    // ── Sample depth ──────────────────────────────────────────────────────────
    float depth = depthTex.read(gid).r;

    // RT path stores linear camera distance.  Sky pixels are written at the far
    // plane distance (see path_trace.metal), not at depth==1.0.
    const float farLinearDepth = frame.proj[3][2] / (1.0f - frame.proj[2][2]);
    if (depth >= farLinearDepth * 0.999f) { return; }

    // ── Reconstruct world-space surface position ──────────────────────────────
    // RT path writes *linear camera distance* into gDepthCopy (not NDC depth).
    // Reconstruct by building the camera ray from invViewProj and advancing by
    // that linear distance.
    float2 screenUV = (float2(gid) + 0.5f) / float2(dims);
    float2 ndc      = uv_to_ndc(screenUV);
    float4 farClip  = frame.invViewProj * float4(ndc, 1.0f, 1.0f);
    float3 farPos   = farClip.xyz / farClip.w;
    float3 camPos   = frame.cameraPos.xyz;
    float3 rayDir   = normalize(farPos - camPos);
    float3 worldPos = camPos + rayDir * depth;

    // ── OBB test: transform to decal local space ──────────────────────────────
    float4 localPos4 = decal.invModel * float4(worldPos, 1.0f);
    float3 local     = localPos4.xyz / localPos4.w;

    // The decal unit cube spans [-0.5, 0.5] in each local axis.
    if (any(abs(local) > float3(0.5f))) { return; }

    // ── Compute decal UV ──────────────────────────────────────────────────────
    // Project onto the XZ plane of the decal OBB:
    //   U = local.x + 0.5  (left  → right)
    //   V = 0.5 - local.z  (front → back, flipped to match Metal UV convention)
    float2 uv = float2(local.x + 0.5f, 0.5f - local.z);

    // ── Sample decal albedo ───────────────────────────────────────────────────
    float4 decalSample = decalAlbedo.sample(samp, uv);
    float  alpha       = decalSample.a * decal.opacity;

    if (alpha < 0.004f) { return; }  // early-out for near-transparent pixels

    // ── Blend into RT albedo buffer ───────────────────────────────────────────
    // Standard over-compositing: dst' = src * alpha + dst * (1 - alpha).
    // Preserves the original surface albedo in the dest alpha channel.
    float4 existing = rtAlbedo.read(gid);
    float3 blended  = mix(existing.rgb, decalSample.rgb, alpha);

    rtAlbedo.write(float4(blended, existing.a), gid);
}
