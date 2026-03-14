// metal_render_device.mm
// Complete Metal backend for all RHI interfaces.
// Compiled as Objective-C++ (ARC enabled).

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "metal_render_device.h"

#include "daedalus/core/assert.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_command_queue.h"
#include "daedalus/render/rhi/i_command_buffer.h"
#include "daedalus/render/rhi/i_render_pass_encoder.h"
#include "daedalus/render/rhi/i_compute_pass_encoder.h"
#include "daedalus/render/rhi/i_swapchain.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/rhi/i_sampler.h"
#include "daedalus/render/rhi/i_shader.h"
#include "daedalus/render/rhi/i_pipeline.h"
#include "daedalus/render/rhi/i_fence.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace daedalus::rhi
{

// ─── Helper: enum conversions ─────────────────────────────────────────────────

static MTLPixelFormat toMTL(TextureFormat fmt) noexcept
{
    switch (fmt)
    {
        case TextureFormat::RGBA8Unorm:       return MTLPixelFormatRGBA8Unorm;
        case TextureFormat::RGBA8Unorm_sRGB:  return MTLPixelFormatRGBA8Unorm_sRGB;
        case TextureFormat::BGRA8Unorm:       return MTLPixelFormatBGRA8Unorm;
        case TextureFormat::BGRA8Unorm_sRGB:  return MTLPixelFormatBGRA8Unorm_sRGB;
        case TextureFormat::RGBA16Float:      return MTLPixelFormatRGBA16Float;
        case TextureFormat::RG16Float:        return MTLPixelFormatRG16Float;
        case TextureFormat::R32Float:         return MTLPixelFormatR32Float;
        case TextureFormat::RG32Float:        return MTLPixelFormatRG32Float;
        case TextureFormat::RGBA32Float:      return MTLPixelFormatRGBA32Float;
        case TextureFormat::R11G11B10Float:   return MTLPixelFormatRG11B10Float;
        case TextureFormat::Depth32Float:     return MTLPixelFormatDepth32Float;
        case TextureFormat::Depth24Stencil8:  return MTLPixelFormatDepth24Unorm_Stencil8;
        default:                              return MTLPixelFormatInvalid;
    }
}

static TextureFormat fromMTL(MTLPixelFormat fmt) noexcept
{
    switch (fmt)
    {
        case MTLPixelFormatRGBA8Unorm:           return TextureFormat::RGBA8Unorm;
        case MTLPixelFormatRGBA8Unorm_sRGB:      return TextureFormat::RGBA8Unorm_sRGB;
        case MTLPixelFormatBGRA8Unorm:           return TextureFormat::BGRA8Unorm;
        case MTLPixelFormatBGRA8Unorm_sRGB:      return TextureFormat::BGRA8Unorm_sRGB;
        case MTLPixelFormatRGBA16Float:          return TextureFormat::RGBA16Float;
        case MTLPixelFormatRG16Float:            return TextureFormat::RG16Float;
        case MTLPixelFormatR32Float:             return TextureFormat::R32Float;
        case MTLPixelFormatRG32Float:            return TextureFormat::RG32Float;
        case MTLPixelFormatRGBA32Float:          return TextureFormat::RGBA32Float;
        case MTLPixelFormatRG11B10Float:         return TextureFormat::R11G11B10Float;
        case MTLPixelFormatDepth32Float:         return TextureFormat::Depth32Float;
        case MTLPixelFormatDepth24Unorm_Stencil8:return TextureFormat::Depth24Stencil8;
        default:                                 return TextureFormat::Invalid;
    }
}

static MTLLoadAction toMTL(LoadAction a) noexcept
{
    switch (a)
    {
        case LoadAction::Load:     return MTLLoadActionLoad;
        case LoadAction::Clear:    return MTLLoadActionClear;
        case LoadAction::DontCare: return MTLLoadActionDontCare;
    }
    return MTLLoadActionDontCare;
}

static MTLStoreAction toMTL(StoreAction a) noexcept
{
    switch (a)
    {
        case StoreAction::Store:    return MTLStoreActionStore;
        case StoreAction::DontCare: return MTLStoreActionDontCare;
        case StoreAction::Resolve:  return MTLStoreActionMultisampleResolve;
    }
    return MTLStoreActionStore;
}

static MTLSamplerMinMagFilter toMTLMinMag(SamplerDescriptor::Filter f) noexcept
{
    return f == SamplerDescriptor::Filter::Linear
           ? MTLSamplerMinMagFilterLinear
           : MTLSamplerMinMagFilterNearest;
}

static MTLSamplerAddressMode toMTL(SamplerDescriptor::AddressMode m) noexcept
{
    switch (m)
    {
        case SamplerDescriptor::AddressMode::Repeat:       return MTLSamplerAddressModeRepeat;
        case SamplerDescriptor::AddressMode::MirrorRepeat: return MTLSamplerAddressModeMirrorRepeat;
        case SamplerDescriptor::AddressMode::ClampToEdge:  return MTLSamplerAddressModeClampToEdge;
        case SamplerDescriptor::AddressMode::ClampToBorder:return MTLSamplerAddressModeClampToBorderColor;
    }
    return MTLSamplerAddressModeRepeat;
}

static MTLVertexFormat toMTL(VertexFormat fmt) noexcept
{
    switch (fmt)
    {
        case VertexFormat::Float:            return MTLVertexFormatFloat;
        case VertexFormat::Float2:           return MTLVertexFormatFloat2;
        case VertexFormat::Float3:           return MTLVertexFormatFloat3;
        case VertexFormat::Float4:           return MTLVertexFormatFloat4;
        case VertexFormat::Half2:            return MTLVertexFormatHalf2;
        case VertexFormat::Half4:            return MTLVertexFormatHalf4;
        case VertexFormat::UInt:             return MTLVertexFormatUInt;
        case VertexFormat::UInt2:            return MTLVertexFormatUInt2;
        case VertexFormat::UByte4Normalized: return MTLVertexFormatUChar4Normalized;
        case VertexFormat::Short2Normalized: return MTLVertexFormatShort2Normalized;
    }
    return MTLVertexFormatFloat;
}

static MTLCompareFunction toMTL(CompareFunction fn) noexcept
{
    switch (fn)
    {
        case CompareFunction::Never:        return MTLCompareFunctionNever;
        case CompareFunction::Less:         return MTLCompareFunctionLess;
        case CompareFunction::LessEqual:    return MTLCompareFunctionLessEqual;
        case CompareFunction::Equal:        return MTLCompareFunctionEqual;
        case CompareFunction::GreaterEqual: return MTLCompareFunctionGreaterEqual;
        case CompareFunction::Greater:      return MTLCompareFunctionGreater;
        case CompareFunction::NotEqual:     return MTLCompareFunctionNotEqual;
        case CompareFunction::Always:       return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionLess;
}

static MTLCullMode toMTL(CullMode mode) noexcept
{
    switch (mode)
    {
        case CullMode::None:  return MTLCullModeNone;
        case CullMode::Front: return MTLCullModeFront;
        case CullMode::Back:  return MTLCullModeBack;
    }
    return MTLCullModeNone;
}

static MTLBlendFactor toMTL(BlendFactor f) noexcept
{
    switch (f)
    {
        case BlendFactor::Zero:             return MTLBlendFactorZero;
        case BlendFactor::One:              return MTLBlendFactorOne;
        case BlendFactor::SrcAlpha:         return MTLBlendFactorSourceAlpha;
        case BlendFactor::OneMinusSrcAlpha: return MTLBlendFactorOneMinusSourceAlpha;
        case BlendFactor::DstAlpha:         return MTLBlendFactorDestinationAlpha;
        case BlendFactor::OneMinusDstAlpha: return MTLBlendFactorOneMinusDestinationAlpha;
        case BlendFactor::SrcColor:         return MTLBlendFactorSourceColor;
        case BlendFactor::OneMinusSrcColor: return MTLBlendFactorOneMinusSourceColor;
    }
    return MTLBlendFactorOne;
}

static MTLBlendOperation toMTL(BlendOperation op) noexcept
{
    switch (op)
    {
        case BlendOperation::Add:             return MTLBlendOperationAdd;
        case BlendOperation::Subtract:        return MTLBlendOperationSubtract;
        case BlendOperation::ReverseSubtract: return MTLBlendOperationReverseSubtract;
        case BlendOperation::Min:             return MTLBlendOperationMin;
        case BlendOperation::Max:             return MTLBlendOperationMax;
    }
    return MTLBlendOperationAdd;
}

static u32 formatBytesPerPixel(TextureFormat fmt) noexcept
{
    switch (fmt)
    {
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::RGBA8Unorm_sRGB:
        case TextureFormat::BGRA8Unorm:
        case TextureFormat::BGRA8Unorm_sRGB:
        case TextureFormat::R11G11B10Float:
        case TextureFormat::Depth32Float:
        case TextureFormat::RG16Float:
        case TextureFormat::R32Float:
        case TextureFormat::Depth24Stencil8: return 4;
        case TextureFormat::RGBA16Float:
        case TextureFormat::RG32Float:       return 8;
        case TextureFormat::RGBA32Float:     return 16;
        default:                             return 4;
    }
}

// ─── MetalTexture ─────────────────────────────────────────────────────────────

class MetalTexture final : public ITexture
{
public:
    explicit MetalTexture(id<MTLTexture> texture, TextureUsage usage = TextureUsage::ShaderRead)
        : m_texture(texture), m_usage(usage) {}

    ~MetalTexture() override = default;

    [[nodiscard]] u32          width()     const noexcept override { return static_cast<u32>(m_texture.width);     }
    [[nodiscard]] u32          height()    const noexcept override { return static_cast<u32>(m_texture.height);    }
    [[nodiscard]] u32          mipLevels() const noexcept override { return static_cast<u32>(m_texture.mipmapLevelCount); }
    [[nodiscard]] TextureFormat format()   const noexcept override { return fromMTL(m_texture.pixelFormat);        }
    [[nodiscard]] TextureUsage  usage()    const noexcept override { return m_usage;                               }

    [[nodiscard]] id<MTLTexture> mtlTexture() const noexcept { return m_texture; }
    [[nodiscard]] void* nativeHandle() const noexcept override { return (__bridge void*)m_texture; }

private:
    id<MTLTexture> m_texture;
    TextureUsage   m_usage;
};

// ─── MetalBuffer ─────────────────────────────────────────────────────────────

class MetalBuffer final : public IBuffer
{
public:
    MetalBuffer(id<MTLBuffer> buffer, usize size, BufferUsage usage)
        : m_buffer(buffer), m_size(size), m_usage(usage) {}

    ~MetalBuffer() override = default;

    [[nodiscard]] usize       size()  const noexcept override { return m_size;  }
    [[nodiscard]] BufferUsage usage() const noexcept override { return m_usage; }

    [[nodiscard]] void* map()          override { return m_buffer.contents; }
    void                unmap() noexcept override { /* Metal mapping is persistent */ }

    [[nodiscard]] id<MTLBuffer> mtlBuffer() const noexcept { return m_buffer; }

private:
    id<MTLBuffer> m_buffer;
    usize         m_size;
    BufferUsage   m_usage;
};

// ─── MetalSampler ────────────────────────────────────────────────────────────

class MetalSampler final : public ISampler
{
public:
    MetalSampler(id<MTLSamplerState> state, SamplerDescriptor desc)
        : m_state(state), m_desc(std::move(desc)) {}

    ~MetalSampler() override = default;

    [[nodiscard]] const SamplerDescriptor& descriptor() const noexcept override
    {
        return m_desc;
    }

    [[nodiscard]] id<MTLSamplerState> mtlSamplerState() const noexcept { return m_state; }

private:
    id<MTLSamplerState> m_state;
    SamplerDescriptor   m_desc;
};

// ─── MetalShader ─────────────────────────────────────────────────────────────

class MetalShader final : public IShader
{
public:
    MetalShader(id<MTLFunction> function,
                ShaderStage     stage,
                std::string     entryPoint)
        : m_function(function)
        , m_stage(stage)
        , m_entryPoint(std::move(entryPoint))
    {}

    ~MetalShader() override = default;

    [[nodiscard]] ShaderStage     stage()      const noexcept override { return m_stage;      }
    [[nodiscard]] std::string_view entryPoint() const noexcept override { return m_entryPoint; }

    [[nodiscard]] id<MTLFunction> mtlFunction() const noexcept { return m_function; }

private:
    id<MTLFunction> m_function;
    ShaderStage     m_stage;
    std::string     m_entryPoint;
};

// ─── MetalPipeline ────────────────────────────────────────────────────────────

class MetalPipeline final : public IPipeline
{
public:
    /// Render pipeline: owns depth stencil state and cull mode.
    MetalPipeline(id<MTLRenderPipelineState> pso,
                  id<MTLDepthStencilState>   dss,
                  MTLCullMode                cullMode)
        : m_renderPSO(pso), m_depthStencilState(dss), m_cullMode(cullMode) {}

    /// Compute pipeline.
    explicit MetalPipeline(id<MTLComputePipelineState> pso) : m_computePSO(pso) {}

    ~MetalPipeline() override = default;

    [[nodiscard]] id<MTLRenderPipelineState>  renderPSO()         const noexcept { return m_renderPSO;         }
    [[nodiscard]] id<MTLDepthStencilState>    depthStencilState() const noexcept { return m_depthStencilState; }
    [[nodiscard]] MTLCullMode                 cullMode()          const noexcept { return m_cullMode;          }
    [[nodiscard]] id<MTLComputePipelineState> computePSO()        const noexcept { return m_computePSO;        }

private:
    id<MTLRenderPipelineState>  m_renderPSO         = nil;
    id<MTLDepthStencilState>    m_depthStencilState = nil;
    MTLCullMode                 m_cullMode          = MTLCullModeNone;
    id<MTLComputePipelineState> m_computePSO        = nil;
};

// ─── MetalFence ──────────────────────────────────────────────────────────────

class MetalFence final : public IFence
{
public:
    MetalFence() : m_completed(false) {}
    ~MetalFence() override = default;

    void waitUntilCompleted() noexcept override
    {
        if (m_commandBuffer)
        {
            [m_commandBuffer waitUntilCompleted];
        }
    }

    [[nodiscard]] bool isCompleted() const noexcept override
    {
        if (!m_commandBuffer) { return true; }
        return m_commandBuffer.status == MTLCommandBufferStatusCompleted
            || m_commandBuffer.status == MTLCommandBufferStatusError;
    }

    void attachCommandBuffer(id<MTLCommandBuffer> cmdbuf)
    {
        m_commandBuffer = cmdbuf;
    }

private:
    id<MTLCommandBuffer> m_commandBuffer = nil;
    bool                 m_completed;
};

// ─── MetalAccelerationStructure ─────────────────────────────────────────────

class MetalAccelerationStructure final : public IAccelerationStructure
{
public:
    explicit MetalAccelerationStructure(id<MTLAccelerationStructure> as)
        : m_accelStruct(as) {}

    ~MetalAccelerationStructure() override = default;

    [[nodiscard]] void* nativeHandle() const noexcept override
    {
        return (__bridge void*)m_accelStruct;
    }

    [[nodiscard]] id<MTLAccelerationStructure> mtlAccelStruct() const noexcept
    {
        return m_accelStruct;
    }

    /// Swap the underlying Metal AS handle (used by rebuildAccelStruct).
    void replaceNative(id<MTLAccelerationStructure> as) noexcept
    {
        m_accelStruct = as;
    }

private:
    id<MTLAccelerationStructure> m_accelStruct;
};

// ─── MetalComputePassEncoder ──────────────────────────────────────────────────────────

class MetalComputePassEncoder final : public IComputePassEncoder
{
public:
    explicit MetalComputePassEncoder(id<MTLComputeCommandEncoder> enc)
        : m_encoder(enc) {}

    ~MetalComputePassEncoder() override = default;

    void setComputePipeline(IPipeline* pipeline) override
    {
        auto* mp = static_cast<MetalPipeline*>(pipeline);
        [m_encoder setComputePipelineState:mp->computePSO()];
        NSUInteger w = mp->computePSO().threadExecutionWidth;
        m_tgWidth  = (w > 0) ? w : 64;
        m_tgHeight = 8;
    }

    void setBuffer(IBuffer* buffer, u32 offset, u32 index) override
    {
        [m_encoder setBuffer:static_cast<MetalBuffer*>(buffer)->mtlBuffer()
                      offset:offset
                     atIndex:index];
    }

    void setTexture(ITexture* texture, u32 index) override
    {
        [m_encoder setTexture:static_cast<MetalTexture*>(texture)->mtlTexture()
                      atIndex:index];
    }

    void setTextures(std::span<ITexture* const> textures, u32 baseIndex) override
    {
        const NSUInteger count = static_cast<NSUInteger>(textures.size());
        if (count == 0) return;

        // Build a temporary C array of id<MTLTexture> for setTextures:withRange:.
        std::vector<id<MTLTexture>> mtlTextures(count);
        for (NSUInteger i = 0; i < count; ++i)
        {
            mtlTextures[i] = textures[i]
                ? static_cast<MetalTexture*>(textures[i])->mtlTexture()
                : nil;
        }

        [m_encoder setTextures:mtlTextures.data()
                     withRange:NSMakeRange(baseIndex, count)];
    }

    void setSampler(ISampler* sampler, u32 index) override
    {
        [m_encoder setSamplerState:static_cast<MetalSampler*>(sampler)->mtlSamplerState()
                           atIndex:index];
    }

    void setBytes(const void* data, u32 size, u32 index) override
    {
        [m_encoder setBytes:data length:size atIndex:index];
    }

    void dispatch(u32 threadCountX, u32 threadCountY, u32 threadCountZ) override
    {
        MTLSize threads = { threadCountX, threadCountY, threadCountZ };
        // Match threadgroup dimensionality to the dispatch:
        //   1D dispatch (Y==1, Z==1) → {width, 1, 1}  (kernels use uint gid)
        //   2D dispatch (Z==1)       → {width, height, 1} (kernels use uint2 gid)
        //   3D dispatch              → {4, 4, 4}  (64 threads/group, optimal for Apple Silicon)
        MTLSize tg;
        if (threadCountY == 1 && threadCountZ == 1)
            tg = { m_tgWidth, 1, 1 };
        else if (threadCountZ == 1)
            tg = { m_tgWidth, m_tgHeight, 1 };
        else
            tg = { 4, 4, 4 };
        [m_encoder dispatchThreads:threads threadsPerThreadgroup:tg];
    }

    void pushDebugGroup(std::string_view label) override
    {
        [m_encoder pushDebugGroup:[NSString stringWithUTF8String:std::string(label).c_str()]];
    }

    void popDebugGroup() override
    {
        [m_encoder popDebugGroup];
    }

    void end() override
    {
        [m_encoder endEncoding];
    }

    // ─── Acceleration structures ────────────────────────────────────────────

    void setAccelerationStructure(IAccelerationStructure* accel, u32 index) override
    {
        if (accel)
        {
            auto* ma = static_cast<MetalAccelerationStructure*>(accel);
            [m_encoder setAccelerationStructure:ma->mtlAccelStruct()
                                  atBufferIndex:index];
        }
    }

    // ─── Resource residency ──────────────────────────────────────────────────

    void useResource(IBuffer* buffer, ResourceUsage usage) override
    {
        if (!buffer) return;
        auto* mb = static_cast<MetalBuffer*>(buffer);
        MTLResourceUsage mtlUsage = 0;
        if (static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::Read))
            mtlUsage |= MTLResourceUsageRead;
        if (static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::Write))
            mtlUsage |= MTLResourceUsageWrite;
        [m_encoder useResource:mb->mtlBuffer() usage:mtlUsage];
    }

    void useResource(ITexture* texture, ResourceUsage usage) override
    {
        if (!texture) return;
        auto* mt = static_cast<MetalTexture*>(texture);
        MTLResourceUsage mtlUsage = 0;
        if (static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::Read))
            mtlUsage |= MTLResourceUsageRead;
        if (static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::Write))
            mtlUsage |= MTLResourceUsageWrite;
        [m_encoder useResource:mt->mtlTexture() usage:mtlUsage];
    }

    void useResource(IAccelerationStructure* accel, ResourceUsage usage) override
    {
        if (!accel) return;
        auto* ma = static_cast<MetalAccelerationStructure*>(accel);
        MTLResourceUsage mtlUsage = 0;
        if (static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::Read))
            mtlUsage |= MTLResourceUsageRead;
        if (static_cast<u32>(usage) & static_cast<u32>(ResourceUsage::Write))
            mtlUsage |= MTLResourceUsageWrite;
        [m_encoder useResource:ma->mtlAccelStruct() usage:mtlUsage];
    }

