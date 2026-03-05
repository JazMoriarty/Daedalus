#pragma once

#include "daedalus/core/types.h"

namespace daedalus::rhi
{

// ─── IBindGroup ───────────────────────────────────────────────────────────────
// An immutable snapshot of resource bindings (buffers, textures, samplers) for
// use by a pipeline stage.  Corresponds to a descriptor set (Vulkan) or
// argument buffer (Metal).

class IBindGroup
{
public:
    virtual ~IBindGroup() = default;

    IBindGroup(const IBindGroup&)            = delete;
    IBindGroup& operator=(const IBindGroup&) = delete;

protected:
    IBindGroup() = default;
};

} // namespace daedalus::rhi
