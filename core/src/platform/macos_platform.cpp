#include "macos_platform.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <mach-o/dyld.h>

namespace daedalus
{

PlatformValueResult<std::vector<byte>>
MacosPlatform::readFile(std::string_view path) const
{
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        // Distinguish "not found" from "permission denied" via errno.
        return std::unexpected(errno == EACCES ? PlatformError::PermissionDenied
                                               : PlatformError::FileNotFound);
    }

    const std::streamsize size = file.tellg();
    if (size < 0)
    {
        return std::unexpected(PlatformError::IOError);
    }

    std::vector<byte> buffer(static_cast<usize>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        return std::unexpected(PlatformError::IOError);
    }

    return buffer;
}

PlatformResult
MacosPlatform::writeFile(std::string_view path, std::span<const byte> data) const
{
    std::ofstream file(std::string(path), std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        return std::unexpected(errno == EACCES ? PlatformError::PermissionDenied
                                               : PlatformError::IOError);
    }

    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));

    if (!file)
    {
        return std::unexpected(PlatformError::IOError);
    }

    return {}; // std::expected<void, ...> success
}

bool MacosPlatform::fileExists(std::string_view path) const noexcept
{
    return std::filesystem::is_regular_file(std::string(path));
}

f64 MacosPlatform::getTimeSeconds() const noexcept
{
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<f64>;
    static const auto s_epoch = Clock::now();
    return std::chrono::duration_cast<Seconds>(Clock::now() - s_epoch).count();
}

std::string MacosPlatform::getExecutableDir() const
{
    // Use the macOS-specific _NSGetExecutablePath.
    char    path[PATH_MAX] = {};
    uint32_t size          = static_cast<uint32_t>(sizeof(path));
    if (_NSGetExecutablePath(path, &size) != 0)
    {
        return {};
    }

    // Resolve symlinks and return the directory component.
    char resolved[PATH_MAX] = {};
    if (realpath(path, resolved) == nullptr)
    {
        return {};
    }

    return std::filesystem::path(resolved).parent_path().string();
}

} // namespace daedalus
