// volumetric_fog_scatter.metal
// Pass 3c — Froxel light scatter.
//
// One compute thread per froxel (160 × 90 × 64 = ~921k threads).
// For each froxel the kernel reconstructs its world-space centre, then
// evaluates the Henyey-Greenstein phase function and distance attenuation
// for every light source in the scene.  The accumulated in-scatter radiance
// plus the uniform extinction coefficient are written to the output 3D texture.
//
// Inputs / outputs:
//   texture(0) : froxelScatter  RGBA16Float 3D (write) — (in-scatter.rgb, extinction.a)
//   buffer(0)  : FrameConstants
//   buffer(1)  : VolumetricFogConstants
//   buffer(2)  : uint lightCount
//   buffer(3)  : PointLightGPU[]
//   buffer(4)  : uint spotCount
//   buffer(5)  : SpotLightGPU[]

#include "common.h"

// ─── World-space froxel centre reconstruction ────────────────────────────────
// Maps integer froxel coordinates → a world-space point at the froxel's centre.
// Exponential depth mapping distributes slices logarithmically so near-field
// froxels are denser, matching the precision of the depth buffer.

static float3 froxel_to_world(uint3              froxelId,
                               constant FrameConstants&         frame,
                               constant VolumetricFogConstants& fog)
{
    float2 uv = (float2(froxelId.xy) + 0.5f) / float2(FROXEL_W, FROXEL_H);

    // Normalised depth W ∈ (0, 1) → linear view depth via exponential mapping.
    float w         = (float(froxelId.z) + 0.5f) / float(FROXEL_D);
    float fogNear   = fog.ambientFog.w;
    float fogFar    = fog.fogFar;
    float viewDepth = fogNear * pow(fogFar / fogNear, w);

    // Convert linear view depth → NDC Z using the perspective projection inverse.
    // NDC Z = proj[2][2] + proj[3][2] / viewDepth  (perspectiveLH_ZO)
    float ndcZ = frame.proj[2][2] + frame.proj[3][2] / viewDepth;

    return reconstruct_world_pos(ndcZ, uv, frame.invViewProj);
}

// ─── Kernel ──────────────────────────────────────────────────────────────────

kernel void fog_scatter(
    texture3d<float, access::write>   froxelScatter  [[texture(0)]],
    constant FrameConstants&          frame          [[buffer(0)]],
    constant VolumetricFogConstants&  fog            [[buffer(1)]],
    constant uint&                    lightCount     [[buffer(2)]],
    constant PointLightGPU*           lights         [[buffer(3)]],
    constant uint&                    spotCount      [[buffer(4)]],
    constant SpotLightGPU*            spotLights     [[buffer(5)]],
    uint3                             gid            [[thread_position_in_grid]])
{
    if (gid.x >= FROXEL_W || gid.y >= FROXEL_H || gid.z >= FROXEL_D) return;

    float3 worldPos = froxel_to_world(gid, frame, fog);

    // Direction from this froxel toward the camera (used for phase function).
    float3 viewDir = normalize(frame.cameraPos.xyz - worldPos);

    // Uniform fog: extinction coefficient = scattering + absorption.
    // We model fog as purely scattering (no absorption) per the spec, so
    // extinction == density.  The scattering albedo scales in-scatter.
    float  extinction = fog.density;

    // ── Ambient in-scatter ───────────────────────────────────────────────────
    // Constant ambient radiance, attenuated by the scattering albedo.
    float3 inScatter = fog.ambientFog.xyz * fog.scattering;

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
        atten *= atten;  // quadratic falloff, consistent with deferred lighting

        inScatter += lCol * fog.scattering * phase * atten;
    }

    // ── Spot lights ───────────────────────────────────────────────────────────────────────
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
        inScatter += lCol * fog.scattering * phase * atten * coneAtten;
    }

    // Write (in-scatter.rgb, extinction.a) to the froxel grid.
    froxelScatter.write(float4(inScatter, extinction), gid);
}
