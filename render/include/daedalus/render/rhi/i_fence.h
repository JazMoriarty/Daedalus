#pragma once

#include "daedalus/core/types.h"

namespace daedalus::rhi
{

// ─── IFence ───────────────────────────────────────────────────────────────────
// A CPU/GPU synchronisation primitive.  The GPU signals the fence when a
// command buffer committed to a queue has finished executing; the CPU can then
// wait on it.

class IFence
{
public:
    virtual ~IFence() = default;

    IFence(const IFence&)            = delete;
    IFence& operator=(const IFence&) = delete;

    /// Block the calling CPU thread until the GPU has signalled this fence.
    virtual void waitUntilCompleted() noexcept = 0;

    /// Returns true if the GPU has already signalled this fence.
    [[nodiscard]] virtual bool isCompleted() const noexcept = 0;

protected:
    IFence() = default;
};

} // namespace daedalus::rhi
