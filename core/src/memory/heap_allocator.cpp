#include "daedalus/core/memory/heap_allocator.h"
#include "daedalus/core/assert.h"

#include <cstdlib>
#include <cstdio>

namespace daedalus
{

HeapAllocator::~HeapAllocator()
{
    const i64 count = m_allocationCount.load(std::memory_order_relaxed);
    if (count != 0)
    {
        std::fprintf(stderr,
            "[Daedalus] HeapAllocator destroyed with %lld outstanding allocation(s) "
            "(%zu bytes)\n",
            static_cast<long long>(count),
            m_bytesAllocated.load(std::memory_order_relaxed));
    }
}

void* HeapAllocator::allocate(usize size, usize alignment)
{
    DAEDALUS_ASSERT(size > 0,      "allocate: size must be > 0");
    DAEDALUS_ASSERT(alignment > 0, "allocate: alignment must be > 0");

    // alignment must be a power of two for aligned_alloc.
    DAEDALUS_ASSERT((alignment & (alignment - 1)) == 0,
                    "allocate: alignment must be a power of two");

    // aligned_alloc requires size to be a multiple of alignment.
    const usize alignedSize = (size + alignment - 1) & ~(alignment - 1);

    void* ptr = std::aligned_alloc(alignment, alignedSize);
    DAEDALUS_ASSERT(ptr != nullptr, "HeapAllocator: system allocation failed");

    m_bytesAllocated.fetch_add(alignedSize, std::memory_order_relaxed);
    m_allocationCount.fetch_add(1,          std::memory_order_relaxed);

    return ptr;
}

void HeapAllocator::deallocate(void* ptr, usize size) noexcept
{
    if (!ptr) { return; }
    std::free(ptr);
    m_bytesAllocated.fetch_sub(size,  std::memory_order_relaxed);
    m_allocationCount.fetch_sub(1,    std::memory_order_relaxed);
}

} // namespace daedalus