private:
    id<MTLComputeCommandEncoder> m_encoder;
    NSUInteger                   m_tgWidth  = 8;
    NSUInteger                   m_tgHeight = 8;
};

// ─── MetalRenderPassEncoder ───────────────────────────────────────────────────────────

class MetalRenderPassEncoder final : public IRenderPassEncoder
{
public:
    explicit MetalRenderPassEncoder(id<MTLRenderCommandEncoder> enc)
        : m_encoder(enc) {}

    ~MetalRenderPassEncoder() override = default;

    void setRenderPipeline(IPipeline* pipeline) override
    {
        auto* mp = static_cast<MetalPipeline*>(pipeline);
        [m_encoder setRenderPipelineState:mp->renderPSO()];
        if (mp->depthStencilState())
            [m_encoder setDepthStencilState:mp->depthStencilState()];
        // Generated world geometry (sector tessellator) uses CCW winding for front faces.
        // Metal defaults front-facing winding to clockwise, which culls floor/ceiling.
        [m_encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [m_encoder setCullMode:mp->cullMode()];
    }

    void setViewport(const Viewport& vp) override
    {
        [m_encoder setViewport:MTLViewport{
            .originX = vp.x,    .originY = vp.y,
            .width   = vp.width, .height  = vp.height,
            .znear   = vp.nearZ, .zfar    = vp.farZ
        }];
    }

    void setScissor(const ScissorRect& r) override
    {
        [m_encoder setScissorRect:MTLScissorRect{
            .x      = static_cast<NSUInteger>(r.x),
            .y      = static_cast<NSUInteger>(r.y),
            .width  = r.width,
            .height = r.height
        }];
    }

    void setVertexBuffer(IBuffer* buffer, u32 offset, u32 bindIndex) override
    {
        [m_encoder setVertexBuffer:static_cast<MetalBuffer*>(buffer)->mtlBuffer()
                            offset:offset
                           atIndex:bindIndex];
    }

    void setVertexTexture(ITexture* texture, u32 bindIndex) override
    {
        [m_encoder setVertexTexture:static_cast<MetalTexture*>(texture)->mtlTexture()
                            atIndex:bindIndex];
    }

    void setVertexSampler(ISampler* sampler, u32 bindIndex) override
    {
        [m_encoder setVertexSamplerState:static_cast<MetalSampler*>(sampler)->mtlSamplerState()
                                 atIndex:bindIndex];
    }

    void setIndexBuffer(IBuffer* buffer, u32 offset, bool use32Bit) override
    {
        m_indexBuffer = static_cast<MetalBuffer*>(buffer)->mtlBuffer();
        m_indexOffset = offset;
        m_indexType   = use32Bit ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
    }

    void setBindGroup(u32 /*index*/, IBindGroup* /*group*/) override
    {
        // Bind groups not used; resources bound individually.
    }

    void setFragmentBuffer(IBuffer* buffer, u32 offset, u32 bindIndex) override
    {
        [m_encoder setFragmentBuffer:static_cast<MetalBuffer*>(buffer)->mtlBuffer()
                              offset:offset
                             atIndex:bindIndex];
    }

    void setFragmentTexture(ITexture* texture, u32 bindIndex) override
    {
        [m_encoder setFragmentTexture:static_cast<MetalTexture*>(texture)->mtlTexture()
                              atIndex:bindIndex];
    }

    void setFragmentSampler(ISampler* sampler, u32 bindIndex) override
    {
        [m_encoder setFragmentSamplerState:static_cast<MetalSampler*>(sampler)->mtlSamplerState()
                                   atIndex:bindIndex];
    }

    void setVertexBytes(const void* data, u32 size, u32 bindIndex) override
    {
        [m_encoder setVertexBytes:data length:size atIndex:bindIndex];
    }

    void setFragmentBytes(const void* data, u32 size, u32 bindIndex) override
    {
        [m_encoder setFragmentBytes:data length:size atIndex:bindIndex];
    }

    void draw(u32 vertexCount, u32 instanceCount,
              u32 firstVertex, u32 firstInstance) override
    {
        [m_encoder drawPrimitives:MTLPrimitiveTypeTriangle
                      vertexStart:firstVertex
                      vertexCount:vertexCount
                    instanceCount:instanceCount
                     baseInstance:firstInstance];
    }

    void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex,
                     i32 vertexOffset, u32 firstInstance) override
    {
        DAEDALUS_ASSERT(m_indexBuffer != nil, "drawIndexed: index buffer not set");
        const u32 indexStride = (m_indexType == MTLIndexTypeUInt32) ? 4u : 2u;
        [m_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                              indexCount:indexCount
                               indexType:m_indexType
                             indexBuffer:m_indexBuffer
                       indexBufferOffset:m_indexOffset + firstIndex * indexStride
                           instanceCount:instanceCount
                              baseVertex:vertexOffset
                            baseInstance:firstInstance];
    }

