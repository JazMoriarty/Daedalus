#pragma once

#include "daedalus/render/rhi/rhi_types.h"
#include "daedalus/render/rhi/i_shader.h"

#include <vector>

namespace daedalus::rhi
{

// ─── Pipeline descriptors ───────────────────────────────────────────────────────

struct RenderPipelineDescriptor
{
    IShader* vertexShader   = nullptr;
    IShader* fragmentShader = nullptr;

    // ─── Colour attachments ────────────────────────────────────────────────────
    u32 colorAttachmentCount = 0;
    std::array<TextureFormat, MAX_COLOR_ATTACHMENTS>                colorFormats = {};
    std::array<ColorAttachmentBlendDescriptor, MAX_COLOR_ATTACHMENTS> blendStates = {};

    // ─── Depth ─────────────────────────────────────────────────────────────
    TextureFormat   depthFormat  = TextureFormat::Invalid;
    bool            depthTest    = false;
    bool            depthWrite   = false;
    CompareFunction depthCompare = CompareFunction::Less;

    // ─── Rasterizer ─────────────────────────────────────────────────────────
    PrimitiveType primitiveType = PrimitiveType::TriangleList;
    CullMode      cullMode      = CullMode::Back;

    // ─── Vertex input ────────────────────────────────────────────────────────
    std::vector<VertexAttributeDescriptor>    vertexAttributes;
    std::vector<VertexBufferLayoutDescriptor> vertexBufferLayouts;

    std::string debugName;
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
