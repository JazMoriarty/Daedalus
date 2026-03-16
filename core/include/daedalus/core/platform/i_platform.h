#pragma once

#include "daedalus/core/types.h"

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace daedalus
{

// ─── Platform errors ──────────────────────────────────────────────────────────

enum class PlatformError
{
    FileNotFound,
    PermissionDenied,
    IOError,
    OutOfMemory,
    NotSupported,
};

using PlatformResult = std::expected<void, PlatformError>;

template<typename T>
using PlatformValueResult = std::expected<T, PlatformError>;

// ─── IPlatform ────────────────────────────────────────────────────────────────
// Abstract platform services. One concrete implementation per target OS.
// Injected at startup; subsystems hold a const reference, never call OS APIs
// directly.

class IPlatform
{
public:
    virtual ~IPlatform() = default;

    // Non-copyable, non-movable — owned through a unique_ptr held by the
    // application root.
    IPlatform(const IPlatform&)            = delete;
    IPlatform& operator=(const IPlatform&) = delete;

    // ─── File I/O ─────────────────────────────────────────────────────────────

    /// Read the entire contents of a file at `path` into a byte vector.
    [[nodiscard]] virtual PlatformValueResult<std::vector<byte>>
    readFile(std::string_view path) const = 0;

    /// Write `data` to the file at `path`, creating it if necessary.
    [[nodiscard]] virtual PlatformResult
    writeFile(std::string_view path, std::span<const byte> data) const = 0;

    /// Returns true if a regular file exists at `path` and is readable.
    [[nodiscard]] virtual bool fileExists(std::string_view path) const noexcept = 0;

    // ─── Time ─────────────────────────────────────────────────────────────────

    /// Returns a monotonic high-resolution timestamp in seconds.
    /// The epoch is undefined; suitable only for measuring elapsed time.
    [[nodiscard]] virtual f64 getTimeSeconds() const noexcept = 0;

    // ─── Environment ──────────────────────────────────────────────────────────

    /// Returns the directory that contains the running executable.
    [[nodiscard]] virtual std::string getExecutableDir() const = 0;

    /// Returns the directory where runtime resources (assets, shaders, etc.) are located.
    /// For standalone executables: same as getExecutableDir().
    /// For macOS app bundles: Contents/MacOS/ (where executable and resources are copied).
    /// This makes resource loading agnostic to launch mode.
    [[nodiscard]] virtual std::string getResourceDir() const = 0;

protected:
    IPlatform() = default;
};

} // namespace daedalus
