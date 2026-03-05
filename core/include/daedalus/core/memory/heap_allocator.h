#pragma once

#include "daedalus/core/memory/allocator.h"

#include <atomic>

namespace daedalus
{

// ─── HeapAllocator ────────────────────────────────────────────────────────────
// General-purpose allocator backed by the system heap (malloc / aligned_alloc).
// In debug builds, tracks live allocation count and total bytes; asserts that
// all memory has been released on destruction.

class HeapAllocator final : public IAllocator
{
public:
    HeapAllocator()  = default;
    ~HeapAllocator() override;

    [[nodiscard]] void* allocate(usize size,
                                 usize alignment = alignof(std::max_align_t)) override;

    void deallocate(void* ptr, usize size) noexcept override;

    // ─── Diagnostics (always available, zero-cost in release) ─────────────────

    /// Total bytes currently outstanding.
    [[nodiscard]] usize bytesAllocated() const noexcept
    {
        return m_bytesAllocated.load(std::memory_order_relaxed);
    }

    /// Number of live allocations (allocate calls minus deallocate calls).
    [[nodiscard]] i64 allocationCount() const noexcept
    {
        return m_allocationCount.load(std::memory_order_relaxed);
    }

private:
    std::atomic<usize> m_bytesAllocated{0};
    std::atomic<i64>   m_allocationCount{0};
};

} // namespace daedalus
