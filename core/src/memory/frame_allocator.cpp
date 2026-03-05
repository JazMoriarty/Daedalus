#include "daedalus/core/memory/frame_allocator.h"
#include "daedalus/core/assert.h"

#include <cstdlib>

namespace daedalus
{

FrameAllocator::FrameAllocator(usize capacityBytes)
    : m_capacity(capacityBytes)
{
    DAEDALUS_ASSERT(capacityBytes > 0, "FrameAllocator: capacity must be > 0");
    // 16-byte alignment for the backing buffer covers all SIMD requirements.
    m_buffer = static_cast<byte*>(std::aligned_alloc(16, capacityBytes));
    DAEDALUS_ASSERT(m_buffer != nullptr, "FrameAllocator: backing allocation failed");
}

FrameAllocator::~FrameAllocator()
{
    std::free(m_buffer);
}

void* FrameAllocator::allocate(usize size, usize alignment)
{
    DAEDALUS_ASSERT(size > 0,      "FrameAllocator::allocate: size must be > 0");
    DAEDALUS_ASSERT(alignment > 0, "FrameAllocator::allocate: alignment must be > 0");
    DAEDALUS_ASSERT((alignment & (alignment - 1)) == 0,
                    "FrameAllocator::allocate: alignment must be a power of two");

    // Align the current offset upward.
    const usize aligned = (m_offset + alignment - 1) & ~(alignment - 1);
    const usize end     = aligned + size;

    DAEDALUS_ASSERT(end <= m_capacity,
                    "FrameAllocator::allocate: frame budget exhausted");

    m_offset = end;
    return m_buffer + aligned;
}

void FrameAllocator::reset() noexcept
{
    m_offset = 0;
}

} // namespace daedalus