    void drawIndirect(IBuffer* argBuffer, u32 argBufferOffset) override
    {
        id<MTLBuffer> mtlBuf = static_cast<MetalBuffer*>(argBuffer)->mtlBuffer();
        [m_encoder drawPrimitives:MTLPrimitiveTypeTriangle
                   indirectBuffer:mtlBuf
             indirectBufferOffset:static_cast<NSUInteger>(argBufferOffset)];
    }

    void end() override
    {
        [m_encoder endEncoding];
    }

private:
    id<MTLRenderCommandEncoder> m_encoder;
    id<MTLBuffer>               m_indexBuffer = nil;
    u32                         m_indexOffset = 0;
    MTLIndexType                m_indexType   = MTLIndexTypeUInt32;
};

// ─── MetalSwapchain ───────────────────────────────────────────────────────────

class MetalSwapchain final : public ISwapchain
{
public:
    MetalSwapchain(CAMetalLayer* layer, SDL_MetalView view,
                   u32 width, u32 height)
        : m_layer(layer)
        , m_view(view)
        , m_width(width)
        , m_height(height)
    {
        DAEDALUS_ASSERT(m_layer != nil, "MetalSwapchain: null CAMetalLayer");
    }

    ~MetalSwapchain() override
    {
        m_currentDrawable = nil;
        SDL_Metal_DestroyView(m_view);
    }

