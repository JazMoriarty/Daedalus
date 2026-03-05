#pragma once

#include "daedalus/render/rhi/i_command_buffer.h"

#include <memory>
#include <string_view>

namespace daedalus::rhi
{

// ─── ICommandQueue ────────────────────────────────────────────────────────────
// A submission queue for GPU work.  Command buffers are created from a queue
// and remain associated with it through their lifetime.

class ICommandQueue
{
public:
    virtual ~ICommandQueue() = default;

    ICommandQueue(const ICommandQueue&)            = delete;
    ICommandQueue& operator=(const ICommandQueue&) = delete;

    /// Create a new command buffer for recording.
    [[nodiscard]] virtual std::unique_ptr<ICommandBuffer>
    createCommandBuffer(std::string_view debugLabel = {}) = 0;

protected:
    ICommandQueue() = default;
};

} // namespace daedalus::rhi
