#pragma once

#include "daedalus/render/rhi/rhi_types.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/rhi/i_sampler.h"
#include "daedalus/render/rhi/i_pipeline.h"
#include "daedalus/render/rhi/i_bind_group.h"

namespace daedalus::rhi
{

// ─── IRenderPassEncoder ───────────────────────────────────────────────────────
// Records GPU draw commands for a single render pass.
// Obtained from ICommandBuffer::beginRenderPass().
// Must call end() when recording is complete.

class IRenderPassEncoder
{
public:
    virtual ~IRenderPassEncoder() = default;

    IRenderPassEncoder(const IRenderPassEncoder&)            = delete;
    IRenderPassEncoder& operator=(const IRenderPassEncoder&) = delete;

    // ─── State binding ────────────────────────────────────────────────────────

    virtual void setRenderPipeline(IPipeline* pipeline) = 0;

    virtual void setViewport(const Viewport& viewport)       = 0;
    virtual void setScissor(const ScissorRect& rect)         = 0;

    virtual void setVertexBuffer  (IBuffer*  buffer,  u32 offset, u32 bindIndex) = 0;
    virtual void setVertexTexture  (ITexture* texture,             u32 bindIndex) = 0;
    virtual void setIndexBuffer    (IBuffer*  buffer,  u32 offset, bool use32Bit) = 0;
    virtual void setBindGroup      (u32 index, IBindGroup* group)                 = 0;

    // Fragment-stage resources
    virtual void setFragmentBuffer (IBuffer*  buffer,  u32 offset, u32 bindIndex) = 0;
    virtual void setFragmentTexture(ITexture* texture,             u32 bindIndex) = 0;
    virtual void setFragmentSampler(ISampler* sampler,             u32 bindIndex) = 0;
    virtual void setVertexSampler  (ISampler* sampler,             u32 bindIndex) = 0;

    // ─── Push constants ───────────────────────────────────────────────────────────

    virtual void setVertexBytes  (const void* data, u32 size, u32 bindIndex) = 0;
    virtual void setFragmentBytes(const void* data, u32 size, u32 bindIndex) = 0;

    // ─── Draw ─────────────────────────────────────────────────────────────────

    virtual void draw(u32 vertexCount,
                      u32 instanceCount = 1,
                      u32 firstVertex   = 0,
                      u32 firstInstance = 0) = 0;

    virtual void drawIndexed(u32 indexCount,
                              u32 instanceCount = 1,
                              u32 firstIndex    = 0,
                              i32 vertexOffset  = 0,
                              u32 firstInstance = 0) = 0;

    // ─── End ──────────────────────────────────────────────────────────────────

    /// Finish recording render commands for this pass.
    virtual void end() = 0;

protected:
    IRenderPassEncoder() = default;
};

} // namespace daedalus::rhi
