// ssr_main.metal
// Screen-Space Reflections (SSR) — spec Pass 14.
//
// Single compute pass placed between TAA and Bloom.  Reads three G-buffer
// inputs (normals, depth, scene colour) and writes a composited RGBA result
// that replaces the TAA output for all downstream passes (Bloom, Tonemap).
//
// Algorithm
// ---------
// For each screen pixel:
//   1. Skip background and high-roughness surfaces (write-through).
//   2. Reconstruct world-space surface position and decode world-space normal.
//   3. Build a reflection ray in world space from the camera-to-surface
//      view direction and the G-buffer normal.
//   4. March the ray in world space (fixed step = maxDistance / maxSteps),
//      projecting each sample to screen space and comparing linearised depth
//      against the depth buffer.
//   5. On intersection, refine with a binary search over the last march
//      interval for sub-step precision.
//   6. Sample the scene colour at the resolved hit UV.
//   7. Blend with the original pixel colour using a weight derived from:
//        Fresnel (NdotV Schlick, F0 = 0.04 dielectric)
//        × roughness fade (linear 1 → 0 over [0, roughnessCutoff])
//        × screen-edge fade (smooth ramp over fadeStart UV distance)
//        × distance fade (1 − hitT / maxDistance)
//
// Bindings
// --------
//   texture(0)  gNormal       RGBA8    oct-encoded world normal (rg) + roughness (b) + metalness (a)
//   texture(1)  gDepthCopy    R32F     raw NDC depth [0, 1]  (copy of hardware depth buffer)
//   texture(2)  sceneColor    RGBA16F  TAA-resolved HDR scene colour (source for reflection samples)
//   texture(3)  ssrOut        RGBA16F  composited output (write)
//   buffer(0)   FrameConstants
//   buffer(1)   SSRConstants
//   sampler(0)  linSamp       linear clamp

#include "common.h"

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// Project a world-space position into screen UV.
/// Returns false (and leaves uv/ndcZ unwritten) if the position is behind
/// the camera or outside the [-1,1] NDC box.
static bool project_to_uv(float3 worldPos,
                           float4x4 viewProj,
                           thread float2& uv,
                           thread float& ndcZ)
{
    float4 clip = viewProj * float4(worldPos, 1.0f);
    if (clip.w <= 0.0f) return false;

    float3 ndc = clip.xyz / clip.w;
    if (any(ndc.xy < -1.0f) || any(ndc.xy > 1.0f)) return false;
    if (ndc.z < 0.0f || ndc.z > 1.0f)               return false;

    uv   = ndc_to_uv(ndc.xy);
    ndcZ = ndc.z;
    return true;
}

// ─── Kernel ──────────────────────────────────────────────────────────────────