    [[nodiscard]] ITexture* nextDrawable() override
    {
        m_currentDrawable = [m_layer nextDrawable];
        DAEDALUS_ASSERT(m_currentDrawable != nil,
                        "MetalSwapchain::nextDrawable: failed to acquire drawable");
        m_drawableTexture = std::make_unique<MetalTexture>(
            m_currentDrawable.texture,
            TextureUsage::RenderTarget);
        return m_drawableTexture.get();
    }

    void present() override
    {
        // Actual presentation is driven by ICommandBuffer::present() which
        // calls presentDrawable on the command buffer.  This method is a no-op
        // in the Metal backend.
    }

    void resize(u32 width, u32 height) override
    {
        m_width  = width;
        m_height = height;
        m_layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width),
                                          static_cast<CGFloat>(height));
    }

    [[nodiscard]] u32          width()  const noexcept override { return m_width;  }
    [[nodiscard]] u32          height() const noexcept override { return m_height; }
    [[nodiscard]] TextureFormat format() const noexcept override
    {
        return fromMTL(m_layer.pixelFormat);
    }

    [[nodiscard]] id<CAMetalDrawable> currentDrawable() const noexcept
    {
        return m_currentDrawable;
    }
    [[nodiscard]] void* currentDrawableHandle() noexcept override
    {
        return (__bridge void*)m_currentDrawable;
    }

private:
    CAMetalLayer*                   m_layer;
    SDL_MetalView                   m_view;
    u32                             m_width;
    u32                             m_height;
    id<CAMetalDrawable>             m_currentDrawable = nil;
    std::unique_ptr<MetalTexture>   m_drawableTexture;
};

// ─── MetalCommandBuffer ───────────────────────────────────────────────────────

class MetalCommandBuffer final : public ICommandBuffer
{
public:
    explicit MetalCommandBuffer(id<MTLCommandBuffer> cmdbuf)
        : m_cmdBuf(cmdbuf) {}

    ~MetalCommandBuffer() override = default;

    [[nodiscard]] IRenderPassEncoder*
    beginRenderPass(const RenderPassDescriptor& desc) override
    {
        MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];

        for (u32 i = 0; i < desc.colorAttachmentCount; ++i)
        {
            const ColorAttachmentDescriptor& ca = desc.colorAttachments[i];
            DAEDALUS_ASSERT(ca.texture != nullptr,
                            "beginRenderPass: color attachment texture is null");

            auto* mt = static_cast<MetalTexture*>(ca.texture);
            rpd.colorAttachments[i].texture     = mt->mtlTexture();
            rpd.colorAttachments[i].loadAction  = toMTL(ca.loadAction);
            rpd.colorAttachments[i].storeAction = toMTL(ca.storeAction);
            rpd.colorAttachments[i].clearColor  = MTLClearColorMake(
                ca.clearColor.r, ca.clearColor.g,
                ca.clearColor.b, ca.clearColor.a);
        }

        if (desc.hasDepthAttachment && desc.depthAttachment.texture != nullptr)
        {
            auto* dt = static_cast<MetalTexture*>(desc.depthAttachment.texture);
            rpd.depthAttachment.texture     = dt->mtlTexture();
            rpd.depthAttachment.loadAction  = toMTL(desc.depthAttachment.loadAction);
            rpd.depthAttachment.storeAction = toMTL(desc.depthAttachment.storeAction);
            rpd.depthAttachment.clearDepth  = desc.depthAttachment.clearDepth;
        }

