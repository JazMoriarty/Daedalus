#pragma once

#include "daedalus/render/rhi/rhi_types.h"
#include "daedalus/render/rhi/i_texture.h"

namespace daedalus::rhi
{

// ─── ISwapchain ───────────────────────────────────────────────────────────────
// The presentable back-buffer surface tied to a native window.

class ISwapchain
{
public:
    virtual ~ISwapchain() = default;

    ISwapchain(const ISwapchain&)            = delete;
    ISwapchain& operator=(const ISwapchain&) = delete;

    /// Acquire the next drawable texture to render into for this frame.
    /// Returns a non-owning pointer; ownership stays with the swapchain.
    /// Must be called once per frame before recording render commands.
    [[nodiscard]] virtual ITexture* nextDrawable() = 0;

    /// Present the last drawable acquired with nextDrawable().
    /// This queues a present command; actual flip happens after commit().
    virtual void present() = 0;

    /// Resize the swapchain to match a new window size.
    virtual void resize(u32 width, u32 height) = 0;

    [[nodiscard]] virtual u32          width()  const noexcept = 0;
    [[nodiscard]] virtual u32          height() const noexcept = 0;
    [[nodiscard]] virtual TextureFormat format() const noexcept = 0;

    /// Backend-specific handle for the current drawable (e.g. id<CAMetalDrawable> on Metal).
    /// Returns nullptr for offscreen swapchains or before the first nextDrawable() call.
    [[nodiscard]] virtual void* currentDrawableHandle() noexcept = 0;

protected:
    ISwapchain() = default;
};

} // namespace daedalus::rhi
