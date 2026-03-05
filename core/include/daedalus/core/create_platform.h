#pragma once

#include "daedalus/core/platform/i_platform.h"

#include <memory>

namespace daedalus
{

/// Create and return the platform-appropriate IPlatform implementation.
/// The concrete type is selected at compile time.
[[nodiscard]] std::unique_ptr<IPlatform> createPlatform();

} // namespace daedalus
