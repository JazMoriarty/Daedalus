#pragma once

#include "daedalus/render/rhi/i_pipeline.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/rhi/i_sampler.h"
#include "daedalus/render/rhi/i_acceleration_structure.h"
#include "daedalus/render/rhi/rhi_types.h"
#include "daedalus/core/types.h"

#include <span>

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

    /// Bind an array of textures starting at the given texture index.
    /// Maps to setTextures:withRange: on Metal.
    ///
    /// @param textures  Contiguous texture pointers (nullptr entries are legal).
    /// @param baseIndex First texture slot index.
    virtual void setTextures(std::span<ITexture* const> textures, u32 baseIndex) = 0;

    /// Bind small constant data inline (equivalent to Metal setBytes / Vulkan push constants).
    virtual void setBytes(const void* data, u32 size, u32 index) = 0;

    // ─── Acceleration structures ──────────────────────────────────────────

    /// Bind an acceleration structure (typically a TLAS) for ray intersection
    /// queries in compute shaders.  Maps to setAccelerationStructure:atBufferIndex:
    /// on Metal.
    ///
    /// @param accel  The acceleration structure to bind. nullptr unbinds.
    /// @param index  Buffer index at which the AS is accessible in the shader.
    virtual void setAccelerationStructure(IAccelerationStructure* accel, u32 index) = 0;

    // ─── Resource residency ──────────────────────────────────────────────────

    /// Declare that a buffer is indirectly accessed by this dispatch (e.g.
    /// vertex/index buffers referenced through an acceleration structure).
    /// Maps to [MTLComputeCommandEncoder useResource:usage:] on Metal.
    virtual void useResource(IBuffer*  buffer,  ResourceUsage usage) = 0;

    /// Declare that a texture is indirectly accessed by this dispatch.
    virtual void useResource(ITexture* texture, ResourceUsage usage) = 0;

    /// Declare that an acceleration structure is indirectly accessed by this
    /// dispatch (e.g. BLAS objects referenced through a TLAS).
    virtual void useResource(IAccelerationStructure* accel, ResourceUsage usage) = 0;

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
