#pragma once

#include "daedalus/render/rhi/i_render_device.h"

#include <memory>

namespace daedalus::rhi
{

/// Create and return the platform-appropriate render device.
/// On macOS this is the Metal backend; on Windows the Vulkan backend.
/// The concrete type is selected at compile time; callers only see IRenderDevice.
[[nodiscard]] std::unique_ptr<IRenderDevice> createRenderDevice();

} // namespace daedalus::rhi
