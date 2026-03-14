// volumetric_fog_scatter_rt.metal
// Pass 3c (RT) — Froxel light scatter with ray-traced shadow occlusion.
//
// One compute thread per froxel (160 × 90 × 64 = ~921k threads).
// For each froxel the kernel reconstructs its world-space centre, then
// evaluates the Henyey-Greenstein phase function and distance attenuation
// for every light source in the scene.  Shadow rays are traced through the
// TLAS to determine occlusion — ALL lights (sun, point, spot) are tested,
// producing fully correct volumetric shafts through any geometry opening.
//
// Inputs / outputs:
//   texture(0) : froxelScatter  RGBA16Float 3D (write) — (in-scatter.rgb, extinction.a)
//   buffer(0)  : FrameConstants
//   buffer(1)  : VolumetricFogConstants
//   buffer(2)  : uint lightCount
//   buffer(3)  : PointLightGPU[]
//   buffer(4)  : uint spotCount
//   buffer(5)  : SpotLightGPU[]
//   buffer(6)  : instance_acceleration_structure (TLAS)

#include "common.h"
#include <metal_raytracing>

using namespace metal::raytracing;

// ─── World-space froxel centre reconstruction ────────────────────────────────
// Identical to the deferred fog_scatter kernel.

static float3 froxel_to_world(uint3                            froxelId,
                               constant FrameConstants&         frame,
                               constant VolumetricFogConstants& fog)
{
    float2 uv = (float2(froxelId.xy) + 0.5f) / float2(FROXEL_W, FROXEL_H);

    float w         = (float(froxelId.z) + 0.5f) / float(FROXEL_D);
    float fogNear   = fog.ambientFog.w;
    float fogFar    = fog.fogFar;
    float viewDepth = fogNear * pow(fogFar / fogNear, w);

    float ndcZ = frame.proj[2][2] + frame.proj[3][2] / viewDepth;

    return reconstruct_world_pos(ndcZ, uv, frame.invViewProj);
}

// ─── Shadow ray helper ───────────────────────────────────────────────────────
// Traces a shadow ray from `origin` toward `direction` up to `maxDist`.
// Returns 1.0 if the ray reaches the light (unoccluded), 0.0 if blocked.

static float trace_shadow(float3                                origin,
                           float3                                direction,
                           float                                 maxDist,
                           instance_acceleration_structure        tlas)
{
    ray shadowRay;
    shadowRay.origin       = origin;
    shadowRay.direction    = direction;
    shadowRay.min_distance = 0.001f;
    shadowRay.max_distance = maxDist;

    intersector<instancing> isect;
    isect.accept_any_intersection(true);  // early-out on first hit
    auto hit = isect.intersect(shadowRay, tlas);

    return (hit.type == intersection_type::none) ? 1.0f : 0.0f;
}

// ─── Kernel ──────────────────────────────────────────────────────────────────

kernel void fog_scatter_rt(
    texture3d<float, access::write>      froxelScatter  [[texture(0)]],
    constant FrameConstants&             frame          [[buffer(0)]],
    constant VolumetricFogConstants&     fog            [[buffer(1)]],
    constant uint&                       lightCount     [[buffer(2)]],
    constant PointLightGPU*              lights         [[buffer(3)]],
    constant uint&                       spotCount      [[buffer(4)]],
    constant SpotLightGPU*               spotLights     [[buffer(5)]],
    instance_acceleration_structure      tlas           [[buffer(6)]],
    uint3                                gid            [[thread_position_in_grid]])
{
    if (gid.x >= FROXEL_W || gid.y >= FROXEL_H || gid.z >= FROXEL_D) return;

    float3 worldPos = froxel_to_world(gid, frame, fog);

    // Direction from this froxel toward the camera (used for phase function).
    float3 viewDir = normalize(frame.cameraPos.xyz - worldPos);

    // Uniform fog: extinction coefficient = scattering + absorption.
    float  extinction = fog.density;

    // ── Ambient in-scatter ───────────────────────────────────────────────────
    float3 inScatter = fog.ambientFog.xyz * fog.scattering;

    // ── Sun (directional light) ──────────────────────────────────────────────
    // Trace a shadow ray toward the sun.  Long max distance since the sun is
    // infinitely far — any geometry hit means occlusion.
    {
        float3 sunDir    = normalize(frame.sunDirection.xyz);
        float3 sunColor  = frame.sunColor.xyz * frame.sunColor.w;
        float  cosTheta  = dot(viewDir, sunDir);
        float  phase     = hg_phase(cosTheta, fog.anisotropy);
        float  sunVis    = trace_shadow(worldPos, sunDir, 1000.0f, tlas);

        inScatter += sunColor * fog.scattering * phase * sunVis;
    }

    // ── Point lights ─────────────────────────────────────────────────────────
    for (uint i = 0; i < lightCount; ++i)
    {
        float3 lPos  = lights[i].positionRadius.xyz;
        float  lRad  = lights[i].positionRadius.w;
        float3 lCol  = lights[i].colorIntensity.xyz * lights[i].colorIntensity.w;

        float3 toLight = lPos - worldPos;
        float  dist    = length(toLight);
        if (dist >= lRad) continue;

        float3 L        = toLight / dist;
        float  cosTheta = dot(viewDir, L);
        float  phase    = hg_phase(cosTheta, fog.anisotropy);
        float  atten    = 1.0f - (dist / lRad);
        atten *= atten;

        float  vis = trace_shadow(worldPos, L, dist - 0.002f, tlas);
        inScatter += lCol * fog.scattering * phase * atten * vis;
    }

    // ── Spot lights ──────────────────────────────────────────────────────────
    for (uint si = 0; si < spotCount; ++si)
    {
        const SpotLightGPU spot = spotLights[si];
        if (spot.colorIntensity.w <= 0.0f) continue;

        float3 toLight = spot.positionRange.xyz - worldPos;
        float  dist    = length(toLight);
        if (dist >= spot.positionRange.w) continue;

        float3 L        = toLight / dist;
        float  cosTheta = dot(viewDir, L);
        float  phase    = hg_phase(cosTheta, fog.anisotropy);
        float  atten    = 1.0f - (dist / spot.positionRange.w);
        atten *= atten;

        float3 spotDir   = normalize(spot.directionOuterCos.xyz);
        float  coneAtten = smoothstep(spot.directionOuterCos.w,
                                      spot.innerCosAndPad.x,
                                      dot(-L, spotDir));

        float3 lCol = spot.colorIntensity.xyz * spot.colorIntensity.w;
        float  vis  = trace_shadow(worldPos, L, dist - 0.002f, tlas);
        inScatter += lCol * fog.scattering * phase * atten * coneAtten * vis;
    }

    // Write (in-scatter.rgb, extinction.a) to the froxel grid.
    froxelScatter.write(float4(inScatter, extinction), gid);
}