        if (!desc.debugLabel.empty())
        {
            rpd.renderTargetWidth  = 0; // Metal infers from attachments
            rpd.renderTargetHeight = 0;
        }

        id<MTLRenderCommandEncoder> enc =
            [m_cmdBuf renderCommandEncoderWithDescriptor:rpd];
        DAEDALUS_ASSERT(enc != nil, "beginRenderPass: failed to create encoder");

        if (!desc.debugLabel.empty())
        {
            enc.label = [NSString stringWithUTF8String:desc.debugLabel.c_str()];
        }

        m_renderEncoder = std::make_unique<MetalRenderPassEncoder>(enc);
        return m_renderEncoder.get();
    }

    [[nodiscard]] IComputePassEncoder*
    beginComputePass(std::string_view debugLabel = {}) override
    {
        id<MTLComputeCommandEncoder> enc = [m_cmdBuf computeCommandEncoder];
        DAEDALUS_ASSERT(enc != nil, "beginComputePass: failed to create encoder");
        if (!debugLabel.empty())
            enc.label = [NSString stringWithUTF8String:std::string(debugLabel).c_str()];
        m_computeEncoder = std::make_unique<MetalComputePassEncoder>(enc);
        return m_computeEncoder.get();
    }

    void copyBuffer(IBuffer* src, usize srcOffset,
                    IBuffer* dst, usize dstOffset, usize size) override
    {
        id<MTLBlitCommandEncoder> blit = [m_cmdBuf blitCommandEncoder];
        [blit copyFromBuffer:static_cast<MetalBuffer*>(src)->mtlBuffer()
                sourceOffset:srcOffset
                    toBuffer:static_cast<MetalBuffer*>(dst)->mtlBuffer()
           destinationOffset:dstOffset
                        size:size];
        [blit endEncoding];
    }

    void copyTexture(ITexture* src, ITexture* dst) override
    {
        id<MTLBlitCommandEncoder> blit = [m_cmdBuf blitCommandEncoder];
        [blit copyFromTexture:static_cast<MetalTexture*>(src)->mtlTexture()
                    toTexture:static_cast<MetalTexture*>(dst)->mtlTexture()];
        [blit endEncoding];
    }

    void present(ISwapchain& swapchain) override
    {
        // Use the escape hatch so MetalOffscreenSwapchain (returns nullptr) is handled
        // correctly without a static_cast that would crash on the wrong type.
        if (void* handle = swapchain.currentDrawableHandle())
        {
            [m_cmdBuf presentDrawable:(__bridge id<CAMetalDrawable>)handle];
        }
    }

    void commit() override
    {
        [m_cmdBuf commit];
    }

    void signalOnCompletion(IFence* fence) override
    {
        static_cast<MetalFence*>(fence)->attachCommandBuffer(m_cmdBuf);
    }

    void pushDebugGroup(std::string_view label) override
    {
        [m_cmdBuf pushDebugGroup:[NSString stringWithUTF8String:std::string(label).c_str()]];
    }

    void popDebugGroup() override
    {
        [m_cmdBuf popDebugGroup];
    }

private:
    id<MTLCommandBuffer>                     m_cmdBuf;
    std::unique_ptr<MetalRenderPassEncoder>  m_renderEncoder;
    std::unique_ptr<MetalComputePassEncoder> m_computeEncoder;
};

// ─── MetalCommandQueue ────────────────────────────────────────────────────────

class MetalCommandQueue final : public ICommandQueue
{
public:
    explicit MetalCommandQueue(id<MTLCommandQueue> queue) : m_queue(queue) {}
    ~MetalCommandQueue() override = default;

    [[nodiscard]] std::unique_ptr<ICommandBuffer>
    createCommandBuffer(std::string_view debugLabel) override
    {
        id<MTLCommandBuffer> cmdbuf = [m_queue commandBuffer];
        DAEDALUS_ASSERT(cmdbuf != nil, "createCommandBuffer: failed");

        if (!debugLabel.empty())
        {
            cmdbuf.label = [NSString stringWithUTF8String:
                            std::string(debugLabel).c_str()];
        }
        return std::make_unique<MetalCommandBuffer>(cmdbuf);
    }

    [[nodiscard]] void* nativeHandle() const noexcept override
    {
        return (__bridge void*)m_queue;
    }

private:
    id<MTLCommandQueue> m_queue;
};

// ─── MetalOffscreenSwapchain ──────────────────────────────────────────────────────────────────
// ISwapchain backed by a persistent MTLTexture (no CAMetalLayer / display sync).
// Used by the editor 3D viewport: FrameRenderer renders into it; ImGui displays it.

class MetalOffscreenSwapchain final : public ISwapchain
{
public:
    MetalOffscreenSwapchain(id<MTLDevice> device, u32 width, u32 height)
        : m_device(device)
    {
        createBacking(width, height);
    }

    ~MetalOffscreenSwapchain() override = default;

    [[nodiscard]] ITexture* nextDrawable() override
    {
        return m_texture.get();
    }

    void present() override {} // no display sync — intentional no-op

    void resize(u32 width, u32 height) override
    {
        if (m_texture && m_texture->width() == width && m_texture->height() == height)
            return;
        createBacking(width, height);
    }

    [[nodiscard]] u32          width()  const noexcept override { return m_texture ? m_texture->width()  : 0u; }
    [[nodiscard]] u32          height() const noexcept override { return m_texture ? m_texture->height() : 0u; }
    [[nodiscard]] TextureFormat format() const noexcept override { return TextureFormat::BGRA8Unorm; }

    /// Always nullptr — no CAMetalDrawable for an offscreen surface.
    [[nodiscard]] void* currentDrawableHandle() noexcept override { return nullptr; }

private:
    void createBacking(u32 width, u32 height)
    {
        DAEDALUS_ASSERT(width > 0 && height > 0, "MetalOffscreenSwapchain: degenerate size");
        MTLTextureDescriptor* d = [[MTLTextureDescriptor alloc] init];
        d.textureType = MTLTextureType2D;
        d.width       = width;
        d.height      = height;
        d.pixelFormat = MTLPixelFormatBGRA8Unorm;
        d.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModePrivate;
        id<MTLTexture> tex = [m_device newTextureWithDescriptor:d];
        DAEDALUS_ASSERT(tex != nil, "MetalOffscreenSwapchain: texture creation failed");
        tex.label = @"OffscreenViewport";
        m_texture = std::make_unique<MetalTexture>(
            tex, TextureUsage::RenderTarget | TextureUsage::ShaderRead);
    }

    id<MTLDevice>                 m_device;
    std::unique_ptr<MetalTexture> m_texture;
};

// ─── MetalRenderDevice ────────────────────────────────────────────────────────

class MetalRenderDevice final : public IRenderDevice
{
public:
    explicit MetalRenderDevice(id<MTLDevice> device)
        : m_device(device)
        , m_deviceName([device.name UTF8String])
    {}

    ~MetalRenderDevice() override = default;

    // ─── Queues ───────────────────────────────────────────────────────────────

    [[nodiscard]] std::unique_ptr<ICommandQueue>
    createCommandQueue(std::string_view debugName) override
    {
        id<MTLCommandQueue> queue = [m_device newCommandQueue];
        DAEDALUS_ASSERT(queue != nil, "createCommandQueue: failed");
        if (!debugName.empty())
        {
            queue.label = [NSString stringWithUTF8String:std::string(debugName).c_str()];
        }
        return std::make_unique<MetalCommandQueue>(queue);
    }

    // ─── Swapchain ────────────────────────────────────────────────────────────

