#pragma once

#include "daedalus/core/platform/i_platform.h"

namespace daedalus
{

// ─── MacosPlatform ────────────────────────────────────────────────────────────
// macOS implementation of IPlatform.  Uses std::fstream for file I/O and
// std::chrono for time.  No Objective-C required.

class MacosPlatform final : public IPlatform
{
public:
    MacosPlatform()  = default;
    ~MacosPlatform() override = default;

    [[nodiscard]] PlatformValueResult<std::vector<byte>>
    readFile(std::string_view path) const override;

    [[nodiscard]] PlatformResult
    writeFile(std::string_view path, std::span<const byte> data) const override;

    [[nodiscard]] bool fileExists(std::string_view path) const noexcept override;

    [[nodiscard]] f64 getTimeSeconds() const noexcept override;

    [[nodiscard]] std::string getExecutableDir() const override;
};

} // namespace daedalus
