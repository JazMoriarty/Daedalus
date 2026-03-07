// particle_pool.h
// Owns the GPU buffers for one particle emitter's simulation state.
//
// A ParticlePool is allocated once (via createParticlePool) and lives for
// the duration of the owning entity.  The FrameRenderer holds a non-owning
// pointer to each pool submitted in SceneView::particleEmitters.
//
// Buffer layout (all BufferUsage::Storage):
//
//   stateBuffer    — ParticleGPU[maxParticles]               (64 * N bytes)
//   deadList       — uint[maxParticles] + uint header         (4 * (N+1) bytes)
//                    [0] = count of available dead indices
//                    [1..N] = particle indices available for reuse
//   aliveListA     — uint[maxParticles] + uint header
//   aliveListB     — uint[maxParticles] + uint header
//   indirectArgs   — DrawIndirectArgsGPU (16 bytes)
//                    vertexCount always = 6; instanceCount written by compact kernel
//
// aliveListFlip in ParticleEmitterConstantsGPU controls which list is the
// read source and which is the write destination for each simulate frame.
// FrameRenderer toggles this each frame per emitter.
//
// Non-owning usage: ParticleEmitterComponent holds a raw ParticlePool* and
// assumes the application object that called createParticlePool() manages lifetime.

#pragma once

#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/render/scene_data.h"
#include "daedalus/core/types.h"
#include "daedalus/core/assert.h"

#include <memory>

namespace daedalus::render
{

// ─── ParticlePool ─────────────────────────────────────────────────────────────

struct ParticlePool
{
    u32 maxParticles = 0;

    /// Per-particle simulation state.  Written by emit/simulate, read by render.
    std::unique_ptr<rhi::IBuffer> stateBuffer;

    /// Atomic stack of free (dead) particle indices.
    /// u32[0] = available count, u32[1..N] = indices.
    std::unique_ptr<rhi::IBuffer> deadList;

    /// Ping-pong alive-index lists.  simulate reads one and writes the other.
    std::unique_ptr<rhi::IBuffer> aliveListA;
    std::unique_ptr<rhi::IBuffer> aliveListB;

    /// DrawIndirectArgsGPU written by the compact kernel each frame.
    /// Passed directly to drawIndirect() — no CPU readback ever needed.
    std::unique_ptr<rhi::IBuffer> indirectArgs;

    /// Alive list ping-pong state: 0 = simulate reads A writes B,
    /// 1 = simulate reads B writes A.  Toggled by FrameRenderer each frame.
    u32 aliveListFlip = 0;

    ParticlePool()                             = default;
    ~ParticlePool()                            = default;
    ParticlePool(const ParticlePool&)          = delete;
    ParticlePool& operator=(const ParticlePool&) = delete;
};

// ─── createParticlePool ───────────────────────────────────────────────────────
// Allocate all GPU buffers for one emitter and CPU-initialise the dead list
// so all N indices are available from the first frame.
//
// @param device       RHI device to allocate buffers from.
// @param maxParticles Maximum number of simultaneously alive particles.
//
// @returns A fully initialised ParticlePool; asserts on allocation failure.

[[nodiscard]] inline std::unique_ptr<ParticlePool>
createParticlePool(rhi::IRenderDevice& device, u32 maxParticles)
{
    DAEDALUS_ASSERT(maxParticles > 0, "createParticlePool: maxParticles must be > 0");

    auto pool          = std::make_unique<ParticlePool>();
    pool->maxParticles = maxParticles;

    // ── State buffer ─────────────────────────────────────────────────────────
    {
        rhi::BufferDescriptor d;
        d.size      = static_cast<usize>(maxParticles) * sizeof(ParticleGPU);
        d.usage     = rhi::BufferUsage::Storage;
        d.debugName = "ParticleState";
        pool->stateBuffer = device.createBuffer(d);
        DAEDALUS_ASSERT(pool->stateBuffer, "createParticlePool: state buffer alloc failed");
    }

    // ── Dead list ────────────────────────────────────────────────────────────
    // Format: [count u32] [index u32] [index u32] ... (count + 1 elements).
    // CPU-initialises count = N and all indices [0..N-1].
    {
        const usize sz = (static_cast<usize>(maxParticles) + 1u) * sizeof(u32);
        std::vector<u32> initData(maxParticles + 1u);
        initData[0] = maxParticles;                    // all particles dead at start
        for (u32 i = 0; i < maxParticles; ++i)
            initData[i + 1u] = i;

        rhi::BufferDescriptor d;
        d.size      = sz;
        d.usage     = rhi::BufferUsage::Storage;
        d.initData  = initData.data();
        d.debugName = "ParticleDeadList";
        pool->deadList = device.createBuffer(d);
        DAEDALUS_ASSERT(pool->deadList, "createParticlePool: dead list alloc failed");
    }

    // ── Alive lists A & B ─────────────────────────────────────────────────────
    // Format: [count u32] [index u32...] — count is zero at start.
    {
        const usize sz = (static_cast<usize>(maxParticles) + 1u) * sizeof(u32);
        std::vector<u32> zeroInit(maxParticles + 1u, 0u);

        rhi::BufferDescriptor d;
        d.size      = sz;
        d.usage     = rhi::BufferUsage::Storage;
        d.initData  = zeroInit.data();

        d.debugName    = "ParticleAliveListA";
        pool->aliveListA = device.createBuffer(d);
        DAEDALUS_ASSERT(pool->aliveListA, "createParticlePool: alive list A alloc failed");

        d.debugName    = "ParticleAliveListB";
        pool->aliveListB = device.createBuffer(d);
        DAEDALUS_ASSERT(pool->aliveListB, "createParticlePool: alive list B alloc failed");
    }

    // ── Indirect draw args ────────────────────────────────────────────────────
    // vertexCount = 6 (fixed for quad); instanceCount written each frame by compact.
    {
        DrawIndirectArgsGPU initArgs;
        initArgs.vertexCount   = 6u;
        initArgs.instanceCount = 0u;
        initArgs.firstVertex   = 0u;
        initArgs.baseInstance  = 0u;

        rhi::BufferDescriptor d;
        d.size      = sizeof(DrawIndirectArgsGPU);
        d.usage     = rhi::BufferUsage::Storage;
        d.initData  = &initArgs;
        d.debugName = "ParticleIndirectArgs";
        pool->indirectArgs = device.createBuffer(d);
        DAEDALUS_ASSERT(pool->indirectArgs, "createParticlePool: indirect args alloc failed");
    }

    return pool;
}

} // namespace daedalus::render
