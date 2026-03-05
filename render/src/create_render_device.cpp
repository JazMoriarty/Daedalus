#include "daedalus/render/create_render_device.h"
#include "daedalus/core/assert.h"

#if defined(DAEDALUS_BACKEND_METAL)
#include "rhi/backends/metal/metal_render_device.h"
#elif defined(DAEDALUS_BACKEND_VULKAN)
// Vulkan backend factory will be added in a future phase.
#endif

namespace daedalus::rhi
{

std::unique_ptr<IRenderDevice> createRenderDevice()
{
#if defined(DAEDALUS_BACKEND_METAL)
    return createMetalRenderDevice();
#elif defined(DAEDALUS_BACKEND_VULKAN)
    DAEDALUS_ASSERT_FAIL("Vulkan backend not yet implemented");
    return nullptr;
#else
    DAEDALUS_ASSERT_FAIL("No render backend configured");
    return nullptr;
#endif
}

} // namespace daedalus::rhi
