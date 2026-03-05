#pragma once

#include "daedalus/render/rhi/rhi_types.h"

namespace daedalus::rhi
{

// ─── IBuffer ─────────────────────────────────────────────────────────────────
// An opaque GPU buffer handle.  Owned exclusively by the code that created it
// through a unique_ptr<IBuffer>.

class IBuffer
{
public:
    virtual ~IBuffer() = default;

    IBuffer(const IBuffer&)            = delete;
    IBuffer& operator=(const IBuffer&) = delete;

    [[nodiscard]] virtual usize       size()  const noexcept = 0;
    [[nodiscard]] virtual BufferUsage usage() const noexcept = 0;

    /// Map the buffer for CPU write access.  Returns a pointer into the
    /// buffer's backing memory.  Only valid for Staging or Uniform buffers.
    [[nodiscard]] virtual void* map()          = 0;
    virtual void                unmap() noexcept = 0;

protected:
    IBuffer() = default;
};

} // namespace daedalus::rhi
