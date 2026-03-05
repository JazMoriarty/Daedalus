#pragma once

#include "daedalus/core/memory/allocator.h"

namespace daedalus
{

// ─── PoolAllocator ────────────────────────────────────────────────────────────
// Fixed-size block allocator backed by a single contiguous buffer.
// Allocate and deallocate are O(1).  Each block must be exactly `blockSize`
// bytes; requesting a different size asserts in debug builds.
//
// Useful for component arrays, small message objects, or any homogeneous pool
// where object sizes are known at construction time.
//
// Not thread-safe.

class PoolAllocator final : public IAllocator
{
public:
    /// `blockSize` is the size in bytes of each slot (>= sizeof(void*)).
    /// `blockCount` is the maximum number of live allocations.
    PoolAllocator(usize blockSize, usize blockCount);
    ~PoolAllocator() override;

    /// Returns a free block.  `size` must equal the block size passed at
    /// construction; alignment must be <= the block alignment.
    /// Asserts if the pool is exhausted.
    [[nodiscard]] void* allocate(usize size,
                                 usize alignment = alignof(std::max_align_t)) override;

    /// Return `ptr` to the free list.  `size` must equal the block size.
    void deallocate(void* ptr, usize size) noexcept override;

    // ─── Diagnostics ──────────────────────────────────────────────────────────

    [[nodiscard]] usize blockSize()  const noexcept { return m_blockSize; }
    [[nodiscard]] usize blockCount() const noexcept { return m_blockCount; }
    [[nodiscard]] usize freeCount()  const noexcept { return m_freeCount; }
    [[nodiscard]] usize usedCount()  const noexcept { return m_blockCount - m_freeCount; }

private:
    // Free blocks are stored as an intrusive singly-linked list.  Each free
    // slot contains a pointer to the next free slot as its first word.
    struct FreeBlock
    {
        FreeBlock* next = nullptr;
    };

    byte*      m_buffer     = nullptr;
    FreeBlock* m_freeHead   = nullptr;
    usize      m_blockSize  = 0;
    usize      m_blockCount = 0;
    usize      m_freeCount  = 0;
};

} // namespace daedalus