    [[nodiscard]] std::unique_ptr<ISwapchain>
    createSwapchain(void* nativeWindowHandle, u32 width, u32 height) override
    {
        auto* sdlWindow = static_cast<SDL_Window*>(nativeWindowHandle);
        SDL_MetalView view = SDL_Metal_CreateView(sdlWindow);
        DAEDALUS_ASSERT(view != nullptr, "createSwapchain: SDL_Metal_CreateView failed");

        CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(view);
        DAEDALUS_ASSERT(layer != nil, "createSwapchain: CAMetalLayer is nil");

        layer.device                = m_device;
        layer.pixelFormat           = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly       = NO;  // allow shader reads from swapchain texture
        layer.drawableSize          = CGSizeMake(static_cast<CGFloat>(width),
                                                  static_cast<CGFloat>(height));
        // displaySyncEnabled = NO: prevents [CAMetalLayer nextDrawable] from blocking
        // on vsync.  With the default of YES the Window Server's cursor-compositing
        // work (active when the cursor is over the Metal window) can push frame time
        // past the vsync boundary, stalling nextDrawable for 16-32 ms.  During that
        // stall, mouse-motion events accumulate and fire all at once on the following
        // frame, producing the "stutter-when-cursor-is-over-the-window" artefact.
        // Decoupling from vsync releases drawables as soon as the GPU finishes;
        // the caller applies its own ~60 fps cap via SDL_DelayPrecise.
        layer.maximumDrawableCount  = 2;
        layer.displaySyncEnabled    = NO;

        return std::make_unique<MetalSwapchain>(layer, view, width, height);
    }

    // ─── Resources ────────────────────────────────────────────────────────────

    [[nodiscard]] std::unique_ptr<IBuffer>
    createBuffer(const BufferDescriptor& desc) override
    {
        DAEDALUS_ASSERT(desc.size > 0, "createBuffer: size must be > 0");

        MTLResourceOptions options = MTLResourceStorageModeShared;
        id<MTLBuffer> buf = [m_device newBufferWithLength:desc.size
                                                  options:options];
        DAEDALUS_ASSERT(buf != nil, "createBuffer: failed");

        if (desc.initData)
        {
            std::memcpy(buf.contents, desc.initData, desc.size);
        }
        if (!desc.debugName.empty())
        {
            buf.label = [NSString stringWithUTF8String:desc.debugName.c_str()];
        }

        return std::make_unique<MetalBuffer>(buf, desc.size, desc.usage);
    }

    [[nodiscard]] std::unique_ptr<ITexture>
    createTexture(const TextureDescriptor& desc) override
    {
        MTLTextureDescriptor* mtlDesc = [[MTLTextureDescriptor alloc] init];
        mtlDesc.textureType = (desc.depth > 1) ? MTLTextureType3D : MTLTextureType2D;
        mtlDesc.width       = desc.width;
        mtlDesc.height      = desc.height;
        mtlDesc.depth       = desc.depth;
        mtlDesc.mipmapLevelCount = desc.mipLevels;
        mtlDesc.arrayLength = desc.arrayLayers;
        mtlDesc.pixelFormat = toMTL(desc.format);

        MTLTextureUsage usage = 0;
        if (hasFlag(desc.usage, TextureUsage::ShaderRead))  usage |= MTLTextureUsageShaderRead;
        if (hasFlag(desc.usage, TextureUsage::ShaderWrite)) usage |= MTLTextureUsageShaderWrite;
        if (hasFlag(desc.usage, TextureUsage::RenderTarget))usage |= MTLTextureUsageRenderTarget;
        if (hasFlag(desc.usage, TextureUsage::DepthStencil))usage |= MTLTextureUsageRenderTarget;
        mtlDesc.usage = usage;
        // Textures with CPU init data use shared storage so replaceRegion can upload.
        mtlDesc.storageMode = (desc.initData != nullptr)
            ? MTLStorageModeShared : MTLStorageModePrivate;

        id<MTLTexture> tex = [m_device newTextureWithDescriptor:mtlDesc];
        DAEDALUS_ASSERT(tex != nil, "createTexture: failed");

        if (desc.initData)
        {
            const u32 bytesPerRow = desc.width * formatBytesPerPixel(desc.format);
            if (desc.depth > 1)
            {
                // 3D texture: MTLRegionMake3D covers all depth slices.
                // bytesPerImage is the stride between consecutive depth slices.
                const u32 bytesPerImage = bytesPerRow * desc.height;
                [tex replaceRegion:MTLRegionMake3D(0, 0, 0, desc.width, desc.height, desc.depth)
                       mipmapLevel:0
                             slice:0
                         withBytes:desc.initData
                       bytesPerRow:bytesPerRow
                     bytesPerImage:bytesPerImage];
            }
            else
            {
                [tex replaceRegion:MTLRegionMake2D(0, 0, desc.width, desc.height)
                       mipmapLevel:0
                         withBytes:desc.initData
                       bytesPerRow:bytesPerRow];
            }
        }

        if (!desc.debugName.empty())
        {
            tex.label = [NSString stringWithUTF8String:desc.debugName.c_str()];
        }

        return std::make_unique<MetalTexture>(tex, desc.usage);
    }

    [[nodiscard]] std::unique_ptr<ISampler>
    createSampler(const SamplerDescriptor& desc) override
    {
        MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
        sd.magFilter    = toMTLMinMag(desc.magFilter);
        sd.minFilter    = toMTLMinMag(desc.minFilter);
        sd.mipFilter    = desc.mipFilter == SamplerDescriptor::Filter::Linear
                          ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
        sd.sAddressMode = toMTL(desc.addressU);
        sd.tAddressMode = toMTL(desc.addressV);
        sd.rAddressMode = toMTL(desc.addressW);
        sd.maxAnisotropy = static_cast<NSUInteger>(desc.maxAnisotropy);

        id<MTLSamplerState> state = [m_device newSamplerStateWithDescriptor:sd];
        DAEDALUS_ASSERT(state != nil, "createSampler: failed");

        return std::make_unique<MetalSampler>(state, desc);
    }

    // ─── Shaders ──────────────────────────────────────────────────────────────

    [[nodiscard]] std::unique_ptr<IShader>
    createShader(std::span<const byte> source,
                 ShaderStage           stage,
                 std::string_view      entryPoint) override
    {
        // Source is MSL text.
        NSString* src = [[NSString alloc]
            initWithBytes:source.data()
                   length:source.size()
                 encoding:NSUTF8StringEncoding];

        NSError* error = nil;
        id<MTLLibrary> lib = [m_device newLibraryWithSource:src
                                                    options:nil
                                                      error:&error];
        DAEDALUS_ASSERT(lib != nil,
            error ? [[error localizedDescription] UTF8String]
                  : "createShader: compilation failed");

        NSString* name = [NSString stringWithUTF8String:std::string(entryPoint).c_str()];
        id<MTLFunction> func = [lib newFunctionWithName:name];
        DAEDALUS_ASSERT(func != nil, "createShader: entry point not found in MSL library");

        return std::make_unique<MetalShader>(func, stage, std::string(entryPoint));
    }

    [[nodiscard]] std::unique_ptr<IShader>
    createShaderFromLibrary(std::string_view libraryPath,
                            ShaderStage      stage,
                            std::string_view entryPoint) override
    {
        std::string pathStr(libraryPath);
        id<MTLLibrary> lib = nil;
        auto it = m_libraryCache.find(pathStr);
        if (it != m_libraryCache.end())
        {
            lib = it->second;
        }
        else
        {
            NSURL* url = [NSURL fileURLWithPath:
                [NSString stringWithUTF8String:pathStr.c_str()]];
            NSError* error = nil;
            lib = [m_device newLibraryWithURL:url error:&error];
            DAEDALUS_ASSERT(lib != nil,
                error ? [[error localizedDescription] UTF8String]
                      : "createShaderFromLibrary: failed to load .metallib");
            m_libraryCache[pathStr] = lib;
        }
        NSString* name = [NSString stringWithUTF8String:std::string(entryPoint).c_str()];
        id<MTLFunction> func = [lib newFunctionWithName:name];
        DAEDALUS_ASSERT(func != nil, "createShaderFromLibrary: entry point not found");
        return std::make_unique<MetalShader>(func, stage, std::string(entryPoint));
    }

