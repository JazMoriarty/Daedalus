#pragma once

#include "daedalus/core/memory/allocator.h"
#include "daedalus/core/assert.h"

namespace daedalus
{

// ─── FrameAllocator ───────────────────────────────────────────────────────────
// High-speed linear (bump-pointer) allocator intended for per-frame scratch
// memory.  All allocations are served in O(1); there is no per-object free —
// call reset() at the start of each frame to reclaim the entire buffer.
//
// Not thread-safe.  Use one FrameAllocator per thread, or serialise access
// externally.

class FrameAllocator final : public IAllocator
{
public:
    /// Allocate the backing buffer.  `capacityBytes` must be > 0.
    explicit FrameAllocator(usize capacityBytes);
    ~FrameAllocator() override;

    [[nodiscard]] void* allocate(usize size,
                                 usize alignment = alignof(std::max_align_t)) override;

    /// No-op — individual allocations cannot be freed.
    /// The IAllocator contract requires this signature; calling it is valid but
    /// does nothing.  Memory is reclaimed in bulk via reset().
    void deallocate(void* /*ptr*/, usize /*size*/) noexcept override {}

    /// Reclaim all memory allocated since the last reset.  Does NOT run
    /// destructors — callers must handle object lifetimes explicitly.
    void reset() noexcept;

    // ─── Diagnostics ──────────────────────────────────────────────────────────

    [[nodiscard]] usize capacity()  const noexcept { return m_capacity; }
    [[nodiscard]] usize used()      const noexcept { return m_offset; }
    [[nodiscard]] usize remaining() const noexcept { return m_capacity - m_offset; }

private:
    byte*  m_buffer   = nullptr;
    usize  m_capacity = 0;
    usize  m_offset   = 0;
};

} // namespace daedalus