kernel void ssr_main(
    texture2d<float, access::read>    gNormal    [[texture(0)]],
    texture2d<float, access::read>    gDepthCopy [[texture(1)]],
    texture2d<float, access::sample>  sceneColor [[texture(2)]],
    texture2d<float, access::write>   ssrOut     [[texture(3)]],
    constant FrameConstants&          frame      [[buffer(0)]],
    constant SSRConstants&            ssr        [[buffer(1)]],
    sampler                           linSamp    [[sampler(0)]],
    uint2                             tid        [[thread_position_in_grid]])
{
    const uint2 dims = uint2(frame.screenSize.xy);
    if (tid.x >= dims.x || tid.y >= dims.y) return;

    const float2 uv = (float2(tid) + 0.5f) / float2(dims);

    // ── Read + early-out ────────────────────────────────────────────────────

    const float ndcDepth = gDepthCopy.read(tid).r;

    // Background pixel (sky): write-through, no reflection.
    if (ndcDepth >= 1.0f)
    {
        ssrOut.write(sceneColor.sample(linSamp, uv), tid);
        return;
    }

    const float4 gN       = gNormal.read(tid);
    const float  roughness = gN.b;

    // Surfaces above the roughness cutoff: write-through.
    if (roughness >= ssr.roughnessCutoff)
    {
        ssrOut.write(sceneColor.sample(linSamp, uv), tid);
        return;
    }

    // ── Surface reconstruction ───────────────────────────────────────────────

    // World position from NDC depth and screen UV.
    const float3 worldPos = reconstruct_world_pos(ndcDepth, uv, frame.invViewProj);

    // Decode octahedral normal (stored in [0,1], remap to [-1,1]).
    const float3 worldN = decode_normal(gN.rg * 2.0f - 1.0f);

    // View direction: from camera toward surface (into the scene).
    const float3 viewDir = normalize(worldPos - frame.cameraPos.xyz);

    // Guard: skip back-facing normals (viewing from behind a surface).
    if (dot(viewDir, worldN) >= 0.0f)
    {
        ssrOut.write(sceneColor.sample(linSamp, uv), tid);
        return;
    }

    // Reflection direction in world space.
    const float3 reflectDir = reflect(viewDir, worldN);

    // ── Ray march ────────────────────────────────────────────────────────────

    const float stepSize = ssr.maxDistance / float(ssr.maxSteps);

    bool   hit    = false;
    float  hitT   = 0.0f;
    float2 hitUV  = uv;
    float  hitNdcZ = ndcDepth;

    float prevT = 0.0f;

    for (uint i = 0u; i < ssr.maxSteps; ++i)
    {
        const float t = (float(i) + 1.0f) * stepSize;

        float3 rayPos = worldPos + reflectDir * t;

        float2 sampleUV;
        float  rayNdcZ;
        if (!project_to_uv(rayPos, frame.viewProj, sampleUV, rayNdcZ))
            break;  // Ray exited screen or went behind camera

        // Sample scene depth at the projected screen position.
        const uint2 sampleTid = clamp(uint2(sampleUV * float2(dims)),
                                      uint2(0u), dims - 1u);
        const float sceneNdcZ = gDepthCopy.read(sampleTid).r;

        // Convert NDC depths to linear view-space depths for comparison.
        const float rayViewZ   = ndc_to_linear_depth(rayNdcZ,   frame.proj);
        const float sceneViewZ = ndc_to_linear_depth(sceneNdcZ, frame.proj);

        // Intersection: ray is at or behind the scene surface.
        if (rayViewZ >= sceneViewZ && (rayViewZ - sceneViewZ) < ssr.thickness)
        {
            hit     = true;
            hitT    = t;
            hitUV   = sampleUV;
            hitNdcZ = rayNdcZ;
            break;
        }

        prevT = t;
    }

    // ── Binary search refinement ─────────────────────────────────────────────

    if (hit)
    {
        float tMin = prevT;
        float tMax = hitT;

        for (int r = 0; r < 8; ++r)
        {
            const float tMid  = (tMin + tMax) * 0.5f;
            float3 refinePos  = worldPos + reflectDir * tMid;

            float2 refUV;
            float  refNdcZ;
            if (!project_to_uv(refinePos, frame.viewProj, refUV, refNdcZ))
                break;

            const uint2 refTid     = clamp(uint2(refUV * float2(dims)),
                                           uint2(0u), dims - 1u);
            const float sceneNdcZ  = gDepthCopy.read(refTid).r;
            const float rayViewZ   = ndc_to_linear_depth(refNdcZ,   frame.proj);
            const float sceneViewZ = ndc_to_linear_depth(sceneNdcZ, frame.proj);

            if (rayViewZ >= sceneViewZ)
            {
                tMax  = tMid;
                hitUV = refUV;
                hitT  = tMid;
            }
            else
            {
                tMin = tMid;
            }
        }
    }

    // ── Blend weight ─────────────────────────────────────────────────────────

    const float4 baseColor = sceneColor.sample(linSamp, uv);

    if (!hit)
    {
        ssrOut.write(baseColor, tid);
        return;
    }

    // Schlick Fresnel: F0 = 0.04 for dielectric surfaces.
    const float NdotV       = saturate(-dot(viewDir, worldN));
    const float fresnel     = 0.04f + 0.96f * pow(1.0f - NdotV, 5.0f);

    // Roughness fade: full reflection at roughness=0, zero at roughnessCutoff.
    const float roughFade   = saturate(1.0f - roughness / ssr.roughnessCutoff);

    // Screen-edge fade: smooth ramp to zero at the edge of the screen.
    const float2 edgeDist   = min(hitUV, 1.0f - hitUV);
    const float  edgeFade   = saturate(min(edgeDist.x, edgeDist.y) / ssr.fadeStart);

    // Distance fade: reflections fade toward zero as the ray approaches maxDistance.
    const float  distFade   = saturate(1.0f - hitT / ssr.maxDistance);

    const float  blendWeight = fresnel * roughFade * edgeFade * distFade;

    // ── Composite ────────────────────────────────────────────────────────────

    const float4 reflectColor = sceneColor.sample(linSamp, hitUV);
    const float4 finalColor   = mix(baseColor, reflectColor, blendWeight);

    ssrOut.write(finalColor, tid);
}