    // ─── Pipelines ────────────────────────────────────────────────────────────

    [[nodiscard]] std::unique_ptr<IPipeline>
    createRenderPipeline(const RenderPipelineDescriptor& desc) override
    {
        MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];

        if (desc.vertexShader)
            pd.vertexFunction   = static_cast<MetalShader*>(desc.vertexShader)->mtlFunction();
        if (desc.fragmentShader)
            pd.fragmentFunction = static_cast<MetalShader*>(desc.fragmentShader)->mtlFunction();

        // ─── Vertex descriptor ────────────────────────────────────────────────
        if (!desc.vertexAttributes.empty())
        {
            MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
            for (const auto& attr : desc.vertexAttributes)
            {
                vd.attributes[attr.location].format      = toMTL(attr.format);
                vd.attributes[attr.location].offset      = attr.offset;
                vd.attributes[attr.location].bufferIndex = attr.bufferIndex;
            }
            for (u32 i = 0; i < static_cast<u32>(desc.vertexBufferLayouts.size()); ++i)
            {
                const u32 bufIdx = desc.vertexBufferLayouts[i].bufferIndex;
                vd.layouts[bufIdx].stride       = desc.vertexBufferLayouts[i].stride;
                vd.layouts[bufIdx].stepFunction = MTLVertexStepFunctionPerVertex;
            }
            pd.vertexDescriptor = vd;
        }

        // ─── Color attachments + blend ────────────────────────────────────────
        for (u32 i = 0; i < desc.colorAttachmentCount; ++i)
        {
            pd.colorAttachments[i].pixelFormat = toMTL(desc.colorFormats[i]);
            const auto& bs = desc.blendStates[i];
            if (bs.blendEnabled)
            {
                pd.colorAttachments[i].blendingEnabled             = YES;
                pd.colorAttachments[i].sourceRGBBlendFactor        = toMTL(bs.srcRGB);
                pd.colorAttachments[i].destinationRGBBlendFactor   = toMTL(bs.dstRGB);
                pd.colorAttachments[i].rgbBlendOperation           = toMTL(bs.rgbOp);
                pd.colorAttachments[i].sourceAlphaBlendFactor      = toMTL(bs.srcAlpha);
                pd.colorAttachments[i].destinationAlphaBlendFactor = toMTL(bs.dstAlpha);
                pd.colorAttachments[i].alphaBlendOperation         = toMTL(bs.alphaOp);
            }
        }

        // ─── Depth attachment ─────────────────────────────────────────────────
        if (desc.depthFormat != TextureFormat::Invalid)
            pd.depthAttachmentPixelFormat = toMTL(desc.depthFormat);

        if (!desc.debugName.empty())
            pd.label = [NSString stringWithUTF8String:desc.debugName.c_str()];

        NSError* error = nil;
        id<MTLRenderPipelineState> pso =
            [m_device newRenderPipelineStateWithDescriptor:pd error:&error];
        DAEDALUS_ASSERT(pso != nil,
            error ? [[error localizedDescription] UTF8String]
                  : "createRenderPipeline: failed");

        // ─── Depth stencil state ──────────────────────────────────────────────
        id<MTLDepthStencilState> dss = nil;
        if (desc.depthTest || desc.depthWrite)
        {
            MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
            dsd.depthCompareFunction = desc.depthTest
                ? toMTL(desc.depthCompare) : MTLCompareFunctionAlways;
            dsd.depthWriteEnabled    = desc.depthWrite ? YES : NO;
            dss = [m_device newDepthStencilStateWithDescriptor:dsd];
        }

        return std::make_unique<MetalPipeline>(pso, dss, toMTL(desc.cullMode));
    }

    [[nodiscard]] std::unique_ptr<IPipeline>
    createComputePipeline(const ComputePipelineDescriptor& desc) override
    {
        DAEDALUS_ASSERT(desc.computeShader != nullptr,
                        "createComputePipeline: null compute shader");

        NSError* error = nil;
        id<MTLComputePipelineState> pso = [m_device
            newComputePipelineStateWithFunction:
                static_cast<MetalShader*>(desc.computeShader)->mtlFunction()
            error:&error];
        DAEDALUS_ASSERT(pso != nil,
            error ? [[error localizedDescription] UTF8String]
                  : "createComputePipeline: failed");

        return std::make_unique<MetalPipeline>(pso);
    }

    // ─── Synchronisation ──────────────────────────────────────────────────────

    [[nodiscard]] std::unique_ptr<IFence>
    createFence() override
    {
        return std::make_unique<MetalFence>();
    }

    // ─── Diagnostics ──────────────────────────────────────────────────────────
    [[nodiscard]] std::string_view deviceName() const noexcept override
    {
        return m_deviceName;
    }

    [[nodiscard]] void* nativeDevice() const noexcept override
    {
        return (__bridge void*)m_device;
    }

    [[nodiscard]] std::unique_ptr<ISwapchain>
    createOffscreenSwapchain(u32 width, u32 height) override
    {
        return std::make_unique<MetalOffscreenSwapchain>(m_device, width, height);
    }

    // ─── Acceleration structures ────────────────────────────────────────────

    [[nodiscard]] std::unique_ptr<IAccelerationStructure>
    createPrimitiveAccelStruct(std::span<const AccelStructGeometryDesc> geometries) override
    {
        if (![m_device supportsRaytracing] || geometries.empty()) return nullptr;

        NSMutableArray<MTLAccelerationStructureTriangleGeometryDescriptor*>* geoArray =
            [NSMutableArray arrayWithCapacity:geometries.size()];

        for (const auto& geo : geometries)
        {
            auto* td = [[MTLAccelerationStructureTriangleGeometryDescriptor alloc] init];
            td.vertexBuffer  = static_cast<MetalBuffer*>(geo.vertexBuffer)->mtlBuffer();
            td.vertexBufferOffset = 0;
            td.vertexStride  = geo.vertexStride;
            td.vertexFormat  = MTLAttributeFormatFloat3;
            if (geo.indexBuffer)
            {
                td.indexBuffer = static_cast<MetalBuffer*>(geo.indexBuffer)->mtlBuffer();
                td.indexBufferOffset = 0;
                td.indexType   = MTLIndexTypeUInt32;
                td.triangleCount = geo.indexCount / 3;
            }
            else
            {
                td.triangleCount = geo.vertexCount / 3;
            }
            [geoArray addObject:td];
        }

        auto* desc = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
        desc.geometryDescriptors = geoArray;
        return buildAccelStructSync(desc);
    }

    [[nodiscard]] std::unique_ptr<IAccelerationStructure>
    createInstanceAccelStruct(std::span<const AccelStructInstanceDesc> instances) override
    {
        if (![m_device supportsRaytracing] || instances.empty()) return nullptr;
        return buildTLASSync(instances);
    }

    void rebuildAccelStruct(
        IAccelerationStructure& accel,
        std::span<const AccelStructGeometryDesc> geometries,
        std::span<const AccelStructInstanceDesc> instances,
        AccelStructBuildMode mode) override
    {
        if (![m_device supportsRaytracing]) return;
        auto& ma = static_cast<MetalAccelerationStructure&>(accel);

        if (!instances.empty())
        {
            // TLAS rebuild — always full rebuild for correctness.
            auto fresh = buildTLASSync(instances);
            if (fresh)
                ma.replaceNative(static_cast<MetalAccelerationStructure*>(fresh.get())->mtlAccelStruct());
        }
        else if (!geometries.empty())
        {
            // BLAS rebuild.
            NSMutableArray<MTLAccelerationStructureTriangleGeometryDescriptor*>* geoArray =
                [NSMutableArray arrayWithCapacity:geometries.size()];
            for (const auto& geo : geometries)
            {
                auto* td = [[MTLAccelerationStructureTriangleGeometryDescriptor alloc] init];
                td.vertexBuffer  = static_cast<MetalBuffer*>(geo.vertexBuffer)->mtlBuffer();
                td.vertexBufferOffset = 0;
                td.vertexStride  = geo.vertexStride;
                td.vertexFormat  = MTLAttributeFormatFloat3;
                if (geo.indexBuffer)
                {
                    td.indexBuffer = static_cast<MetalBuffer*>(geo.indexBuffer)->mtlBuffer();
                    td.indexBufferOffset = 0;
                    td.indexType   = MTLIndexTypeUInt32;
                    td.triangleCount = geo.indexCount / 3;
                }
                else
                {
                    td.triangleCount = geo.vertexCount / 3;
                }
                [geoArray addObject:td];
            }
            auto* desc = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
            desc.geometryDescriptors = geoArray;

            MTLAccelerationStructureSizes sizes =
                [m_device accelerationStructureSizesWithDescriptor:desc];

            if (mode == AccelStructBuildMode::Refit)
            {
                id<MTLBuffer> scratch = [m_device newBufferWithLength:sizes.refitScratchBufferSize
                                                              options:MTLResourceStorageModePrivate];
                id<MTLCommandBuffer> cmdbuf = [asBuildQueue() commandBuffer];
                id<MTLAccelerationStructureCommandEncoder> enc =
                    [cmdbuf accelerationStructureCommandEncoder];
                [enc refitAccelerationStructure:ma.mtlAccelStruct()
                                     descriptor:desc
                                    destination:ma.mtlAccelStruct()
                                  scratchBuffer:scratch
                            scratchBufferOffset:0];
                [enc endEncoding];
                [cmdbuf commit];
                [cmdbuf waitUntilCompleted];
            }
            else
            {
                auto fresh = buildAccelStructSync(desc);
                if (fresh)
                    ma.replaceNative(
                        static_cast<MetalAccelerationStructure*>(fresh.get())->mtlAccelStruct());
            }
        }
    }

    [[nodiscard]] bool supportsRayTracing() const noexcept override
    {
        return [m_device supportsRaytracing];
    }

