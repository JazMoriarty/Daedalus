// volumetric_fog_integrate.metal
// Pass 3d — Froxel column integration.
//
// One compute thread per (x, y) froxel column; each thread iterates all 64
// depth slices front-to-back and accumulates:
//   • accum_scatter   — light that has been scattered toward the camera
//   • accum_trans     — fraction of scene light that survives the fog
//
// At each slice the kernel uses the analytic integral for a piecewise-constant
// medium, which is numerically stable even for very thin or very thick slices:
//   scatter_contrib = in_scatter × (1 − exp(−σ_t × Δd)) / σ_t
//
// The integrated state (accum_scatter, accum_trans) is written at each depth
// index so the composite pass can sample exactly the right depth per pixel.
//
// Inputs / outputs:
//   texture(0) : froxelScatter  RGBA16Float 3D (read)  — scatter + extinction from fog_scatter
//   texture(1) : froxelResult   RGBA16Float 3D (write) — (accumulated scatter.rgb, transmittance.a)
//   buffer(0)  : VolumetricFogConstants

#include "common.h"

kernel void fog_integrate(
    texture3d<float, access::read>    froxelScatter  [[texture(0)]],
    texture3d<float, access::write>   froxelResult   [[texture(1)]],
    constant VolumetricFogConstants&  fog            [[buffer(0)]],
    uint2                             gid            [[thread_position_in_grid]])
{
    if (gid.x >= FROXEL_W || gid.y >= FROXEL_H) return;

    float fogNear = fog.ambientFog.w;
    float fogFar  = fog.fogFar;

    float3 accum_scatter = float3(0.0f);
    float  accum_trans   = 1.0f;

    for (uint z = 0; z < FROXEL_D; ++z)
    {
        float4 slice     = froxelScatter.read(uint3(gid, z));
        float3 in_scatter = slice.rgb;
        // Guard against zero extinction to prevent division-by-zero.
        float  extinction = max(slice.a, 1e-6f);

        // World-space depth span of this slice via exponential mapping.
        float zFront = float(z)     / float(FROXEL_D);
        float zBack  = float(z + 1) / float(FROXEL_D);
        float dFront = fogNear * pow(fogFar / fogNear, zFront);
        float dBack  = fogNear * pow(fogFar / fogNear, zBack);
        float thickness = dBack - dFront;

        // Beer-Lambert transmittance through this slice.
        float sliceTrans = exp(-extinction * thickness);

        // Analytic scatter integral over [0, thickness]:
        //   ∫₀^Δd  exp(−σ_t·t) · σ_s · L_in  dt = in_scatter · (1 − exp(−σ_t·Δd)) / σ_t
        float3 scatter_contrib = in_scatter * (1.0f - sliceTrans) / extinction;

        // Accumulate: weight by transmittance of all preceding slices.
        accum_scatter += accum_trans * scatter_contrib;
        accum_trans   *= sliceTrans;

        // Store integrated state at this depth so the composite pass can sample
        // the correct depth slice per pixel.
        froxelResult.write(float4(accum_scatter, accum_trans), uint3(gid, z));
    }
}
