#include "daedalus/core/memory/pool_allocator.h"
#include "daedalus/core/assert.h"

#include <cstdlib>

namespace daedalus
{

PoolAllocator::PoolAllocator(usize blockSize, usize blockCount)
{
    DAEDALUS_ASSERT(blockSize >= sizeof(FreeBlock),
                    "PoolAllocator: blockSize must be >= sizeof(void*)");
    DAEDALUS_ASSERT(blockCount > 0, "PoolAllocator: blockCount must be > 0");

    m_blockSize  = blockSize;
    m_blockCount = blockCount;
    m_freeCount  = blockCount;

    // Allocate the contiguous backing buffer.  16-byte alignment satisfies
    // most component types without extra padding.
    m_buffer = static_cast<byte*>(std::aligned_alloc(16, blockSize * blockCount));
    DAEDALUS_ASSERT(m_buffer != nullptr, "PoolAllocator: backing allocation failed");

    // Build the initial free list by linking consecutive blocks.
    m_freeHead = reinterpret_cast<FreeBlock*>(m_buffer);
    FreeBlock* current = m_freeHead;
    for (usize i = 0; i + 1 < blockCount; ++i)
    {
        byte* nextAddr = m_buffer + (i + 1) * blockSize;
        current->next  = reinterpret_cast<FreeBlock*>(nextAddr);
        current        = current->next;
    }
    current->next = nullptr; // last block
}

PoolAllocator::~PoolAllocator()
{
    if (m_freeCount != m_blockCount)
    {
        std::fprintf(stderr,
            "[Daedalus] PoolAllocator destroyed with %zu block(s) still allocated\n",
            m_blockCount - m_freeCount);
    }
    std::free(m_buffer);
}

void* PoolAllocator::allocate(usize size, usize /*alignment*/)
{
    DAEDALUS_ASSERT(size <= m_blockSize,
                    "PoolAllocator::allocate: requested size exceeds block size");
    DAEDALUS_ASSERT(m_freeHead != nullptr, "PoolAllocator: pool exhausted");

    FreeBlock* block = m_freeHead;
    m_freeHead       = block->next;
    --m_freeCount;
    return block;
}

void PoolAllocator::deallocate(void* ptr, usize size) noexcept
{
    DAEDALUS_ASSERT(size <= m_blockSize,
                    "PoolAllocator::deallocate: size does not match block size");
    DAEDALUS_ASSERT(ptr != nullptr, "PoolAllocator::deallocate: null pointer");

    // Return this block to the head of the free list.
    auto* block  = static_cast<FreeBlock*>(ptr);
    block->next  = m_freeHead;
    m_freeHead   = block;
    ++m_freeCount;
}

} // namespace daedalus
