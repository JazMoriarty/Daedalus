#pragma once

#include "daedalus/render/rhi/rhi_types.h"
#include "daedalus/render/rhi/i_shader.h"

namespace daedalus::rhi
{

// ─── Pipeline descriptors ─────────────────────────────────────────────────────

struct RenderPipelineDescriptor
{
    IShader* vertexShader   = nullptr;
    IShader* fragmentShader = nullptr;

    // Colour attachment formats must match the render pass being used with.
    u32                                          colorAttachmentCount = 0;
    std::array<TextureFormat, MAX_COLOR_ATTACHMENTS> colorFormats     = {};
    TextureFormat depthFormat   = TextureFormat::Invalid;
    PrimitiveType primitiveType = PrimitiveType::TriangleList;
    bool          depthTest     = false;
    bool          depthWrite    = false;
    std::string   debugName;
};

struct ComputePipelineDescriptor
{
    IShader*    computeShader = nullptr;
    std::string debugName;
};

// ─── IPipeline ────────────────────────────────────────────────────────────────

class IPipeline
{
public:
    virtual ~IPipeline() = default;

    IPipeline(const IPipeline&)            = delete;
    IPipeline& operator=(const IPipeline&) = delete;

protected:
    IPipeline() = default;
};

} // namespace daedalus::rhi
