#pragma once

#include "daedalus/render/rhi/i_render_device.h"

#include <memory>

namespace daedalus::rhi
{

/// Create and return the Metal-backed IRenderDevice.
/// Asserts if no suitable Metal device is found (requires Apple Silicon or
/// discrete GPU with Metal support).
[[nodiscard]] std::unique_ptr<IRenderDevice> createMetalRenderDevice();

} // namespace daedalus::rhi