private:
    /// Lazy-init command queue for synchronous AS builds.
    id<MTLCommandQueue> asBuildQueue()
    {
        if (!m_asBuildQueue)
            m_asBuildQueue = [m_device newCommandQueue];
        return m_asBuildQueue;
    }

    /// Synchronously build an acceleration structure from a descriptor.
    std::unique_ptr<IAccelerationStructure> buildAccelStructSync(
        MTLAccelerationStructureDescriptor* desc)
    {
        MTLAccelerationStructureSizes sizes =
            [m_device accelerationStructureSizesWithDescriptor:desc];

        id<MTLAccelerationStructure> as =
            [m_device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
        DAEDALUS_ASSERT(as != nil, "buildAccelStructSync: AS allocation failed");

        id<MTLBuffer> scratch =
            [m_device newBufferWithLength:sizes.buildScratchBufferSize
                                  options:MTLResourceStorageModePrivate];
        DAEDALUS_ASSERT(scratch != nil, "buildAccelStructSync: scratch allocation failed");

        id<MTLCommandBuffer> cmdbuf = [asBuildQueue() commandBuffer];
        id<MTLAccelerationStructureCommandEncoder> enc =
            [cmdbuf accelerationStructureCommandEncoder];
        [enc buildAccelerationStructure:as
                             descriptor:desc
                          scratchBuffer:scratch
                    scratchBufferOffset:0];
        [enc endEncoding];
        [cmdbuf commit];
        [cmdbuf waitUntilCompleted];

        return std::make_unique<MetalAccelerationStructure>(as);
    }

    /// Build a TLAS from instance descriptors (synchronous).
    std::unique_ptr<IAccelerationStructure> buildTLASSync(
        std::span<const AccelStructInstanceDesc> instances)
    {
        const NSUInteger count = instances.size();
        const NSUInteger bufSize = count * sizeof(MTLAccelerationStructureInstanceDescriptor);
        id<MTLBuffer> instanceBuf =
            [m_device newBufferWithLength:bufSize options:MTLResourceStorageModeShared];
        DAEDALUS_ASSERT(instanceBuf != nil, "buildTLASSync: instance buffer alloc failed");

        auto* descs = static_cast<MTLAccelerationStructureInstanceDescriptor*>(
            instanceBuf.contents);

        // Collect unique BLAS references for instancedAccelerationStructures array.
        NSMutableArray<id<MTLAccelerationStructure>>* blasArray = [NSMutableArray array];
        std::unordered_map<void*, uint32_t> blasIndexMap;

        for (const auto& inst : instances)
        {
            auto* ma = static_cast<MetalAccelerationStructure*>(inst.blas);
            void* key = (__bridge void*)ma->mtlAccelStruct();
            if (blasIndexMap.find(key) == blasIndexMap.end())
            {
                blasIndexMap[key] = static_cast<uint32_t>([blasArray count]);
                [blasArray addObject:ma->mtlAccelStruct()];
            }
        }

        for (NSUInteger i = 0; i < count; ++i)
        {
            const auto& inst = instances[i];
            auto& d = descs[i];
            std::memset(&d, 0, sizeof(d));

            auto* ma = static_cast<MetalAccelerationStructure*>(inst.blas);
            void* key = (__bridge void*)ma->mtlAccelStruct();
            d.accelerationStructureIndex = blasIndexMap[key];
            d.mask = inst.mask;
            d.intersectionFunctionTableOffset = 0;

            // Column-major 4×4 → Metal MTLPackedFloat4x3 (4 columns of float3).
            // Drop the w component from each column.
            const float* m = inst.transform;
            d.transformationMatrix.columns[0] = MTLPackedFloat3{m[0],  m[1],  m[2]};
            d.transformationMatrix.columns[1] = MTLPackedFloat3{m[4],  m[5],  m[6]};
            d.transformationMatrix.columns[2] = MTLPackedFloat3{m[8],  m[9],  m[10]};
            d.transformationMatrix.columns[3] = MTLPackedFloat3{m[12], m[13], m[14]};
        }

        auto* desc = [MTLInstanceAccelerationStructureDescriptor descriptor];
        desc.instanceDescriptorBuffer        = instanceBuf;
        desc.instanceCount                   = count;
        desc.instancedAccelerationStructures = blasArray;

        return buildAccelStructSync(desc);
    }

    id<MTLDevice>                                   m_device;
    std::string                                     m_deviceName;
    std::unordered_map<std::string, id<MTLLibrary>> m_libraryCache;
    id<MTLCommandQueue>                             m_asBuildQueue = nil;
};

// ─── Factory ─────────────────────────────────────────────────────────────────

std::unique_ptr<IRenderDevice> createMetalRenderDevice()
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    DAEDALUS_ASSERT(device != nil,
                    "createMetalRenderDevice: no Metal-capable device found");
    return std::make_unique<MetalRenderDevice>(device);
}

} // namespace daedalus::rhi
