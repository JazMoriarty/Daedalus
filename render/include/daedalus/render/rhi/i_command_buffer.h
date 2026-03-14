#pragma once

#include "daedalus/render/rhi/rhi_types.h"
#include "daedalus/render/rhi/i_render_pass_encoder.h"
#include "daedalus/render/rhi/i_compute_pass_encoder.h"
#include "daedalus/render/rhi/i_fence.h"

#include <memory>
#include <string_view>

namespace daedalus::rhi
{

class ISwapchain; // forward: avoid circular include

// ─── ICommandBuffer ───────────────────────────────────────────────────────────
// Records GPU commands (render passes, compute, resource transitions, blits)
// for submission to a queue.
//
// Lifecycle per frame:
//   1. Obtain from ICommandQueue::createCommandBuffer().
//   2. Record commands via begin/end render passes etc.
//   3. Call present() if displaying to a swapchain.
//   4. Call commit() to enqueue for execution.

class ICommandBuffer
{
public:
    virtual ~ICommandBuffer() = default;

    ICommandBuffer(const ICommandBuffer&)            = delete;
    ICommandBuffer& operator=(const ICommandBuffer&) = delete;

    // ─── Render pass ──────────────────────────────────────────────────────────

    /// Open a render pass described by `desc`.  Returns a borrowed pointer to
    /// the encoder; the encoder is owned by this command buffer.
    /// Must call encoder->end() before calling beginRenderPass again or commit.
    [[nodiscard]] virtual IRenderPassEncoder*
    beginRenderPass(const RenderPassDescriptor& desc) = 0;

    // ─── Compute pass ───────────────────────────────────────────────────────────

    /// Open a compute pass.  Returns a borrowed pointer to the encoder;
    /// the encoder is owned by this command buffer.
    /// Must call encoder->end() before opening another pass or calling commit.
    [[nodiscard]] virtual IComputePassEncoder*
    beginComputePass(std::string_view debugLabel = {}) = 0;

    // ─── Resource transitions (blit / copy) ────────────────────────────────────────────

    /// Copy a region of bytes from one buffer to another.
    virtual void copyBuffer(IBuffer* src, usize srcOffset,
                            IBuffer* dst, usize dstOffset,
                            usize    size) = 0;

    /// Copy entire texture contents from src to dst (same format and dimensions).
    virtual void copyTexture(ITexture* src, ITexture* dst) = 0;

    // ─── Presentation ─────────────────────────────────────────────────────────

    /// Schedule presentation of the current drawable from `swapchain`.
    /// Must be called after the last render pass that writes to the drawable.
    virtual void present(ISwapchain& swapchain) = 0;

    // ─── Submission ───────────────────────────────────────────────────────────

    /// Enqueue this command buffer for execution on the GPU.
    /// No further recording is allowed after commit().
    virtual void commit() = 0;

    /// Associate `fence` with this command buffer so that `fence->waitUntilCompleted()`
    /// blocks until this command buffer has finished executing on the GPU.
    /// Must be called before commit().
    virtual void signalOnCompletion(IFence* fence) = 0;

    // ─── Debugging ────────────────────────────────────────────────────────────

    virtual void pushDebugGroup(std::string_view label) = 0;
    virtual void popDebugGroup()                        = 0;

protected:
    ICommandBuffer() = default;
};

} // namespace daedalus::rhi
