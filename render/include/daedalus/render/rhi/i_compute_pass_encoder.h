#pragma once

#include "daedalus/render/rhi/i_pipeline.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/rhi/i_sampler.h"
#include "daedalus/core/types.h"

namespace daedalus::rhi
{

// ─── IComputePassEncoder ──────────────────────────────────────────────────────
// Records GPU compute dispatch commands.
// Obtained from ICommandBuffer::beginComputePass().
// Must call end() when recording is complete.

class IComputePassEncoder
{
public:
    virtual ~IComputePassEncoder() = default;

    IComputePassEncoder(const IComputePassEncoder&)            = delete;
    IComputePassEncoder& operator=(const IComputePassEncoder&) = delete;

    // ─── Pipeline ─────────────────────────────────────────────────────────────

    virtual void setComputePipeline(IPipeline* pipeline) = 0;

    // ─── Resource binding ─────────────────────────────────────────────────────

    virtual void setBuffer (IBuffer*  buffer,  u32 offset, u32 index) = 0;
    virtual void setTexture(ITexture* texture,             u32 index) = 0;
    virtual void setSampler(ISampler* sampler,             u32 index) = 0;

    /// Bind small constant data inline (equivalent to Metal setBytes / Vulkan push constants).
    virtual void setBytes(const void* data, u32 size, u32 index) = 0;

    // ─── Dispatch ─────────────────────────────────────────────────────────────

    /// Dispatch threadCountX × threadCountY × threadCountZ threads total.
    /// The backend selects an appropriate threadgroup size automatically.
    virtual void dispatch(u32 threadCountX, u32 threadCountY, u32 threadCountZ = 1) = 0;

    // ─── Debugging ────────────────────────────────────────────────────────────

    virtual void pushDebugGroup(std::string_view label) = 0;
    virtual void popDebugGroup()                        = 0;

    // ─── End ──────────────────────────────────────────────────────────────────

    virtual void end() = 0;

protected:
    IComputePassEncoder() = default;
};

} // namespace daedalus::rhi
