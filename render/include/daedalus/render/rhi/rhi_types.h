#pragma once

#include "daedalus/core/types.h"

#include <array>
#include <string>

namespace daedalus::rhi
{

// ─── TextureFormat ────────────────────────────────────────────────────────────

enum class TextureFormat : u32
{
    // Colour formats
    RGBA8Unorm,
    RGBA8Unorm_sRGB,
    BGRA8Unorm,          // primary swapchain format on macOS/Metal
    BGRA8Unorm_sRGB,
    RGBA16Float,
    RG16Float,
    R32Float,
    RG32Float,
    RGBA32Float,
    R11G11B10Float,      // HDR emission / light accumulation

    // Depth / stencil formats
    Depth32Float,
    Depth24Stencil8,

    Invalid,
};

// ─── TextureUsage ─────────────────────────────────────────────────────────────

enum class TextureUsage : u32
{
    None            = 0,
    ShaderRead      = 1 << 0,
    ShaderWrite     = 1 << 1,
    RenderTarget    = 1 << 2,
    DepthStencil    = 1 << 3,
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept
{
    return static_cast<TextureUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}

inline bool hasFlag(TextureUsage flags, TextureUsage flag) noexcept
{
    return (static_cast<u32>(flags) & static_cast<u32>(flag)) != 0;
}

// ─── BufferUsage ──────────────────────────────────────────────────────────────

enum class BufferUsage : u32
{
    None            = 0,
    Vertex          = 1 << 0,
    Index           = 1 << 1,
    Uniform         = 1 << 2,
    Storage         = 1 << 3,
    Staging         = 1 << 4,  // CPU-writable, GPU-readable transfer buffer
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept
{
    return static_cast<BufferUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}

// ─── Primitive types ──────────────────────────────────────────────────────────

enum class PrimitiveType : u32
{
    TriangleList,
    TriangleStrip,
    LineList,
    PointList,
};

// ─── Load / store actions ─────────────────────────────────────────────────────

enum class LoadAction : u32
{
    Load,       // Keep existing contents
    Clear,      // Clear to ClearValue before rendering
    DontCare,   // Contents undefined before rendering (maximally efficient)
};

enum class StoreAction : u32
{
    Store,      // Persist rendered contents for later use
    DontCare,   // Contents may be discarded after rendering
    Resolve,    // Multi-sample resolve into resolve texture
};

// ─── ClearColor ───────────────────────────────────────────────────────────────

struct ClearColor
{
    f32 r = 0.0f;
    f32 g = 0.0f;
    f32 b = 0.0f;
    f32 a = 1.0f;
};

// ─── Descriptors ─────────────────────────────────────────────────────────────

struct BufferDescriptor
{
    usize       size      = 0;
    BufferUsage usage     = BufferUsage::None;
    const void* initData  = nullptr; // optional initial contents
    std::string debugName;
};

struct TextureDescriptor
{
    u32          width      = 1;
    u32          height     = 1;
    u32          depth      = 1;
    u32          mipLevels  = 1;
    u32          arrayLayers= 1;
    TextureFormat format    = TextureFormat::RGBA8Unorm;
    TextureUsage  usage     = TextureUsage::ShaderRead;
    std::string   debugName;
};

struct SamplerDescriptor
{
    enum class Filter : u32 { Nearest, Linear };
    enum class AddressMode : u32 { Repeat, MirrorRepeat, ClampToEdge, ClampToBorder };

    Filter      magFilter   = Filter::Linear;
    Filter      minFilter   = Filter::Linear;
    Filter      mipFilter   = Filter::Linear;
    AddressMode addressU    = AddressMode::Repeat;
    AddressMode addressV    = AddressMode::Repeat;
    AddressMode addressW    = AddressMode::Repeat;
    f32         maxAnisotropy = 1.0f;
    std::string debugName;
};

// ─── RenderPass descriptors ───────────────────────────────────────────────────

struct ColorAttachmentDescriptor
{
    // Pointer to the ITexture* — intentionally left as a raw pointer to avoid
    // circular includes.  The RHI backend casts to the concrete type.
    void*       texture    = nullptr;
    LoadAction  loadAction = LoadAction::Clear;
    StoreAction storeAction= StoreAction::Store;
    ClearColor  clearColor;
};

struct DepthAttachmentDescriptor
{
    void*       texture    = nullptr;
    LoadAction  loadAction = LoadAction::Clear;
    StoreAction storeAction= StoreAction::DontCare;
    f32         clearDepth = 1.0f;
};

static constexpr u32 MAX_COLOR_ATTACHMENTS = 8;

struct RenderPassDescriptor
{
    std::array<ColorAttachmentDescriptor, MAX_COLOR_ATTACHMENTS> colorAttachments;
    u32                  colorAttachmentCount = 0;
    DepthAttachmentDescriptor depthAttachment;
    bool                 hasDepthAttachment   = false;
    std::string          debugLabel;
};

// ─── Viewport & scissor ───────────────────────────────────────────────────────

struct Viewport
{
    f32 x      = 0.0f;
    f32 y      = 0.0f;
    f32 width  = 0.0f;
    f32 height = 0.0f;
    f32 nearZ  = 0.0f;
    f32 farZ   = 1.0f;
};

struct ScissorRect
{
    i32 x      = 0;
    i32 y      = 0;
    u32 width  = 0;
    u32 height = 0;
};

} // namespace daedalus::rhi
