#pragma once

#include "daedalus/render/rhi/rhi_types.h"

namespace daedalus::rhi
{

// ─── ITexture ─────────────────────────────────────────────────────────────────
// An opaque GPU texture handle.

class ITexture
{
public:
    virtual ~ITexture() = default;

    ITexture(const ITexture&)            = delete;
    ITexture& operator=(const ITexture&) = delete;

    [[nodiscard]] virtual u32          width()     const noexcept = 0;
    [[nodiscard]] virtual u32          height()    const noexcept = 0;
    [[nodiscard]] virtual u32          mipLevels() const noexcept = 0;
    [[nodiscard]] virtual TextureFormat format()   const noexcept = 0;
    [[nodiscard]] virtual TextureUsage  usage()    const noexcept = 0;

protected:
    ITexture() = default;
};

} // namespace daedalus::rhi
