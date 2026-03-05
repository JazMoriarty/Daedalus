// frame_renderer.cpp

#include "daedalus/render/frame_renderer.h"
#include "daedalus/render/scene_data.h"
#include "daedalus/render/vertex_types.h"
#include "daedalus/render/rhi/i_command_buffer.h"
#include "daedalus/render/rhi/i_render_pass_encoder.h"
#include "daedalus/render/rhi/i_compute_pass_encoder.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace daedalus::render
{

using namespace rhi;

// GPU buffer slot used for vertex data (avoids colliding with constant-buffer slots 0–1).
static constexpr u32 k_vboSlot = 30;

// ─── TAA: 8-sample Halton(base-2 × base-3) jitter ────────────────────────────
// Values pre-shifted to [-0.5, 0.5] in pixel space.

static constexpr glm::vec2 k_halton[8] =
{
    { 0.0f,          -1.0f/6.0f   },
    {-0.25f,          1.0f/6.0f   },
    { 0.25f,         -7.0f/18.0f  },
    {-0.375f,        -1.0f/18.0f  },
    { 0.125f,         5.0f/18.0f  },
    {-0.125f,        -5.0f/18.0f  },
    { 0.375f,         1.0f/18.0f  },
    {-0.4375f,        7.0f/18.0f  },
};

glm::vec2 FrameRenderer::haltonJitter(u32 frameIndex) noexcept
{
    return k_halton[frameIndex % 8];
}

// ─── Vertex layout helpers ────────────────────────────────────────────────────

static std::vector<VertexAttributeDescriptor> geometryAttributes()
{
    // All attributes source from buffer slot k_vboSlot (avoids constant-buffer conflict).
    // Offsets match StaticMeshVertex layout (stride = 48 bytes).
    return {
        { 0, VertexFormat::Float3,  offsetof(StaticMeshVertex, pos),     k_vboSlot },
        { 1, VertexFormat::Float3,  offsetof(StaticMeshVertex, normal),  k_vboSlot },
        { 2, VertexFormat::Float2,  offsetof(StaticMeshVertex, uv),      k_vboSlot },
        { 3, VertexFormat::Float4,  offsetof(StaticMeshVertex, tangent), k_vboSlot },
    };
}

static std::vector<VertexBufferLayoutDescriptor> geometryLayouts()
{
    return { { sizeof(StaticMeshVertex), k_vboSlot } };
}

// ─── initialize ──────────────────────────────────────────────────────────────

void FrameRenderer::initialize(IRenderDevice&     device,
                               const std::string& shaderLibPath,
                               u32                width,
                               u32                height)
{
    m_width  = width;
    m_height = height;

    createPSOs(device, shaderLibPath);
    createPersistentResources(device, width, height);
}

// ─── createPSOs ──────────────────────────────────────────────────────────────

void FrameRenderer::createPSOs(IRenderDevice& device, const std::string& lib)
{
    auto loadVS = [&](const char* e){ return device.createShaderFromLibrary(lib, ShaderStage::Vertex,   e); };
    auto loadFS = [&](const char* e){ return device.createShaderFromLibrary(lib, ShaderStage::Fragment, e); };
    auto loadCS = [&](const char* e){ return device.createShaderFromLibrary(lib, ShaderStage::Compute,  e); };

    auto vsGBuffer       = loadVS("gbuffer_vert");
    auto fsGBuffer       = loadFS("gbuffer_frag");
    auto vsSkybox        = loadVS("skybox_vert");
    auto fsSkybox        = loadFS("skybox_frag");
    auto vsTAA           = loadVS("taa_vert");
    auto fsTAA           = loadFS("taa_frag");
    auto vsBloom         = loadVS("bloom_vert");
    auto fsBloomExtract  = loadFS("bloom_extract_frag");
    auto fsBloomBlurH    = loadFS("bloom_blur_h_frag");
    auto fsBloomBlurV    = loadFS("bloom_blur_v_frag");
    auto vsTonemap       = loadVS("tonemap_vert");
    auto fsTonemap       = loadFS("tonemap_frag");
    auto csSSAO          = loadCS("ssao_main");
    auto csSSAOBlur      = loadCS("ssao_blur");
    auto csLighting      = loadCS("lighting_main");

    // ── G-buffer (4 colour targets + depth write — spec §Pass 4 layout) ────────────────
    // Depth prepass is intentionally omitted: writing depth here guarantees
    // every visible pixel gets a motion vector, which TAA reprojection requires.
    {
        RenderPipelineDescriptor d;
        d.vertexShader         = vsGBuffer.get();
        d.fragmentShader       = fsGBuffer.get();
        d.colorAttachmentCount = 4;
        d.colorFormats[0]      = TextureFormat::RGBA8Unorm;   // RT0: albedo + baked AO
        d.colorFormats[1]      = TextureFormat::RGBA8Unorm;   // RT1: oct normal + roughness + metalness
        d.colorFormats[2]      = TextureFormat::RGBA16Float;  // RT2: emissive
        d.colorFormats[3]      = TextureFormat::RG16Float;    // RT3: motion vectors
        d.depthFormat          = TextureFormat::Depth32Float;
        d.depthTest            = true;
        d.depthWrite           = true;
        d.depthCompare         = CompareFunction::Less;
        d.cullMode             = CullMode::Back;
        d.vertexAttributes     = geometryAttributes();
        d.vertexBufferLayouts  = geometryLayouts();
        d.debugName            = "GBuffer";
        m_gbufferPSO = device.createRenderPipeline(d);
    }

    // ── Shadow depth (depth-only, no fragment shader) ─────────────────────────
    {
        auto vsShadow = loadVS("shadow_depth_vert");
        RenderPipelineDescriptor d;
        d.vertexShader         = vsShadow.get();
        d.fragmentShader       = nullptr;                // depth-only: no FS needed
        d.colorAttachmentCount = 0;
        d.depthFormat          = TextureFormat::Depth32Float;
        d.depthTest            = true;
        d.depthWrite           = true;
        d.depthCompare         = CompareFunction::Less;
        d.cullMode             = CullMode::Back;         // cull back-faces so ceiling doesn't shadow the interior
        d.vertexAttributes     = geometryAttributes();
        d.vertexBufferLayouts  = geometryLayouts();
        d.debugName            = "ShadowDepth";
        m_shadowDepthPSO = device.createRenderPipeline(d);
    }

    // ── SSAO compute
    {
        ComputePipelineDescriptor d;
        d.computeShader = csSSAO.get();
        d.debugName     = "SSAO";
        m_ssaoPSO = device.createComputePipeline(d);
    }

    // ── SSAO bilateral blur compute
    {
        ComputePipelineDescriptor d;
        d.computeShader = csSSAOBlur.get();
        d.debugName     = "SSAOBlur";
        m_ssaoBlurPSO = device.createComputePipeline(d);
    }

    // ── Lighting compute
    {
        ComputePipelineDescriptor d;
        d.computeShader = csLighting.get();
        d.debugName     = "Lighting";
        m_lightingPSO = device.createComputePipeline(d);
    }

    // ── Skybox (procedural sky, depth LessEqual load) ─────────────────────────
    {
        RenderPipelineDescriptor d;
        d.vertexShader         = vsSkybox.get();
        d.fragmentShader       = fsSkybox.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::RGBA16Float;
        d.depthFormat          = TextureFormat::Depth32Float;
        d.depthTest            = true;
        d.depthWrite           = false;
        d.depthCompare         = CompareFunction::LessEqual;  // passes where depth==1 (sky)
        d.cullMode             = CullMode::None;              // fullscreen triangle
        d.debugName            = "Skybox";
        m_skyboxPSO = device.createRenderPipeline(d);
    }

    // ── TAA ───────────────────────────────────────────────────────────────────
    {
        RenderPipelineDescriptor d;
        d.vertexShader         = vsTAA.get();
        d.fragmentShader       = fsTAA.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::RGBA16Float;
        d.cullMode             = CullMode::None;
        d.debugName            = "TAA";
        m_taaPSO = device.createRenderPipeline(d);
    }

    // ── Bloom extract ─────────────────────────────────────────────────────────
    {
        RenderPipelineDescriptor d;
        d.vertexShader         = vsBloom.get();
        d.fragmentShader       = fsBloomExtract.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::RGBA16Float;
        d.cullMode             = CullMode::None;
        d.debugName            = "BloomExtract";
        m_bloomExtractPSO = device.createRenderPipeline(d);
    }

    // ── Bloom blur H ──────────────────────────────────────────────────────────
    {
        RenderPipelineDescriptor d;
        d.vertexShader         = vsBloom.get();
        d.fragmentShader       = fsBloomBlurH.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::RGBA16Float;
        d.cullMode             = CullMode::None;
        d.debugName            = "BloomBlurH";
        m_bloomBlurHPSO = device.createRenderPipeline(d);
    }

    // ── Bloom blur V ──────────────────────────────────────────────────────────
    {
        RenderPipelineDescriptor d;
        d.vertexShader         = vsBloom.get();
        d.fragmentShader       = fsBloomBlurV.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::RGBA16Float;
        d.cullMode             = CullMode::None;
        d.debugName            = "BloomBlurV";
        m_bloomBlurVPSO = device.createRenderPipeline(d);
    }

    // ── Tone mapping → swapchain ──────────────────────────────────────────────
    {
        RenderPipelineDescriptor d;
        d.vertexShader         = vsTonemap.get();
        d.fragmentShader       = fsTonemap.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::BGRA8Unorm;  // swapchain pixel format
        d.cullMode             = CullMode::None;
        d.debugName            = "ToneMapping";
        m_tonemapPSO = device.createRenderPipeline(d);
    }
}

// ─── createPersistentResources ───────────────────────────────────────────────────────────

void FrameRenderer::createPersistentResources(IRenderDevice& device, u32 w, u32 h)
{
    // Frame constant buffer (updated every frame via map/unmap)
    {
        BufferDescriptor d;
        d.size      = sizeof(FrameGPU);
        d.usage     = BufferUsage::Uniform;
        d.debugName = "FrameConstants";
        m_frameConstBuf = device.createBuffer(d);
    }

    // Light buffer: 16-byte header (u32 count + 3× pad) + up to 64 lights
    {
        constexpr u32 kMaxLights = 64;
        BufferDescriptor d;
        d.size      = 16 + sizeof(PointLightGPU) * kMaxLights;
        d.usage     = BufferUsage::Storage | BufferUsage::Uniform;
        d.debugName = "LightBuffer";
        m_lightBuf = device.createBuffer(d);
    }

    // Linear-clamp sampler (used by TAA, bloom, tone mapping)
    {
        SamplerDescriptor d;
        d.magFilter  = SamplerDescriptor::Filter::Linear;
        d.minFilter  = SamplerDescriptor::Filter::Linear;
        d.mipFilter  = SamplerDescriptor::Filter::Linear;
        d.addressU   = SamplerDescriptor::AddressMode::ClampToEdge;
        d.addressV   = SamplerDescriptor::AddressMode::ClampToEdge;
        d.addressW   = SamplerDescriptor::AddressMode::ClampToEdge;
        d.debugName  = "LinearClamp";
        m_linearClampSampler = device.createSampler(d);
    }

    // Linear-repeat sampler (used by G-buffer material texture sampling)
    {
        SamplerDescriptor d;
        d.magFilter  = SamplerDescriptor::Filter::Linear;
        d.minFilter  = SamplerDescriptor::Filter::Linear;
        d.mipFilter  = SamplerDescriptor::Filter::Linear;
        d.addressU   = SamplerDescriptor::AddressMode::Repeat;
        d.addressV   = SamplerDescriptor::AddressMode::Repeat;
        d.addressW   = SamplerDescriptor::AddressMode::Repeat;
        d.debugName  = "LinearRepeat";
        m_linearRepeatSampler = device.createSampler(d);
    }

    // Fallback 1×1 textures — bound when a MeshDraw material slot is nullptr.
    {
        // White albedo (RGBA 255,255,255,255)
        const u8 whitePixel[4] = { 255, 255, 255, 255 };
        TextureDescriptor d;
        d.width = 1; d.height = 1;
        d.format    = TextureFormat::RGBA8Unorm;
        d.usage     = TextureUsage::ShaderRead;
        d.initData  = whitePixel;
        d.debugName = "FallbackAlbedo";
        m_fallbackAlbedo = device.createTexture(d);
    }
    {
        // Flat tangent-space normal: (0.5, 0.5, 1.0) → (128, 128, 255, 255) in RGBA8.
        const u8 flatNormal[4] = { 128, 128, 255, 255 };
        TextureDescriptor d;
        d.width = 1; d.height = 1;
        d.format    = TextureFormat::RGBA8Unorm;
        d.usage     = TextureUsage::ShaderRead;
        d.initData  = flatNormal;
        d.debugName = "FallbackNormal";
        m_fallbackNormal = device.createTexture(d);
    }
    {
        // Black emissive (no emission)
        const u8 blackPixel[4] = { 0, 0, 0, 0 };
        TextureDescriptor d;
        d.width = 1; d.height = 1;
        d.format    = TextureFormat::RGBA8Unorm;
        d.usage     = TextureUsage::ShaderRead;
        d.initData  = blackPixel;
        d.debugName = "FallbackEmissive";
        m_fallbackEmissive = device.createTexture(d);
    }

    // Spot light constant buffer
    {
        BufferDescriptor d;
        d.size      = sizeof(SpotLightGPU);
        d.usage     = BufferUsage::Uniform;
        d.debugName = "SpotLightConstants";
        m_spotLightBuf = device.createBuffer(d);
    }

    // Shadow depth map — 2048×2048, persistent across frames
    {
        TextureDescriptor d;
        d.width     = 2048;
        d.height    = 2048;
        d.format    = TextureFormat::Depth32Float;
        d.usage     = TextureUsage::DepthStencil | TextureUsage::ShaderRead;
        d.debugName = "ShadowDepth";
        m_shadowDepthTex = device.createTexture(d);
    }

    // TAA history textures
    recreateTAAHistory(device, w, h);
}

void FrameRenderer::recreateTAAHistory(IRenderDevice& device, u32 w, u32 h)
{
    for (int i = 0; i < 2; ++i)
    {
        TextureDescriptor d;
        d.width     = w;
        d.height    = h;
        d.format    = TextureFormat::RGBA16Float;
        d.usage     = TextureUsage::RenderTarget | TextureUsage::ShaderRead;
        d.debugName = (i == 0) ? "TAAHistory0" : "TAAHistory1";
        m_taaHistory[i] = device.createTexture(d);
    }
}

// ─── resize ──────────────────────────────────────────────────────────────────

void FrameRenderer::resize(IRenderDevice& device, u32 width, u32 height)
{
    if (m_width == width && m_height == height)
        return;

    m_width  = width;
    m_height = height;
    recreateTAAHistory(device, width, height);
}

// ─── renderFrame ─────────────────────────────────────────────────────────────

void FrameRenderer::renderFrame(IRenderDevice& device,
                                ICommandQueue& queue,
                                ISwapchain&    swapchain,
                                const SceneView& scene,
                                u32            swapW,
                                u32            swapH)
{
    if (swapW != m_width || swapH != m_height)
        resize(device, swapW, swapH);

    // ── TAA ping-pong ──────────────────────────────────────────────────────────
    const u32 currHist = m_frameIndex & 1u;
    const u32 prevHist = 1u - currHist;

    // ── TAA jitter (sub-pixel, in clip space) ──────────────────────────────────
    const glm::vec2 jitterPx  = haltonJitter(m_frameIndex);
    const glm::vec2 jitterNDC = jitterPx * glm::vec2(2.0f / static_cast<f32>(swapW),
                                                       2.0f / static_cast<f32>(swapH));
    glm::mat4 jitterMat(1.0f);
    jitterMat[3][0] = jitterNDC.x;
    jitterMat[3][1] = jitterNDC.y;

    const glm::mat4 jitteredVP = jitterMat * scene.proj * scene.view;

    // ── Build FrameGPU ────────────────────────────────────────────────────────
    FrameGPU frame;
    frame.view         = scene.view;
    frame.proj         = scene.proj;
    frame.viewProj     = jitteredVP;
    frame.invViewProj  = glm::inverse(jitteredVP);
    frame.prevViewProj = scene.prevProj * scene.prevView;   // unjittered prev VP

    frame.cameraPos    = glm::vec4(scene.cameraPos, 0.0f);
    frame.cameraDir    = glm::vec4(scene.cameraDir, 0.0f);
    frame.screenSize   = glm::vec4(static_cast<f32>(swapW), static_cast<f32>(swapH),
                                   1.0f / static_cast<f32>(swapW),
                                   1.0f / static_cast<f32>(swapH));

    frame.sunDirection = glm::vec4(glm::normalize(scene.sunDirection), 0.0f);
    frame.sunColor     = glm::vec4(scene.sunColor, scene.sunIntensity);
    frame.ambientColor = glm::vec4(scene.ambientColor, 0.0f);

    frame.time       = scene.time;
    frame.deltaTime  = scene.deltaTime;
    frame.frameIndex = static_cast<f32>(m_frameIndex);
    frame.pad0       = 0.0f;
    frame.jitter     = jitterPx;
    frame.pad1       = glm::vec2(0.0f);

    // ── Shadow-casting light view-projection ──────────────────────────────────────
    // Spot light (first in list) takes priority; falls back to sun for outdoor.
    SpotLightGPU spotGPU{};
    if (!scene.spotLights.empty())
    {
        const SpotLight& spot   = scene.spotLights[0];
        const glm::vec3  pos    = spot.position;
        const glm::vec3  dir    = glm::normalize(spot.direction);
        const glm::vec3  up     = (std::abs(dir.y) > 0.99f)
                                  ? glm::vec3(1.f, 0.f, 0.f)
                                  : glm::vec3(0.f, 1.f, 0.f);
        const glm::mat4 spotView = glm::lookAtLH(pos, pos + dir, up);
        const glm::mat4 spotProj = glm::perspectiveLH_ZO(
                                       spot.outerConeAngle * 2.0f, 1.0f,
                                       0.5f, spot.range);  // 0.5 near: no surface closer; much better NDC depth precision
        frame.sunViewProj = spotProj * spotView;

        spotGPU.positionRange     = glm::vec4(pos, spot.range);
        spotGPU.directionOuterCos = glm::vec4(dir, std::cos(spot.outerConeAngle));
        spotGPU.colorIntensity    = glm::vec4(spot.color, spot.intensity);
        spotGPU.innerCosAndPad    = glm::vec4(std::cos(spot.innerConeAngle), 0.f, 0.f, 0.f);
    }
    else
    {
        // Outdoor fallback: orthographic sun shadow
        const glm::vec3 lightDir = glm::normalize(scene.sunDirection);
        const glm::vec3 up       = (std::abs(lightDir.y) > 0.99f)
                                   ? glm::vec3(0.f, 0.f, 1.f)
                                   : glm::vec3(0.f, 1.f, 0.f);
        const glm::vec3 center(0.0f, 2.0f, 0.0f);
        const glm::mat4 lightView = glm::lookAtLH(center + lightDir * 20.f, center, up);
        const glm::mat4 lightProj = glm::orthoLH_ZO(-12.f, 12.f, -12.f, 12.f, 0.1f, 40.f);
        frame.sunViewProj = lightProj * lightView;
    }

    // Upload spot light buffer
    {
        void* p = m_spotLightBuf->map();
        std::memcpy(p, &spotGPU, sizeof(SpotLightGPU));
        m_spotLightBuf->unmap();
    }

    {
        void* p = m_frameConstBuf->map();
        std::memcpy(p, &frame, sizeof(FrameGPU));
        m_frameConstBuf->unmap();
    }

    // ── Upload light buffer ────────────────────────────────────────────────────
    {
        constexpr u32 kMaxLights = 64;
        const u32 lightCount = static_cast<u32>(
            std::min<size_t>(scene.pointLights.size(), kMaxLights));

        void* base = m_lightBuf->map();
        // Header: u32 count + 3 × u32 padding = 16 bytes
        std::memcpy(base, &lightCount, sizeof(u32));
        u32 pad[3] = {};
        std::memcpy(static_cast<char*>(base) + sizeof(u32), pad, sizeof(pad));
        // Light array
        auto* lights = reinterpret_cast<PointLightGPU*>(static_cast<char*>(base) + 16);
        for (u32 i = 0; i < lightCount; ++i)
        {
            lights[i].positionRadius = glm::vec4(scene.pointLights[i].position,
                                                  scene.pointLights[i].radius);
            lights[i].colorIntensity = glm::vec4(scene.pointLights[i].color,
                                                  scene.pointLights[i].intensity);
        }
        m_lightBuf->unmap();
    }

    // ── TAA history warm-up: GPU-clear on first frame ────────────────────────────────────
    // MTLStorageModePrivate textures start with undefined content; a previous run may have
    // left green data in those GPU memory pages.  Issue an empty render pass (clear-on-load)
    // for each history texture so they start black.  Both passes are committed on a separate
    // command buffer that is enqueued before the main frame buffer — Metal serialises them on
    // the same queue, guaranteeing the clears complete before any TAA read.
    if (m_frameIndex == 0)
    {
        auto clearCmd = queue.createCommandBuffer("ClearTAAHistory");
        for (int i = 0; i < 2; ++i)
        {
            RenderPassDescriptor rpd;
            rpd.debugLabel                       = (i == 0) ? "ClearTAAHistory0" : "ClearTAAHistory1";
            rpd.colorAttachmentCount             = 1;
            rpd.colorAttachments[0].texture      = m_taaHistory[i].get();
            rpd.colorAttachments[0].loadAction   = LoadAction::Clear;
            rpd.colorAttachments[0].storeAction  = StoreAction::Store;
            rpd.colorAttachments[0].clearColor   = { 0.0f, 0.0f, 0.0f, 0.0f };
            IRenderPassEncoder* enc = clearCmd->beginRenderPass(rpd);
            enc->end();
        }
        clearCmd->commit();
    }

    // ── Reset and populate the render graph ─────────────────────────────────────────────
    m_graph.reset();

    // Declare transient G-buffer textures matching the spec §Pass 4 layout.
    // Width/height = 0 means "match swapchain dimensions" in RenderGraph::compile.
    const RGTextureId gDepthId = m_graph.createTexture({
        0, 0, TextureFormat::Depth32Float,
        TextureUsage::DepthStencil | TextureUsage::ShaderRead,
        "GDepth"
    });
    const RGTextureId gAlbedoId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA8Unorm,                          // RT0: albedo + baked AO
        TextureUsage::RenderTarget | TextureUsage::ShaderRead,
        "GAlbedo"
    });
    const RGTextureId gNormalId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA8Unorm,                          // RT1: oct normal + roughness + metalness
        TextureUsage::RenderTarget | TextureUsage::ShaderRead,
        "GNormal"
    });
    const RGTextureId gEmissiveId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA16Float,                         // RT2: emissive
        TextureUsage::RenderTarget | TextureUsage::ShaderRead,
        "GEmissive"
    });
    const RGTextureId gMotionId = m_graph.createTexture({
        0, 0, TextureFormat::RG16Float,                           // RT3: motion vectors
        TextureUsage::RenderTarget | TextureUsage::ShaderRead,
        "GMotion"
    });
    const RGTextureId ssaoId = m_graph.createTexture({
        0, 0, TextureFormat::R32Float,
        TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
        "SSAO"
    });
    const RGTextureId ssaoBlurId = m_graph.createTexture({
        0, 0, TextureFormat::R32Float,
        TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
        "SSAOBlur"
    });
    const RGTextureId hdrId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA16Float,
        TextureUsage::RenderTarget | TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
        "HDR"
    });
    const RGTextureId bloomAId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA16Float,
        TextureUsage::RenderTarget | TextureUsage::ShaderRead,
        "BloomA"
    });
    const RGTextureId bloomBId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA16Float,
        TextureUsage::RenderTarget | TextureUsage::ShaderRead,
        "BloomB"
    });

    // Import persistent textures
    const RGTextureId taaOutId  = m_graph.importTexture("TAAHistoryCurr",
                                                          m_taaHistory[currHist].get());
    const RGTextureId taaHistId     = m_graph.importTexture("TAAHistoryPrev",
                                                              m_taaHistory[prevHist].get());
    const RGTextureId shadowDepthId = m_graph.importTexture("ShadowDepth",
                                                              m_shadowDepthTex.get());
    // Import swapchain drawable
    ITexture* drawable = swapchain.nextDrawable();
    const RGTextureId swapId = m_graph.importTexture("Swapchain", drawable);

    // ── Allocate transient textures ───────────────────────────────────────────
    m_graph.compile(device, swapW, swapH);

    // Resolve texture pointers (all valid after compile)
    ITexture* gDepthTex   = m_graph.get(gDepthId);
    ITexture* gAlbedoTex  = m_graph.get(gAlbedoId);
    ITexture* gNormalTex  = m_graph.get(gNormalId);
    ITexture* gEmissiveTex = m_graph.get(gEmissiveId);
    ITexture* gMotionTex  = m_graph.get(gMotionId);
    ITexture* ssaoTex     = m_graph.get(ssaoId);
    ITexture* ssaoBlurTex = m_graph.get(ssaoBlurId);
    ITexture* hdrTex      = m_graph.get(hdrId);
    ITexture* bloomATex   = m_graph.get(bloomAId);
    ITexture* bloomBTex   = m_graph.get(bloomBId);
    ITexture* taaOutTex      = m_taaHistory[currHist].get();
    ITexture* taaHistTex     = m_taaHistory[prevHist].get();
    ITexture* shadowDepthTex = m_shadowDepthTex.get();

    // Fallback textures for draws with no material assigned
    ITexture* fallbackAlbedo   = m_fallbackAlbedo.get();
    ITexture* fallbackNormal   = m_fallbackNormal.get();
    ITexture* fallbackEmissive = m_fallbackEmissive.get();

    // Capture pipeline + sampler state by value for lambda use
    IBuffer*   frameBuf      = m_frameConstBuf.get();
    IBuffer*   lightBuf      = m_lightBuf.get();
    IBuffer*   spotBuf       = m_spotLightBuf.get();
    ISampler*  linSamp       = m_linearClampSampler.get();
    ISampler*  repeatSamp    = m_linearRepeatSampler.get();
    IPipeline* pGBuf         = m_gbufferPSO.get();
    IPipeline* pShadow       = m_shadowDepthPSO.get();
    IPipeline* pSSAO         = m_ssaoPSO.get();
    IPipeline* pSSAOBlur     = m_ssaoBlurPSO.get();
    IPipeline* pLighting     = m_lightingPSO.get();
    IPipeline* pSkybox       = m_skyboxPSO.get();
    IPipeline* pTAA          = m_taaPSO.get();
    IPipeline* pBloomEx      = m_bloomExtractPSO.get();
    IPipeline* pBloomH       = m_bloomBlurHPSO.get();
    IPipeline* pBloomV       = m_bloomBlurVPSO.get();
    IPipeline* pTonemap      = m_tonemapPSO.get();

    // Snapshot the draw list for lambda capture (avoids capturing scene by ref).
    const std::vector<MeshDraw>& draws = scene.meshDraws;

    const Viewport    vp{ 0.0f, 0.0f, static_cast<f32>(swapW), static_cast<f32>(swapH) };
    const ScissorRect sc{ 0, 0, swapW, swapH };

    // ───────────────────────────────────────────────────────────────────────────────
    // Pass 1 — Shadow depth (depth-only, 2048×2048)
    // ───────────────────────────────────────────────────────────────────────────────
    {
        const Viewport    shadowVP { 0.f, 0.f, 2048.f, 2048.f };
        const ScissorRect shadowSC { 0,   0,   2048u,  2048u  };
        RGRenderPassDesc p;
        p.name             = "ShadowDepth";
        p.colorOutputCount = 0;
        p.depthOutput      = shadowDepthId;
        p.clearDepth       = 1.0f;
        p.execute = [=, &draws](IRenderPassEncoder* enc)
        {
            enc->setViewport(shadowVP);
            enc->setScissor(shadowSC);
            enc->setRenderPipeline(pShadow);
            enc->setVertexBuffer(frameBuf, 0, 0);
            for (const MeshDraw& draw : draws)
            {
                const ModelGPU modelGPU{
                    draw.modelMatrix,
                    glm::mat4(glm::inverse(glm::transpose(draw.modelMatrix))),
                    draw.prevModel
                };
                enc->setVertexBytes(&modelGPU, sizeof(ModelGPU), 1);
                enc->setVertexBuffer(draw.vertexBuffer, 0, k_vboSlot);
                enc->setIndexBuffer(draw.indexBuffer, 0, true);
                enc->drawIndexed(draw.indexCount);
            }
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ───────────────────────────────────────────────────────────────────────────────
    // Pass 2 — G-buffer (4 colour outputs + depth, spec §Pass 4 layout)
    // ───────────────────────────────────────────────────────────────────────────────
    {
        RGRenderPassDesc p;
        p.name             = "GBuffer";
        p.colorOutputs[0]  = gAlbedoId;
        p.colorOutputs[1]  = gNormalId;
        p.colorOutputs[2]  = gEmissiveId;
        p.colorOutputs[3]  = gMotionId;
        p.colorOutputCount = 4;
        p.depthOutput      = gDepthId;
        p.clearDepth       = 1.0f;
        p.execute = [=, &draws](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pGBuf);
            enc->setFragmentSampler(repeatSamp, 0); // sampler(0) for all material textures
            for (const MeshDraw& draw : draws)
            {
                const ModelGPU modelGPU{
                    draw.modelMatrix,
                    glm::mat4(glm::inverse(glm::transpose(draw.modelMatrix))),
                    draw.prevModel
                };
                // Vertex stage: frame constants + per-draw model
                enc->setVertexBuffer(frameBuf, 0, 0);
                enc->setVertexBytes(&modelGPU, sizeof(ModelGPU), 1);
                enc->setVertexBuffer(draw.vertexBuffer, 0, k_vboSlot);
                // Fragment stage: frame constants
                enc->setFragmentBuffer(frameBuf, 0, 0);
                // Fragment stage: per-draw material constants
                const MaterialConstantsGPU matConst{ draw.material.roughness,
                                                     draw.material.metalness,
                                                     0.0f, 0.0f };
                enc->setFragmentBytes(&matConst, sizeof(MaterialConstantsGPU), 1);
                // Fragment stage: material textures (fallback if nullptr)
                enc->setFragmentTexture(
                    draw.material.albedo    ? draw.material.albedo    : fallbackAlbedo,   0);
                enc->setFragmentTexture(
                    draw.material.normalMap ? draw.material.normalMap : fallbackNormal,   1);
                enc->setFragmentTexture(
                    draw.material.emissive  ? draw.material.emissive  : fallbackEmissive, 2);
                enc->setIndexBuffer(draw.indexBuffer, 0, true);
                enc->drawIndexed(draw.indexCount);
            }
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 3 — SSAO (compute)
    // ─────────────────────────────────────────────────────────────────────────
    {
        RGComputePassDesc p;
        p.name    = "SSAO";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pSSAO);
            enc->setTexture(gDepthTex,  0);   // gDepth   (read)
            enc->setTexture(gNormalTex, 1);   // gNormal  (read)
            enc->setTexture(ssaoTex,    2);   // aoOut    (write)
            enc->setBuffer(frameBuf, 0, 0);
            enc->dispatch(swapW, swapH, 1);
        };
        m_graph.addComputePass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 3b — SSAO bilateral blur (compute)
    // ─────────────────────────────────────────────────────────────────────────
    {
        RGComputePassDesc p;
        p.name    = "SSAOBlur";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pSSAOBlur);
            enc->setTexture(ssaoTex,     0);   // aoIn    (raw AO, read)
            enc->setTexture(gDepthTex,   1);   // gDepth  (read, depth edge preservation)
            enc->setTexture(ssaoBlurTex, 2);   // aoOut   (write, smoothed AO)
            enc->setBuffer(frameBuf, 0, 0);
            enc->dispatch(swapW, swapH, 1);
        };
        m_graph.addComputePass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 4 — Deferred lighting (compute)
    // ─────────────────────────────────────────────────────────────────────────
    {
        RGComputePassDesc p;
        p.name    = "Lighting";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pLighting);
            enc->setTexture(gAlbedoTex,    0);   // gAlbedoAO
            enc->setTexture(gNormalTex,    1);   // gNormalRoughMet
            enc->setTexture(gDepthTex,     2);   // gDepth
            enc->setTexture(ssaoBlurTex,   3);   // ssaoTex (blurred AO)
            enc->setTexture(hdrTex,        4);   // hdrOut (write)
            enc->setTexture(shadowDepthTex, 5);  // shadow depth (PCF)
            enc->setTexture(gEmissiveTex,  6);   // gEmissive
            enc->setBuffer(frameBuf,  0, 0);
            enc->setBuffer(lightBuf,  0, 1);  // lightCount at offset 0
            enc->setBuffer(lightBuf, 16, 2);  // PointLightGPU[] at offset 16
            enc->setBuffer(spotBuf,   0, 3);  // SpotLightGPU
            enc->dispatch(swapW, swapH, 1);
        };
        m_graph.addComputePass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 5 — Skybox (loads HDR + depth, draws sky on background pixels)
    // ─────────────────────────────────────────────────────────────────────────
    {
        RGRenderPassDesc p;
        p.name             = "Skybox";
        p.colorOutputs[0]  = hdrId;
        p.colorOutputCount = 1;
        p.depthOutput      = gDepthId;
        p.loadColors       = true;   // preserve HDR content from lighting pass
        p.loadDepth        = true;   // use depth for sky-pixel test
        p.execute = [=](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pSkybox);
            enc->setFragmentBuffer(frameBuf, 0, 0);
            enc->draw(3);  // fullscreen triangle (vertex_id driven)
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 6 — TAA
    // ─────────────────────────────────────────────────────────────────────────
    {
        RGRenderPassDesc p;
        p.name             = "TAA";
        p.colorOutputs[0]  = taaOutId;
        p.colorOutputCount = 1;
        p.execute = [=](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pTAA);
            enc->setFragmentTexture(hdrTex,     0);  // current HDR
            enc->setFragmentTexture(taaHistTex, 1);  // history (previous frame)
            enc->setFragmentTexture(gMotionTex, 2);  // motion vectors
            enc->setFragmentSampler(linSamp,    0);
            enc->setFragmentBuffer(frameBuf,    0, 0);
            enc->draw(3);
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 7 — Bloom extract
    // ─────────────────────────────────────────────────────────────────────────
    {
        RGRenderPassDesc p;
        p.name             = "BloomExtract";
        p.colorOutputs[0]  = bloomAId;
        p.colorOutputCount = 1;
        p.execute = [=](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pBloomEx);
            enc->setFragmentTexture(taaOutTex, 0);   // TAA output as source
            enc->setFragmentSampler(linSamp,   0);
            enc->setFragmentBuffer(frameBuf,   0, 0);
            enc->draw(3);
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 8 — Bloom horizontal blur (bloomA → bloomB)
    // ─────────────────────────────────────────────────────────────────────────
    {
        RGRenderPassDesc p;
        p.name             = "BloomBlurH";
        p.colorOutputs[0]  = bloomBId;
        p.colorOutputCount = 1;
        p.execute = [=](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pBloomH);
            enc->setFragmentTexture(bloomATex, 0);
            enc->setFragmentSampler(linSamp,   0);
            enc->setFragmentBuffer(frameBuf,   0, 0);
            enc->draw(3);
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 9 — Bloom vertical blur (bloomB → bloomA)
    // ─────────────────────────────────────────────────────────────────────────
    {
        RGRenderPassDesc p;
        p.name             = "BloomBlurV";
        p.colorOutputs[0]  = bloomAId;
        p.colorOutputCount = 1;
        p.execute = [=](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pBloomV);
            enc->setFragmentTexture(bloomBTex, 0);
            enc->setFragmentSampler(linSamp,   0);
            enc->setFragmentBuffer(frameBuf,   0, 0);
            enc->draw(3);
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 10 — Tone mapping → swapchain
    // ─────────────────────────────────────────────────────────────────────────
    {
        RGRenderPassDesc p;
        p.name             = "ToneMapping";
        p.colorOutputs[0]  = swapId;
        p.colorOutputCount = 1;
        p.execute = [=](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pTonemap);
            enc->setFragmentTexture(taaOutTex,  0);  // TAA-resolved HDR
            enc->setFragmentTexture(bloomATex,  1);  // bloom result
            enc->setFragmentSampler(linSamp,    0);
            enc->setFragmentBuffer(frameBuf,    0, 0);
            enc->draw(3);
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ── Record and submit ─────────────────────────────────────────────────────
    auto cmdBuf = queue.createCommandBuffer("Frame");
    cmdBuf->pushDebugGroup("FrameRenderer");
    m_graph.execute(*cmdBuf);
    cmdBuf->popDebugGroup();
    cmdBuf->present(swapchain);
    cmdBuf->commit();

    ++m_frameIndex;
}

} // namespace daedalus::render
