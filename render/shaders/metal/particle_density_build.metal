// particle_density_build.metal
// Three compute kernels that build a 32×32×32 R16Float voxel density volume
// from a particle state buffer each frame.  The path tracer samples these
// volumes to compute Beer–Lambert transmittance along shadow rays.
//
// Pass order (one set per shadow-casting emitter, before PathTrace):
//   1. particle_density_clear_main   — zero the uint[32768] accumulation buffer
//   2. particle_density_build_main   — splat each alive particle into the buffer
//   3. particle_density_resolve_main — normalise atomic buffer → R16Float 3D texture
//
// The build pass reads the previous frame's stateBuffer (1-frame lag, imperceptible
// for smoke/fire effects).  Atomic uint fixed-point accumulation avoids race conditions.

#include "common.h"

// ─── particle_density_clear_main ─────────────────────────────────────────────
// One thread per voxel (dispatch DENSITY_GRID_SIZE³ threads).
// Zeroes the atomic accumulation buffer so Build has a clean slate.

kernel void particle_density_clear_main(
    device atomic_uint*  atomicBuf [[buffer(0)]],
    constant uint&       bufLen    [[buffer(1)]],
    uint tid [[thread_position_in_grid]])
{
    if (tid >= bufLen) return;
    atomic_store_explicit(&atomicBuf[tid], 0u, memory_order_relaxed);
}

// ─── particle_density_build_main ─────────────────────────────────────────────
// One thread per particle slot (dispatch maxParticles threads).
// Maps each alive particle's world position to a voxel and atomically
// accumulates its alpha contribution as a fixed-point uint.

kernel void particle_density_build_main(
    constant ParticleDensityBuildConstants& c   [[buffer(0)]],
    device const ParticleGPU*               particles [[buffer(1)]],
    device atomic_uint*                     atomicBuf [[buffer(2)]],
    uint tid [[thread_position_in_grid]])
{
    if (tid >= c.maxParticles) return;

    const ParticleGPU p = particles[tid];
    if (!(p.flags & 0x1u)) return;  // not alive

    // Map world position to [0, 1)³ within the emitter AABB.
    float3 volSize = float3(c.worldMax) - float3(c.worldMin);
    float3 uvw = (float3(p.pos) - float3(c.worldMin)) / volSize;

    // Discard particles outside the AABB (conservative bounds may exclude none).
    if (any(uvw < 0.0f) || any(uvw >= 1.0f)) return;

    // Map to integer voxel coordinate.
    uint3 coord = uint3(saturate(uvw) * float(c.gridSize - 1u));
    uint  idx   = coord.z * c.gridSize * c.gridSize
                + coord.y * c.gridSize
                + coord.x;

    // Accumulate fixed-point alpha contribution.
    // Multiply alpha by DENSITY_ATOMIC_SCALE for sub-integer precision.
    uint contrib = uint(p.color.a * float(DENSITY_ATOMIC_SCALE));
    atomic_fetch_add_explicit(&atomicBuf[idx], contrib, memory_order_relaxed);
}

// ─── particle_density_resolve_main ───────────────────────────────────────────
// One thread per voxel (dispatch DENSITY_GRID_SIZE × DENSITY_GRID_SIZE × DENSITY_GRID_SIZE).
// Reads the fixed-point accumulated value, normalises to [0, 1], and writes
// to the R16Float density texture read by the path tracer.

kernel void particle_density_resolve_main(
    device const uint*                          atomicBuf  [[buffer(0)]],
    constant ParticleDensityBuildConstants&     c          [[buffer(1)]],
    texture3d<float, access::write>             densityTex [[texture(0)]],
    uint3 gid [[thread_position_in_grid]])
{
    if (any(gid >= uint3(c.gridSize))) return;

    uint  idx = gid.z * c.gridSize * c.gridSize
              + gid.y * c.gridSize
              + gid.x;

    // Normalise: DENSITY_NORMALIZE_COUNT fully-opaque particles in one voxel → density = 1.0.
    float raw     = float(atomicBuf[idx]) / float(DENSITY_ATOMIC_SCALE);
    float density = saturate(raw / float(DENSITY_NORMALIZE_COUNT));

    densityTex.write(float4(density, 0.0f, 0.0f, 0.0f), gid);
}
