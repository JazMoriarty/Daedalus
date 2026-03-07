// particle_simulate.metal
// GPU particle simulation: emit, simulate, bitonic sort step, compact.
//
// Resource bindings (shared across all kernels unless noted):
//   buffer(0)  = ParticleEmitterConstants  (constant)
//   buffer(1)  = ParticleGPU[]             stateBuffer  (device read/write)
//   buffer(2)  = uint[]                    deadList     (device atomic read/write)
//   buffer(3)  = uint[]                    aliveListA   (device read/write)
//   buffer(4)  = uint[]                    aliveListB   (device read/write)
//   buffer(5)  = DrawIndirectArgs          indirectArgs (device, compact only)
//
// List layout (deadList, aliveListA, aliveListB):
//   [0] = count (atomic uint)
//   [1..N] = particle indices
//
// aliveListFlip in constants: 0 = read A write B, 1 = read B write A.

#include "common.h"

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// PCG hash for a uint seed — fast, well-distributed.
static inline uint pcg(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

/// Map a uint seed to a float in [0,1).
static inline float randf(thread uint& seed)
{
    seed = pcg(seed);
    return float(seed) * (1.0 / float(0xffffffffu));
}

/// Random float in [lo, hi).
static inline float randRange(thread uint& seed, float lo, float hi)
{
    return lo + randf(seed) * (hi - lo);
}

/// Sample a random direction within a cone around `axis` with half-angle `halfAngle`.
static inline float3 randomConeDir(thread uint& seed, float3 axis, float halfAngle)
{
    float cosA = cos(halfAngle);
    float z    = cosA + randf(seed) * (1.0 - cosA);        // cos(theta) in [cosA, 1]
    float phi  = randf(seed) * 2.0 * M_PI_F;
    float r    = sqrt(max(0.0, 1.0 - z * z));
    float3 dir = float3(r * cos(phi), r * sin(phi), z);

    // Rotate from Z-up to `axis`.
    float3 up   = abs(axis.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tang = normalize(cross(up, axis));
    float3 bita = cross(axis, tang);
    return normalize(tang * dir.x + bita * dir.y + axis * dir.z);
}

// ─── Kernel: particle_emit ────────────────────────────────────────────────────
// One thread per spawned particle this frame.
// Pops a free index from deadList, writes initial state to stateBuffer,
// pushes index onto the write alive list.

kernel void particle_emit(
    constant ParticleEmitterConstants& em       [[buffer(0)]],
    device   ParticleGPU*              state    [[buffer(1)]],
    device   atomic_uint*              deadList [[buffer(2)]],
    device   atomic_uint*              aliveA   [[buffer(3)]],
    device   atomic_uint*              aliveB   [[buffer(4)]],
    uint                               gid      [[thread_position_in_grid]])
{
    if (gid >= em.spawnThisFrame) { return; }

    // Pop a free index from deadList.
    // deadList[0] = count; deadList[1..] = indices.
    uint dead = atomic_fetch_sub_explicit(&deadList[0], 1u, memory_order_relaxed);
    if (dead == 0u) { atomic_fetch_add_explicit(&deadList[0], 1u, memory_order_relaxed); return; }
    uint idx = atomic_load_explicit((device atomic_uint*)(deadList + dead), memory_order_relaxed);

    // Unique seed per particle combining emitter position, frame, and spawn id.
    uint seed = pcg(em.frameIndex * 7919u + gid * 1234567u);

    // Sub-frame emission jitter: offset initial position along velocity direction
    // by a fraction of the frame interval to eliminate banding at low rates.
    float spawnFraction = randf(seed);
    float speed   = randRange(seed, em.speedMin, em.speedMax);
    float3 dir    = randomConeDir(seed, em.emitDir, em.coneHalfAngle);
    float3 vel    = dir * speed;
    float  life   = randRange(seed, em.lifetimeMin, em.lifetimeMax);

    // dt is baked into spawnThisFrame; approximate sub-frame offset from fraction.
    float  subDt  = spawnFraction * (1.0 / max(em.emissionRate, 1.0));
    float3 pos    = em.emitterPos + vel * subDt;

    // Atlas frame: random start frame.
    uint totalFrames = max(1u, uint(em.atlasSize.x) * uint(em.atlasSize.y));
    uint frame = uint(randf(seed) * float(totalFrames));

    ParticleGPU p;
    p.pos      = pos;
    p.life     = life;
    p.vel      = vel;
    p.maxLife  = life;
    p.color    = em.colorStart;
    p.size     = em.sizeStart;
    p.rot      = randf(seed) * 2.0 * M_PI_F;
    p.frameIdx = frame;
    p.flags    = 1u;  // alive
    state[idx] = p;

    // Push idx onto write alive list.
    device atomic_uint* writeList = (em.aliveListFlip == 0u) ? aliveB : aliveA;
    uint writeSlot = atomic_fetch_add_explicit(&writeList[0], 1u, memory_order_relaxed);
    if (writeSlot < em.maxParticles)
        atomic_store_explicit((device atomic_uint*)(writeList + 1u + writeSlot),
                              idx, memory_order_relaxed);
}

// ─── Kernel: particle_simulate ────────────────────────────────────────────────
// One thread per alive particle on the READ list.
// Integrates physics, applies curl noise, decrements lifetime.
// Living particles go onto the WRITE list; dead ones go back to deadList.

kernel void particle_simulate(
    constant ParticleEmitterConstants& em       [[buffer(0)]],
    device   ParticleGPU*              state    [[buffer(1)]],
    device   atomic_uint*              deadList [[buffer(2)]],
    device   atomic_uint*              aliveA   [[buffer(3)]],
    device   atomic_uint*              aliveB   [[buffer(4)]],
    uint                               gid      [[thread_position_in_grid]])
{
    device atomic_uint* readList  = (em.aliveListFlip == 0u) ? aliveA : aliveB;
    device atomic_uint* writeList = (em.aliveListFlip == 0u) ? aliveB : aliveA;

    uint aliveCount = atomic_load_explicit(&readList[0], memory_order_relaxed);
    if (gid >= aliveCount) { return; }

    uint idx = atomic_load_explicit((device atomic_uint*)(readList + 1u + gid),
                                    memory_order_relaxed);
    ParticleGPU p = state[idx];

    // Approximate dt from emission rate; this is a crude estimate but keeps the
    // simulation decoupled from a CPU delta-time upload.  FrameConstants.deltaTime
    // would be cleaner; for now constants don't include it, but the struct has room.
    // Use a fixed 1/60 to keep shaders self-contained for this implementation.
    const float dt = 1.0 / 60.0;

    // Decrement lifetime.
    p.life -= dt;
    if (p.life <= 0.0)
    {
        // Return to dead pool.
        p.flags = 0u;
        state[idx] = p;
        uint dead = atomic_fetch_add_explicit(&deadList[0], 1u, memory_order_relaxed);
        if (dead < em.maxParticles)
            atomic_store_explicit((device atomic_uint*)(deadList + 1u + dead),
                                  idx, memory_order_relaxed);
        return;
    }

    // Curl noise turbulence.
    if (em.turbulenceScale > 0.0)
    {
        float3 noisePos = p.pos * 0.5 + float3(0.0, em.frameIndex * 0.001, 0.0);
        p.vel += curl_noise(noisePos) * em.turbulenceScale * dt;
    }

    // Semi-implicit Euler: apply gravity + drag, then integrate position.
    p.vel += em.gravity * dt;
    p.vel *= max(0.0, 1.0 - em.drag * dt);
    p.pos += p.vel * dt;

    // Interpolate visual properties by normalised age t = 1 - life/maxLife.
    float t       = 1.0 - p.life / max(p.maxLife, 0.0001);
    p.color       = mix(em.colorStart, em.colorEnd, t);
    p.size        = mix(em.sizeStart,  em.sizeEnd,  t);

    // Advance atlas frame.
    if (em.atlasFrameRate > 0.0)
    {
        uint totalFrames = max(1u, uint(em.atlasSize.x) * uint(em.atlasSize.y));
        float age        = (p.maxLife - p.life);
        p.frameIdx       = uint(age * em.atlasFrameRate) % totalFrames;
    }

    state[idx] = p;

    // Push onto write alive list.
    uint writeSlot = atomic_fetch_add_explicit(&writeList[0], 1u, memory_order_relaxed);
    if (writeSlot < em.maxParticles)
        atomic_store_explicit((device atomic_uint*)(writeList + 1u + writeSlot),
                              idx, memory_order_relaxed);
}

// ─── Kernel: particle_sort_step ───────────────────────────────────────────────
// One step of GPU bitonic sort on the alive list.
// Called O(log² N) times by FrameRenderer, with (step, stage) constants.
//
// Additional bindings:
//   buffer(5) = SortConstants (step, stage, camera forward)

struct SortConstants
{
    uint   step;         // current bitonic sort step (outer loop k)
    uint   stage;        // current bitonic sort stage (inner loop j)
    float3 cameraForward;
    float  pad;
};

kernel void particle_sort_step(
    constant ParticleEmitterConstants& em       [[buffer(0)]],
    device   ParticleGPU*              state    [[buffer(1)]],
    device   atomic_uint*              deadList [[buffer(2)]],  // unused
    device   atomic_uint*              aliveA   [[buffer(3)]],
    device   atomic_uint*              aliveB   [[buffer(4)]],
    constant SortConstants&            sort     [[buffer(5)]],
    uint                               gid      [[thread_position_in_grid]])
{
    // After simulate, WRITE list holds sorted candidates.
    device atomic_uint* list = (em.aliveListFlip == 0u) ? aliveB : aliveA;
    uint count = atomic_load_explicit(&list[0], memory_order_relaxed);
    if (gid >= count / 2u) { return; }

    uint j    = sort.stage;
    uint k    = sort.step;
    uint ixj  = gid ^ j;
    if (ixj <= gid) { return; }

    uint ia = atomic_load_explicit((device atomic_uint*)(list + 1u + gid),
                                   memory_order_relaxed);
    uint ib = atomic_load_explicit((device atomic_uint*)(list + 1u + ixj),
                                   memory_order_relaxed);

    float da = dot(state[ia].pos, sort.cameraForward);
    float db = dot(state[ib].pos, sort.cameraForward);

    bool ascending = ((gid & k) == 0u);
    bool swap      = ascending ? (da > db) : (da < db);  // back-to-front: farthest first
    if (swap)
    {
        atomic_store_explicit((device atomic_uint*)(list + 1u + gid),  ib, memory_order_relaxed);
        atomic_store_explicit((device atomic_uint*)(list + 1u + ixj), ia, memory_order_relaxed);
    }
}

// ─── Kernel: particle_compact ────────────────────────────────────────────────
// Single-thread dispatch.  Writes the alive count into DrawIndirectArgs,
// then resets the now-stale READ list count to 0 for next frame.

kernel void particle_compact(
    constant ParticleEmitterConstants& em          [[buffer(0)]],
    device   ParticleGPU*              state       [[buffer(1)]],  // unused
    device   atomic_uint*              deadList    [[buffer(2)]],  // unused
    device   atomic_uint*              aliveA      [[buffer(3)]],
    device   atomic_uint*              aliveB      [[buffer(4)]],
    device   DrawIndirectArgs*         indirectArgs[[buffer(5)]],
    uint                               gid         [[thread_position_in_grid]])
{
    if (gid != 0u) { return; }

    device atomic_uint* readList  = (em.aliveListFlip == 0u) ? aliveA : aliveB;
    device atomic_uint* writeList = (em.aliveListFlip == 0u) ? aliveB : aliveA;

    uint alive = atomic_load_explicit(&writeList[0], memory_order_relaxed);

    // Write GPU-driven instance count — drawIndirect reads this directly.
    indirectArgs->instanceCount = alive;
    // vertexCount/firstVertex/baseInstance are initialised to 6/0/0 at pool create time.

    // Reset read list for next frame.
    atomic_store_explicit(&readList[0], 0u, memory_order_relaxed);
}
