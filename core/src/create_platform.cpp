#include "daedalus/core/create_platform.h"
#include "daedalus/core/assert.h"

#if defined(__APPLE__)
#include "platform/macos_platform.h"
#endif

namespace daedalus
{

std::unique_ptr<IPlatform> createPlatform()
{
#if defined(__APPLE__)
    return std::make_unique<MacosPlatform>();
#else
    DAEDALUS_ASSERT_FAIL("createPlatform: unsupported platform");
    return nullptr;
#endif
}

} // namespace daedalus
