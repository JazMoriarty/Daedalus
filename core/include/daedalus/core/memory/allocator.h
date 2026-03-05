#pragma once

#include "daedalus/core/types.h"

#include <memory>
#include <new>
#include <utility>

namespace daedalus
{

// ─── IAllocator ───────────────────────────────────────────────────────────────
// Minimal allocator contract shared by all concrete allocators.
// Allocators are neither copyable nor movable; they are always accessed through
// a pointer or reference.

class IAllocator
{
public:
    virtual ~IAllocator() = default;

    IAllocator(const IAllocator&)            = delete;
    IAllocator& operator=(const IAllocator&) = delete;

    /// Allocate `size` bytes with at least `alignment` alignment.
    /// Returns a non-null pointer on success; asserts on allocation failure.
    [[nodiscard]] virtual void* allocate(usize size,
                                         usize alignment = alignof(std::max_align_t)) = 0;

    /// Release memory previously returned by allocate().
    /// `size` must match the value passed to the corresponding allocate() call.
    virtual void deallocate(void* ptr, usize size) noexcept = 0;

    // ─── Typed helpers ────────────────────────────────────────────────────────

    /// Allocate memory for T and construct it with `args`.
    template<typename T, typename... Args>
    [[nodiscard]] T* construct(Args&&... args)
    {
        void* mem = allocate(sizeof(T), alignof(T));
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    /// Destruct T and release its memory back to this allocator.
    template<typename T>
    void destroy(T* ptr) noexcept
    {
        if (ptr)
        {
            ptr->~T();
            deallocate(ptr, sizeof(T));
        }
    }

protected:
    IAllocator() = default;
};

} // namespace daedalus
