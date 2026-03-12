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
#include <cstdio>

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

    // ── Transparent forward pass (alpha-blended, depth-test no write) ────────────
    {
        auto vsTransparent = loadVS("transparent_vert");
        auto fsTransparent = loadFS("transparent_frag");
        RenderPipelineDescriptor d;
        d.vertexShader         = vsTransparent.get();
        d.fragmentShader       = fsTransparent.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::RGBA16Float;
        // Standard alpha blending: out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
        d.blendStates[0].blendEnabled  = true;
        d.blendStates[0].srcRGB        = BlendFactor::SrcAlpha;
        d.blendStates[0].dstRGB        = BlendFactor::OneMinusSrcAlpha;
        d.blendStates[0].rgbOp         = BlendOperation::Add;
        d.blendStates[0].srcAlpha      = BlendFactor::One;
        d.blendStates[0].dstAlpha      = BlendFactor::OneMinusSrcAlpha;
        d.blendStates[0].alphaOp       = BlendOperation::Add;
        d.depthFormat          = TextureFormat::Depth32Float;
        d.depthTest            = true;
        d.depthWrite           = false;   // read-only depth: transparent objects don't occlude
        d.depthCompare         = CompareFunction::LessEqual;
        d.cullMode             = CullMode::None;  // sprites are always face-on; None is safest
        d.vertexAttributes     = geometryAttributes();
        d.vertexBufferLayouts  = geometryLayouts();
        d.debugName            = "Transparent";
        m_transparentPSO = device.createRenderPipeline(d);
    }

    // ── Decal pass 2.5 (alpha-blend into G-buffer RT0 and RT1) ───────────────
    {
        auto vsDecal = loadVS("decal_vert");
        auto fsDecal = loadFS("decal_frag");
        RenderPipelineDescriptor d;
        d.vertexShader         = vsDecal.get();
        d.fragmentShader       = fsDecal.get();
        d.colorAttachmentCount = 2;
        d.colorFormats[0]      = TextureFormat::RGBA8Unorm;  // RT0: albedo + AO
        d.colorFormats[1]      = TextureFormat::RGBA8Unorm;  // RT1: oct normal + roughness + metalness
        // RT0: blend albedo.rgb by opacity; preserve AO in destination alpha.
        d.blendStates[0].blendEnabled = true;
        d.blendStates[0].srcRGB       = BlendFactor::SrcAlpha;
        d.blendStates[0].dstRGB       = BlendFactor::OneMinusSrcAlpha;
        d.blendStates[0].rgbOp        = BlendOperation::Add;
        d.blendStates[0].srcAlpha     = BlendFactor::Zero;   // do not overwrite AO
        d.blendStates[0].dstAlpha     = BlendFactor::One;    // preserve existing AO
        d.blendStates[0].alphaOp      = BlendOperation::Add;
        // RT1: blend (octNorm, roughness) by opacity; preserve metalness in destination alpha.
        d.blendStates[1].blendEnabled = true;
        d.blendStates[1].srcRGB       = BlendFactor::SrcAlpha;
        d.blendStates[1].dstRGB       = BlendFactor::OneMinusSrcAlpha;
        d.blendStates[1].rgbOp        = BlendOperation::Add;
        d.blendStates[1].srcAlpha     = BlendFactor::Zero;   // do not overwrite metalness
        d.blendStates[1].dstAlpha     = BlendFactor::One;    // preserve existing metalness
        d.blendStates[1].alphaOp      = BlendOperation::Add;
        d.depthFormat          = TextureFormat::Depth32Float;
        d.depthTest            = true;
        d.depthWrite           = false;         // read-only: do NOT overwrite depth
        d.depthCompare         = CompareFunction::LessEqual;
        d.cullMode             = CullMode::None;  // let depth test do all clipping;
                                                  // top face winding varies with camera
        d.vertexAttributes     = geometryAttributes();
        d.vertexBufferLayouts  = geometryLayouts();
        d.debugName            = "Decal";
        m_decalPSO = device.createRenderPipeline(d);
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

    // ── Tone mapping → swapchain ─────────────────────────────────────────────
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

    // ── Depth copy (compute): Depth32Float → R32Float — eliminates decal depth feedback loop ───
    {
        auto csDepthCopy = loadCS("depth_copy_main");
        ComputePipelineDescriptor d;
        d.computeShader = csDepthCopy.get();
        d.debugName     = "DepthCopy";
        m_depthCopyPSO = device.createComputePipeline(d);
    }

    // ── Volumetric fog scatter (compute) ─────────────────────────────────────
    {
        auto csFogScatter = loadCS("fog_scatter");
        ComputePipelineDescriptor d;
        d.computeShader = csFogScatter.get();
        d.debugName     = "FogScatter";
        m_fogScatterPSO = device.createComputePipeline(d);
    }

    // ── Volumetric fog integrate (compute) ───────────────────────────────────
    {
        auto csFogIntegrate = loadCS("fog_integrate");
        ComputePipelineDescriptor d;
        d.computeShader = csFogIntegrate.get();
        d.debugName     = "FogIntegrate";
        m_fogIntegratePSO = device.createComputePipeline(d);
    }

    // ── Volumetric fog composite (compute) ───────────────────────────────────
    {
        auto csFogComposite = loadCS("fog_composite");
        ComputePipelineDescriptor d;
        d.computeShader = csFogComposite.get();
        d.debugName     = "FogComposite";
        m_fogCompositePSO = device.createComputePipeline(d);
    }

    // ── Screen-space reflections (compute) ──────────────────────────────────────────────────────────
    {
        auto csSSR = loadCS("ssr_main");
        ComputePipelineDescriptor d;
        d.computeShader = csSSR.get();
        d.debugName     = "SSR";
        m_ssrPSO = device.createComputePipeline(d);
    }

    // ── Depth of field — CoC (compute) ──────────────────────────────────────────────────────────
    {
        auto csDofCoc = loadCS("dof_coc");
        ComputePipelineDescriptor d;
        d.computeShader = csDofCoc.get();
        d.debugName     = "DoFCoC";
        m_dofCocPSO = device.createComputePipeline(d);
    }

    // ── Depth of field — blur (compute) ──────────────────────────────────────────────────────────
    {
        auto csDofBlur = loadCS("dof_blur");
        ComputePipelineDescriptor d;
        d.computeShader = csDofBlur.get();
        d.debugName     = "DoFBlur";
        m_dofBlurPSO = device.createComputePipeline(d);
    }

    // ── Depth of field — composite (compute) ───────────────────────────────────────────────────────
    {
        auto csDofComposite = loadCS("dof_composite");
        ComputePipelineDescriptor d;
        d.computeShader = csDofComposite.get();
        d.debugName     = "DoFComposite";
        m_dofCompositePSO = device.createComputePipeline(d);
    }

    // ── Motion blur (compute) ────────────────────────────────────────────────────────────────────
    {
        auto csMotionBlur = loadCS("motion_blur_main");
        ComputePipelineDescriptor d;
        d.computeShader = csMotionBlur.get();
        d.debugName     = "MotionBlur";
        m_motionBlurPSO = device.createComputePipeline(d);
    }

    // ── Colour grading → intermediate or swapchain (render) ─────────────────────────────────────────────────
    // Reuses tonemap_vert (fullscreen triangle VS) with a new fragment shader.
    {
        auto fsColorGrade = loadFS("color_grade_frag");
        RenderPipelineDescriptor d;
        d.vertexShader         = vsTonemap.get();      // fullscreen triangle
        d.fragmentShader       = fsColorGrade.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::BGRA8Unorm;  // intermediate or swapchain
        d.cullMode             = CullMode::None;
        d.debugName            = "ColorGrading";
        m_colorGradePSO = device.createRenderPipeline(d);
    }

    // ── Optional FX (vignette + grain + chromatic aberration) → intermediate or swapchain ───────
    // Pass 20: reuses tonemap_vert VS with the optional_fx_frag FS.
    {
        auto fsOptFx = loadFS("optional_fx_frag");
        RenderPipelineDescriptor d;
        d.vertexShader         = vsTonemap.get();      // fullscreen triangle
        d.fragmentShader       = fsOptFx.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::BGRA8Unorm;
        d.cullMode             = CullMode::None;
        d.debugName            = "OptionalFX";
        m_optionalFxPSO = device.createRenderPipeline(d);
    }

    // ── FXAA → swapchain (render) ──────────────────────────────────────────────────────────────
    // Pass 21: reuses tonemap_vert VS with the fxaa_frag FS.
    {
        auto fsFxaa = loadFS("fxaa_frag");
        RenderPipelineDescriptor d;
        d.vertexShader         = vsTonemap.get();      // fullscreen triangle
        d.fragmentShader       = fsFxaa.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::BGRA8Unorm;
        d.cullMode             = CullMode::None;
        d.debugName            = "FXAA";
        m_fxaaPSO = device.createRenderPipeline(d);
    }

    // ── Mirror G-buffer (CullMode::Back) ─────────────────────────────────────────
    // The reflected camera sits outside the room at z < -5, looking in +z.
    // Interior wall normals (N wall: 0,0,-1; W wall: +x; floor: +y; ceiling: -y)
    // all face TOWARD the reflected camera → they are front faces → rendered.
    // The south wall (normal = +z, the mirror plane itself) faces AWAY from the
    // reflected camera → it is a back face → correctly culled here, so it no
    // longer depth-occludes the room interior visible through the mirror.
    {
        RenderPipelineDescriptor d;
        d.vertexShader         = vsGBuffer.get();
        d.fragmentShader       = fsGBuffer.get();
        d.colorAttachmentCount = 4;
        d.colorFormats[0]      = TextureFormat::RGBA8Unorm;
        d.colorFormats[1]      = TextureFormat::RGBA8Unorm;
        d.colorFormats[2]      = TextureFormat::RGBA16Float;
        d.colorFormats[3]      = TextureFormat::RG16Float;
        d.depthFormat          = TextureFormat::Depth32Float;
        d.depthTest            = true;
        d.depthWrite           = true;
        d.depthCompare         = CompareFunction::Less;
        d.cullMode             = CullMode::Back;  // south wall back face is culled; room interior front faces render
        d.vertexAttributes     = geometryAttributes();
        d.vertexBufferLayouts  = geometryLayouts();
        d.debugName            = "MirrorGBuffer";
        m_mirrorGbufferPSO = device.createRenderPipeline(d);
    }

    // ── Particle emit (compute)
    {
        auto csEmit = loadCS("particle_emit");
        ComputePipelineDescriptor d;
        d.computeShader = csEmit.get();
        d.debugName     = "ParticleEmit";
        m_particleEmitPSO = device.createComputePipeline(d);
    }

    // ── Particle simulate (compute) ───────────────────────────────────────────
    {
        auto csSimulate = loadCS("particle_simulate");
        ComputePipelineDescriptor d;
        d.computeShader = csSimulate.get();
        d.debugName     = "ParticleSimulate";
        m_particleSimulatePSO = device.createComputePipeline(d);
    }

    // ── Particle compact (compute) ────────────────────────────────────────────
    {
        auto csCompact = loadCS("particle_compact");
        ComputePipelineDescriptor d;
        d.computeShader = csCompact.get();
        d.debugName     = "ParticleCompact";
        m_particleCompactPSO = device.createComputePipeline(d);
    }

    // ── Particle render (render, no vertex buffer — purely procedural) ────────
    // Premultiplied alpha blend: src = One, dst = OneMinusSrcAlpha.
    // Depth tested (LessEqual), depth NOT written.
    {
        auto vsParticle = loadVS("particle_vert");
        auto fsParticle = loadFS("particle_frag");
        RenderPipelineDescriptor d;
        d.vertexShader         = vsParticle.get();
        d.fragmentShader       = fsParticle.get();
        d.colorAttachmentCount = 1;
        d.colorFormats[0]      = TextureFormat::RGBA16Float;
        // Premultiplied alpha: out.rgb is pre-multiplied by alpha in the fragment shader.
        d.blendStates[0].blendEnabled  = true;
        d.blendStates[0].srcRGB        = BlendFactor::One;
        d.blendStates[0].dstRGB        = BlendFactor::OneMinusSrcAlpha;
        d.blendStates[0].rgbOp         = BlendOperation::Add;
        d.blendStates[0].srcAlpha      = BlendFactor::One;
        d.blendStates[0].dstAlpha      = BlendFactor::OneMinusSrcAlpha;
        d.blendStates[0].alphaOp       = BlendOperation::Add;
        d.depthFormat          = TextureFormat::Depth32Float;
        d.depthTest            = true;
        d.depthWrite           = false;         // particles do not occlude opaque geometry
        d.depthCompare         = CompareFunction::LessEqual;
        d.cullMode             = CullMode::None; // camera-facing quads are always visible
        // No vertex attributes — vertex_id + instance_id drive procedural generation.
        d.debugName            = "ParticleRender";
        m_particleRenderPSO = device.createRenderPipeline(d);
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

    // Spot light buffer: 16-byte header (u32 count + 3×pad) + up to 16 lights.
    {
        constexpr u32 kMaxSpotLights = 16;
        BufferDescriptor d;
        d.size      = 16 + sizeof(SpotLightGPU) * kMaxSpotLights;
        d.usage     = BufferUsage::Storage | BufferUsage::Uniform;
        d.debugName = "SpotLightBuffer";
        m_spotLightBuf = device.createBuffer(d);
    }

    // Decal diagnostic: 1×u32 atomic fragment counter.
    // The decal fragment shader atomically increments this for every live fragment
    // that passes all OBB/depth checks.  CPU reads the previous frame's count at
    // the start of each frame to confirm decals are actually rendering.
    {
        BufferDescriptor d;
        d.size      = sizeof(u32);
        d.usage     = BufferUsage::Storage;
        d.debugName = "DecalDebugCounter";
        m_decalDebugBuf = device.createBuffer(d);
        *static_cast<u32*>(m_decalDebugBuf->map()) = 0u;
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

    // Unit cube mesh — shared VBO + IBO used by every deferred decal draw.
    // The decal vertex shader only consumes position (attr 0); normal/UV/tangent
    // are zeroed because the StaticMeshVertex stride (48 B) must be preserved
    // to match the vertex buffer layout registered in the PSO.
    {
        const StaticMeshVertex verts[8] =
        {
            //  pos                    normal    uv         tangent
            {{ -0.5f, -0.5f, -0.5f }, {}, {},  {} },  // 0 bottom-left-back
            {{  0.5f, -0.5f, -0.5f }, {}, {},  {} },  // 1 bottom-right-back
            {{  0.5f,  0.5f, -0.5f }, {}, {},  {} },  // 2 top-right-back
            {{ -0.5f,  0.5f, -0.5f }, {}, {},  {} },  // 3 top-left-back
            {{ -0.5f, -0.5f,  0.5f }, {}, {},  {} },  // 4 bottom-left-front
            {{  0.5f, -0.5f,  0.5f }, {}, {},  {} },  // 5 bottom-right-front
            {{  0.5f,  0.5f,  0.5f }, {}, {},  {} },  // 6 top-right-front
            {{ -0.5f,  0.5f,  0.5f }, {}, {},  {} },  // 7 top-left-front
        };
        BufferDescriptor bd;
        bd.size      = sizeof(verts);
        bd.usage     = BufferUsage::Vertex;
        bd.initData  = verts;
        bd.debugName = "UnitCubeVBO";
        m_unitCubeVBO = device.createBuffer(bd);

        // 6 faces × 2 triangles × 3 vertices = 36 indices.
        // Winding is CCW when viewed from outside the cube (front-faces outward).
        const u32 indices[36] =
        {
            4, 5, 6,   4, 6, 7,   // Front  (+Z)
            1, 0, 3,   1, 3, 2,   // Back   (-Z)
            5, 1, 2,   5, 2, 6,   // Right  (+X)
            0, 4, 7,   0, 7, 3,   // Left   (-X)
            7, 6, 2,   7, 2, 3,   // Top    (+Y)
            0, 1, 5,   0, 5, 4,   // Bottom (-Y)
        };
        BufferDescriptor id;
        id.size      = sizeof(indices);
        id.usage     = BufferUsage::Index;
        id.initData  = indices;
        id.debugName = "UnitCubeIBO";
        m_unitCubeIBO = device.createBuffer(id);
    }

    // Froxel 3D textures — 160×90×64 RGBA16Float, ShaderRead + ShaderWrite.
    // Persistent across frames: scatter is overwritten each frame by FogScatter;
    // integrate is overwritten each frame by FogIntegrate.
    static constexpr u32 kFroxelW = 160;
    static constexpr u32 kFroxelH = 90;
    static constexpr u32 kFroxelD = 64;
    {
        TextureDescriptor d;
        d.width     = kFroxelW;
        d.height    = kFroxelH;
        d.depth     = kFroxelD;
        d.format    = TextureFormat::RGBA16Float;
        d.usage     = TextureUsage::ShaderRead | TextureUsage::ShaderWrite;
        d.debugName = "FroxelScatter";
        m_froxelScatterTex = device.createTexture(d);
    }
    {
        TextureDescriptor d;
        d.width     = kFroxelW;
        d.height    = kFroxelH;
        d.depth     = kFroxelD;
        d.format    = TextureFormat::RGBA16Float;
        d.usage     = TextureUsage::ShaderRead | TextureUsage::ShaderWrite;
        d.debugName = "FroxelIntegrate";
        m_froxelIntegrateTex = device.createTexture(d);
    }

    // Identity LUT — 32×32×32 RGBA8Unorm 3D texture.
    // Procedurally filled so that r=R, g=G, b=B (passthrough).  Used as the
    // colour grading LUT fallback when scene.colorGrading.lutTexture is nullptr.
    {
        static constexpr u32 kLutSize = 32;
        static constexpr u32 kPixels  = kLutSize * kLutSize * kLutSize;

        // Pixel layout in memory: z (blue) is the outermost axis, then y (green),
        // then x (red) — i.e. pixel at (r, g, b) = index b*32*32 + g*32 + r.
        std::vector<u8> lutData(kPixels * 4);
        for (u32 b = 0; b < kLutSize; ++b)
        for (u32 g = 0; g < kLutSize; ++g)
        for (u32 r = 0; r < kLutSize; ++r)
        {
            const u32 idx     = (b * kLutSize * kLutSize + g * kLutSize + r) * 4;
            lutData[idx + 0]  = static_cast<u8>(r * 255u / (kLutSize - 1u));  // R
            lutData[idx + 1]  = static_cast<u8>(g * 255u / (kLutSize - 1u));  // G
            lutData[idx + 2]  = static_cast<u8>(b * 255u / (kLutSize - 1u));  // B
            lutData[idx + 3]  = 255u;                                          // A
        }

        TextureDescriptor d;
        d.width     = kLutSize;
        d.height    = kLutSize;
        d.depth     = kLutSize;
        d.format    = TextureFormat::RGBA8Unorm;
        d.usage     = TextureUsage::ShaderRead;
        d.initData  = lutData.data();
        d.debugName = "IdentityLUT";
        m_identityLutTex = device.createTexture(d);
    }

    // Mirror pre-pass frame constant buffer (one reflected FrameGPU per frame)
    {
        BufferDescriptor d;
        d.size      = sizeof(FrameGPU);
        d.usage     = BufferUsage::Uniform;
        d.debugName = "MirrorFrameConstants";
        m_mirrorFrameConstBuf = device.createBuffer(d);
    }

    // Fallback 1×1 SSAO texture — full white (AO=1.0, no occlusion).
    // Bound at slot 3 in the mirror pre-pass lighting compute so the reflected
    // scene is not darkened by a missing SSAO pass.
    {
        const float oneF = 1.0f;
        TextureDescriptor d;
        d.width     = 1;
        d.height    = 1;
        d.format    = TextureFormat::R32Float;
        d.usage     = TextureUsage::ShaderRead;
        d.initData  = &oneF;
        d.debugName = "FallbackSsao";
        m_fallbackSsao = device.createTexture(d);
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
    m_frameIndex = 0;  // force TAA history re-clear on next renderFrame
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

    // ── Sort and upload spot light buffer ───────────────────────────────────────────────────────────────
    // The nearest spotlight with castsShadows=true goes to index 0 and claims the
    // shadow map.  All others follow in arbitrary order.  Up to 16 lights total.
    constexpr u32 kMaxSpotLights = 16;
    {
        const auto& spots = scene.spotLights;
        const u32   rawCount = static_cast<u32>(spots.size());

        // Find nearest shadow-eligible light.
        int   shadowIdx  = -1;
        float bestDistSq = FLT_MAX;
        for (u32 i = 0; i < rawCount; ++i)
        {
            if (!spots[i].castsShadows) continue;
            const float d2 = glm::dot(spots[i].position - scene.cameraPos,
                                       spots[i].position - scene.cameraPos);
            if (d2 < bestDistSq) { bestDistSq = d2; shadowIdx = static_cast<int>(i); }
        }

        // Build sorted pointer list: shadow-caster first, then the rest.
        std::vector<const SpotLight*> sorted;
        sorted.reserve(rawCount);
        if (shadowIdx >= 0) sorted.push_back(&spots[shadowIdx]);
        for (u32 i = 0; i < rawCount; ++i)
            if (static_cast<int>(i) != shadowIdx)
                sorted.push_back(&spots[i]);

        // Build frame.sunViewProj from the shadow caster (sorted[0]), else fall back to sun.
        if (shadowIdx >= 0)
        {
            const SpotLight& spot = *sorted[0];
            const glm::vec3  pos  = spot.position;
            const glm::vec3  dir  = glm::normalize(spot.direction);
            const glm::vec3  up   = (std::abs(dir.y) > 0.99f)
                                    ? glm::vec3(1.f, 0.f, 0.f)
                                    : glm::vec3(0.f, 1.f, 0.f);
            const glm::mat4 spotView = glm::lookAtLH(pos, pos + dir, up);
            const glm::mat4 spotProj = glm::perspectiveLH_ZO(
                                           spot.outerConeAngle * 2.0f, 1.0f, 0.5f, spot.range);
            frame.sunViewProj = spotProj * spotView;
        }
        else
        {
            // Outdoor fallback: orthographic sun shadow.
            const glm::vec3 lightDir = glm::normalize(scene.sunDirection);
            const glm::vec3 up       = (std::abs(lightDir.y) > 0.99f)
                                       ? glm::vec3(0.f, 0.f, 1.f)
                                       : glm::vec3(0.f, 1.f, 0.f);
            const glm::vec3 center(0.0f, 2.0f, 0.0f);
            const glm::mat4 lightView = glm::lookAtLH(center + lightDir * 20.f, center, up);
            const glm::mat4 lightProj = glm::orthoLH_ZO(-12.f, 12.f, -12.f, 12.f, 0.1f, 40.f);
            frame.sunViewProj = lightProj * lightView;
        }

        // Upload: u32 count + 3×pad (16 bytes) + SpotLightGPU[].
        const u32 uploadCount = std::min(static_cast<u32>(sorted.size()), kMaxSpotLights);
        void* base = m_spotLightBuf->map();
        std::memcpy(base, &uploadCount, sizeof(u32));
        u32 pad3[3] = {};
        std::memcpy(static_cast<char*>(base) + sizeof(u32), pad3, sizeof(pad3));
        auto* gpuSpots = reinterpret_cast<SpotLightGPU*>(static_cast<char*>(base) + 16);
        for (u32 i = 0; i < uploadCount; ++i)
        {
            const SpotLight& s   = *sorted[i];
            const glm::vec3  dir = glm::normalize(s.direction);
            gpuSpots[i].positionRange     = glm::vec4(s.position, s.range);
            gpuSpots[i].directionOuterCos = glm::vec4(dir, std::cos(s.outerConeAngle));
            gpuSpots[i].colorIntensity    = glm::vec4(s.color, s.intensity);
            gpuSpots[i].innerCosAndPad    = glm::vec4(std::cos(s.innerConeAngle), 0.f, 0.f, 0.f);
        }
        m_spotLightBuf->unmap();
    }

    // ── Mirror reflected view-projection (unjittered, matches the mirror RT) ─────────────────────────
    frame.mirrorViewProj = !scene.mirrors.empty()
        ? (scene.mirrors[0].reflectedProj * scene.mirrors[0].reflectedView)
        : glm::mat4(1.0f);

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

    // ── Build VolumetricFogConstantsGPU ─────────────────────────────────────────────────
    // Built unconditionally (lambdas capture by value); fog passes only execute if enabled.
    VolumetricFogConstantsGPU fogConst{};
    fogConst.density    = scene.fog.density;
    fogConst.anisotropy = scene.fog.anisotropy;
    fogConst.scattering = scene.fog.scattering;
    fogConst.fogFar     = scene.fog.fogFar;
    fogConst.ambientFog = glm::vec4(scene.fog.ambientFog, scene.fog.fogNear);

    // ── Build SSRConstantsGPU ───────────────────────────────────────────────────────────────────────────────
    // Built unconditionally; SSR pass only executes when ssr.enabled is true.
    SSRConstantsGPU ssrConst{};
    ssrConst.maxDistance     = scene.ssr.maxDistance;
    ssrConst.thickness       = scene.ssr.thickness;
    ssrConst.roughnessCutoff = scene.ssr.roughnessCutoff;
    ssrConst.fadeStart       = scene.ssr.fadeStart;
    ssrConst.maxSteps        = scene.ssr.maxSteps;

    // ── Build DoFConstantsGPU ─────────────────────────────────────────────────────────────────────────────
    // Built unconditionally; DoF passes only execute when dof.enabled is true.
    DoFConstantsGPU dofConst{};
    dofConst.focusDistance  = scene.dof.focusDistance;
    dofConst.focusRange     = scene.dof.focusRange;
    dofConst.bokehRadius    = scene.dof.bokehRadius;
    dofConst.nearTransition = scene.dof.nearTransition;
    dofConst.farTransition  = scene.dof.farTransition;

    // ── Build MotionBlurConstantsGPU ──────────────────────────────────────────────────────────────────────
    // Built unconditionally; pass only executes when motionBlur.enabled is true.
    MotionBlurConstantsGPU mbConst{};
    mbConst.shutterAngle = scene.motionBlur.shutterAngle;
    mbConst.numSamples   = scene.motionBlur.numSamples;

    // ── Build ColorGradingConstantsGPU ──────────────────────────────────────────────────────────────────
    // Built unconditionally; pass only executes when colorGrading.enabled is true.
    ColorGradingConstantsGPU cgConst{};
    cgConst.intensity = scene.colorGrading.intensity;

    // ── Build OptionalFxConstantsGPU ─────────────────────────────────────────────────────────────────────
    // Built unconditionally; pass only executes when optionalFx.enabled is true.
    OptionalFxConstantsGPU optFxConst{};
    optFxConst.caAmount          = scene.optionalFx.caAmount;
    optFxConst.vignetteIntensity = scene.optionalFx.vignetteIntensity;
    optFxConst.vignetteRadius    = scene.optionalFx.vignetteRadius;
    optFxConst.grainAmount       = scene.optionalFx.grainAmount;
    // grainSeed varies every frame to prevent temporal grain aliasing.
    // Cycling over 1000 gives a visually non-repeating sequence at 60fps.
    optFxConst.grainSeed         = static_cast<f32>(m_frameIndex % 1000u);

    // ── TAA history warm-up: GPU-clear on first frame
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

    // ── Decal diagnostic: read previous frame's fragment count, zero for this frame ──────
    {
        u32* pCount = static_cast<u32*>(m_decalDebugBuf->map());
        if (m_frameIndex % 60 == 0)
        {
            std::printf("[Decals] frame %u: %u fragments rendered\n",
                        m_frameIndex, *pCount);
        }
        *pCount = 0u;  // zero before current frame's GPU work
    }

    // ── Mirror pre-pass: render each mirror's reflection into its render target ─────────────────────────
    // Must run before m_graph.reset() so it submits its own command buffer first.
    if (!scene.mirrors.empty())
        renderMirrorPrepass(device, queue, scene, frame);

    // ── Reset and populate the render graph ──────────────────────────────────────────
    m_graph.reset();

    // Declare transient G-buffer textures matching the spec §Pass 4 layout.
    // Width/height = 0 means "match swapchain dimensions" in RenderGraph::compile.
    const RGTextureId gDepthId = m_graph.createTexture({
        0, 0, TextureFormat::Depth32Float,
        TextureUsage::DepthStencil | TextureUsage::ShaderRead,
        "GDepth"
    });
    // Depth copy: R32Float snapshot of gDepth taken BEFORE the decal pass so
    // gDepth can remain as the depth attachment without a shader-read feedback loop.
    const RGTextureId gDepthCopyId = m_graph.createTexture({
        0, 0, TextureFormat::R32Float,
        TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
        "GDepthCopy"
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
    // SSR composited output: same dims/format as swapchain, compute read+write.
    const RGTextureId ssrOutId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA16Float,
        TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
        "SSROut"
    });
    // DoF intermediate textures (all compute read+write, only allocated when dof.enabled)
    const RGTextureId cocTexId = m_graph.createTexture({
        0, 0, TextureFormat::R32Float,
        TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
        "DoFCoC"
    });
    const RGTextureId dofNearTexId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA16Float,
        TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
        "DoFNear"
    });
    const RGTextureId dofFarTexId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA16Float,
        TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
        "DoFFar"
    });
    const RGTextureId dofOutTexId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA16Float,
        TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
        "DoFOut"
    });
    // Motion blur output
    const RGTextureId mbOutTexId = m_graph.createTexture({
        0, 0, TextureFormat::RGBA16Float,
        TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
        "MBOut"
    });
    // Tonemap intermediate (used whenever any post pass follows Tonemap)
    const RGTextureId tonemapOutTexId = m_graph.createTexture({
        0, 0, TextureFormat::BGRA8Unorm,
        TextureUsage::RenderTarget | TextureUsage::ShaderRead,
        "TonemapOut"
    });
    // Colour-grading output intermediate (used when OptFx or FXAA follows CG)
    const RGTextureId cgOutTexId = m_graph.createTexture({
        0, 0, TextureFormat::BGRA8Unorm,
        TextureUsage::RenderTarget | TextureUsage::ShaderRead,
        "CGOut"
    });
    // Optional-FX output intermediate (used when FXAA follows OptFx)
    const RGTextureId optFxOutTexId = m_graph.createTexture({
        0, 0, TextureFormat::BGRA8Unorm,
        TextureUsage::RenderTarget | TextureUsage::ShaderRead,
        "OptFxOut"
    });

    // Import persistent textures
    const RGTextureId taaOutId  = m_graph.importTexture("TAAHistoryCurr",
                                                          m_taaHistory[currHist].get());
    const RGTextureId taaHistId     = m_graph.importTexture("TAAHistoryPrev",
                                                              m_taaHistory[prevHist].get());
    const RGTextureId shadowDepthId = m_graph.importTexture("ShadowDepth",
                                                              m_shadowDepthTex.get());
    const RGTextureId froxelScatterId   = m_graph.importTexture("FroxelScatter",
                                                                  m_froxelScatterTex.get());
    const RGTextureId froxelIntegrateId = m_graph.importTexture("FroxelIntegrate",
                                                                  m_froxelIntegrateTex.get());
    // Import swapchain drawable
    ITexture* drawable = swapchain.nextDrawable();
    const RGTextureId swapId = m_graph.importTexture("Swapchain", drawable);

    // ── Allocate transient textures ───────────────────────────────────────────
    m_graph.compile(device, swapW, swapH);

    // Resolve texture pointers (all valid after compile)
    ITexture* gDepthTex          = m_graph.get(gDepthId);
    ITexture* gDepthCopyTex      = m_graph.get(gDepthCopyId);
    ITexture* froxelScatterTex   = m_froxelScatterTex.get();
    ITexture* froxelIntegrateTex = m_froxelIntegrateTex.get();
    ITexture* gAlbedoTex    = m_graph.get(gAlbedoId);
    ITexture* gNormalTex  = m_graph.get(gNormalId);
    ITexture* gEmissiveTex = m_graph.get(gEmissiveId);
    ITexture* gMotionTex  = m_graph.get(gMotionId);
    ITexture* ssaoTex     = m_graph.get(ssaoId);
    ITexture* ssaoBlurTex = m_graph.get(ssaoBlurId);
    ITexture* hdrTex      = m_graph.get(hdrId);
    ITexture* bloomATex   = m_graph.get(bloomAId);
    ITexture* bloomBTex   = m_graph.get(bloomBId);
    ITexture* ssrOutTex      = m_graph.get(ssrOutId);
    ITexture* cocTex         = m_graph.get(cocTexId);
    ITexture* dofNearTex     = m_graph.get(dofNearTexId);
    ITexture* dofFarTex      = m_graph.get(dofFarTexId);
    ITexture* dofOutTex      = m_graph.get(dofOutTexId);
    ITexture* mbOutTex       = m_graph.get(mbOutTexId);
    ITexture* tonemapOutTex  = m_graph.get(tonemapOutTexId);
    ITexture* cgOutTex       = m_graph.get(cgOutTexId);
    ITexture* optFxOutTex    = m_graph.get(optFxOutTexId);
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
    IPipeline* pTransparent  = m_transparentPSO.get();
    IPipeline* pTAA          = m_taaPSO.get();
    IPipeline* pBloomEx      = m_bloomExtractPSO.get();
    IPipeline* pBloomH       = m_bloomBlurHPSO.get();
    IPipeline* pBloomV       = m_bloomBlurVPSO.get();
    IPipeline* pTonemap      = m_tonemapPSO.get();
    IPipeline* pDecal        = m_decalPSO.get();
    IPipeline* pDepthCopy    = m_depthCopyPSO.get();
    IBuffer*   decalDebugBuf = m_decalDebugBuf.get();

    // Particle pipelines (Pass 6.5)
    IPipeline* pPartEmit     = m_particleEmitPSO.get();
    IPipeline* pPartSimulate = m_particleSimulatePSO.get();
    IPipeline* pPartCompact  = m_particleCompactPSO.get();
    IPipeline* pPartRender   = m_particleRenderPSO.get();

    // Volumetric fog pipelines (Passes 3c, 3d, 6.6)
    IPipeline* pFogScatter   = m_fogScatterPSO.get();
    IPipeline* pFogIntegrate = m_fogIntegratePSO.get();
    IPipeline* pFogComposite = m_fogCompositePSO.get();

    // Screen-space reflections (Pass 7.5)
    IPipeline* pSSR          = m_ssrPSO.get();

    // Depth of field (Passes 15, 15.1, 15.2)
    IPipeline* pDofCoc       = m_dofCocPSO.get();
    IPipeline* pDofBlur      = m_dofBlurPSO.get();
    IPipeline* pDofComposite = m_dofCompositePSO.get();

    // Motion blur (Pass 16)
    IPipeline* pMotionBlur   = m_motionBlurPSO.get();

    // Colour grading (Pass 19)
    IPipeline* pColorGrade    = m_colorGradePSO.get();
    ITexture*  identityLutTex = m_identityLutTex.get();

    // Optional FX (Pass 20) + FXAA (Pass 21)
    IPipeline* pOptFx = m_optionalFxPSO.get();
    IPipeline* pFxaa  = m_fxaaPSO.get();

    // Unit cube GPU mesh (shared by all decal draws)
    IBuffer*   cubeVBO       = m_unitCubeVBO.get();
    IBuffer*   cubeIBO       = m_unitCubeIBO.get();

    // Snapshot the opaque draw list for lambda capture.
    const std::vector<MeshDraw>& draws = scene.meshDraws;

    // Sort transparent draws back-to-front and snapshot for lambda capture.
    // Back-to-front order ensures correct alpha blending without depth writes.
    std::vector<MeshDraw> transparentDraws = scene.transparentDraws;
    {
        const glm::vec3 cam = scene.cameraPos;
        std::sort(transparentDraws.begin(), transparentDraws.end(),
            [&cam](const MeshDraw& a, const MeshDraw& b) noexcept
            {
                const glm::vec3 pa = glm::vec3(a.modelMatrix[3]);
                const glm::vec3 pb = glm::vec3(b.modelMatrix[3]);
                return glm::dot(pa - cam, pa - cam) > glm::dot(pb - cam, pb - cam);
            });
    }

    // Decal draw list (reference valid through m_graph.execute below)
    const std::vector<DecalDraw>& decalDraws = scene.decalDraws;

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
                // Apply per-draw portal scissor (spec §Pass 1 GPU scissor step).
                // Restore the full-viewport scissor after each scissored draw so
                // the next draw's default is always full-screen.
                if (draw.scissorValid)
                    enc->setScissor(draw.scissorRect);

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
                // Note: tint is passed but ignored by the G-buffer shader (opaque geometry).
                const MaterialConstantsGPU matConst{ draw.material.roughness,
                                                     draw.material.metalness,
                                                     draw.material.isMirrorSurface ? 1.0f : 0.0f, 0.0f,
                                                     draw.material.tint,
                                                     draw.material.uvOffset,
                                                     draw.material.uvScale,
                                                     glm::vec4(draw.material.sectorAmbient, 0.0f) };
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

                if (draw.scissorValid)
                    enc->setScissor(sc);  // restore full-viewport scissor
            }
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ──────────────────────────────────────────────────────────────────────────────────────
    // Pass 2.4 — Depth copy (compute): Depth32Float → R32Float
    // Must run AFTER the G-buffer pass (so gDepth contains scene depth) and BEFORE
    // the Decal pass (so gDepthCopy is ready for fragment shader sampling).
    // ──────────────────────────────────────────────────────────────────────────────────────
    {
        RGComputePassDesc p;
        p.name    = "DepthCopy";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pDepthCopy);
            enc->setTexture(gDepthTex,     0);  // source: Depth32Float (ShaderRead)
            enc->setTexture(gDepthCopyTex, 1);  // dest:   R32Float     (ShaderWrite)
            enc->dispatch(swapW, swapH, 1);
        };
        m_graph.addComputePass(std::move(p));
    }

    // ──────────────────────────────────────────────────────────────────────────────────────
    // Pass 2.5 — Deferred Decals (G-buffer alpha blend)
    // Alpha-blends decal albedo and normals into G-buffer RT0 (albedo) and RT1
    // (normal/roughness/metalness).  Depth is tested but NOT written; the depth
    // buffer is also sampled in the fragment shader to reconstruct the surface
    // world-space position for OBB projection.
    // ──────────────────────────────────────────────────────────────────────────────────
    {
        RGRenderPassDesc p;
        p.name             = "Decals";
        p.colorOutputs[0]  = gAlbedoId;
        p.colorOutputs[1]  = gNormalId;
        p.colorOutputCount = 2;
        p.depthOutput      = gDepthId;
        p.loadColors       = true;   // blend into existing G-buffer content
        p.loadDepth        = true;   // test against G-buffer depth (no clear)
        p.execute = [=, &decalDraws](IRenderPassEncoder* enc)
        {
            if (decalDraws.empty()) { return; }

            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pDecal);
            enc->setFragmentSampler(repeatSamp, 0);  // sampler(0): linear-repeat
            // Diagnostic counter: shared by all draws in this pass.
            enc->setFragmentBuffer(decalDebugBuf, 0, 2);  // atomic fragCount  buffer(2)

            for (const DecalDraw& draw : decalDraws)
            {
                DecalConstantsGPU dc;
                dc.model     = draw.modelMatrix;
                dc.invModel  = draw.invModelMatrix;
                dc.roughness = draw.roughness;
                dc.metalness = draw.metalness;
                dc.opacity   = draw.opacity;
                dc.pad       = 0.0f;

                // ── Vertex stage ────────────────────────────────────────────────────
                enc->setVertexBuffer(frameBuf,    0,          0);       // FrameConstants  buffer(0)
                enc->setVertexBytes (&dc, sizeof(dc),         1);       // DecalConstants  buffer(1)
                enc->setVertexBuffer(cubeVBO,     0, k_vboSlot);       // unit cube VBO

                // ── Fragment stage ──────────────────────────────────────────────────
                enc->setFragmentBuffer (frameBuf,                   0, 0);  // FrameConstants  buffer(0)
                enc->setFragmentBytes  (&dc, sizeof(dc),               1);  // DecalConstants  buffer(1)
                enc->setFragmentTexture(gDepthCopyTex,                 0);  // depth copy      texture(0)
                enc->setFragmentTexture(draw.albedoTexture,            1);  // decal albedo    texture(1)
                enc->setFragmentTexture(
                    draw.normalTexture ? draw.normalTexture : fallbackNormal, 2); // decal normal texture(2)

                enc->setIndexBuffer(cubeIBO, 0, true);  // u32 indices (true = 32-bit)
                enc->drawIndexed(36);
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
    // Pass 3c — Fog scatter (compute)  [only if fog.enabled]
    // ─────────────────────────────────────────────────────────────────────────
    if (scene.fog.enabled)
    {
        RGComputePassDesc p;
        p.name    = "FogScatter";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pFogScatter);
            enc->setTexture(froxelScatterTex, 0);         // froxelScatter (write)
            enc->setBuffer (frameBuf,  0,     0);         // FrameConstants   buffer(0)
            enc->setBytes  (&fogConst, sizeof(fogConst), 1); // VolumetricFogConstants  buffer(1)
            enc->setBuffer (lightBuf,  0,     2);         // lightCount       buffer(2)
            enc->setBuffer (lightBuf, 16,     3);         // PointLightGPU[]  buffer(3)
            enc->setBuffer (spotBuf,   0,     4);         // spotCount        buffer(4)
            enc->setBuffer (spotBuf,  16,     5);         // SpotLightGPU[]   buffer(5)
            enc->dispatch  (160, 90, 64);                 // 3D: one thread per froxel
        };
        m_graph.addComputePass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 3d — Fog integrate (compute)  [only if fog.enabled]
    // ─────────────────────────────────────────────────────────────────────────
    if (scene.fog.enabled)
    {
        RGComputePassDesc p;
        p.name    = "FogIntegrate";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pFogIntegrate);
            enc->setTexture(froxelScatterTex,   0);           // froxelScatter   (read)
            enc->setTexture(froxelIntegrateTex, 1);           // froxelIntegrate (write)
            enc->setBytes  (&fogConst, sizeof(fogConst), 0);  // VolumetricFogConstants  buffer(0)
            enc->dispatch  (160, 90, 1);                      // 2D: one thread per column
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
            enc->setBuffer(spotBuf,   0, 3);  // spotCount
            enc->setBuffer(spotBuf,  16, 4);  // SpotLightGPU[]
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

    // ──────────────────────────────────────────────────────────────────────────────────
    // Pass 6 — Transparent (forward PBR, alpha blend, back-to-front sorted)
    // Reads G-buffer depth (test only, no write) and accumulates into HDR.
    // ──────────────────────────────────────────────────────────────────────────────────
    {
        RGRenderPassDesc p;
        p.name             = "Transparent";
        p.colorOutputs[0]  = hdrId;
        p.colorOutputCount = 1;
        p.depthOutput      = gDepthId;
        p.loadColors       = true;  // preserve HDR output from lighting + skybox
        p.loadDepth        = true;  // use G-buffer depth for occlusion testing
        p.execute = [=, &transparentDraws](IRenderPassEncoder* enc)
        {
            if (transparentDraws.empty()) { return; }

            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pTransparent);
            enc->setFragmentSampler(repeatSamp, 0);  // sampler(0): repeat for material textures
            enc->setFragmentSampler(linSamp,    1);  // sampler(1): clamp for shadow depth

            for (const MeshDraw& draw : transparentDraws)
            {
                const ModelGPU modelGPU{
                    draw.modelMatrix,
                    glm::mat4(glm::inverse(glm::transpose(draw.modelMatrix))),
                    draw.prevModel
                };
                // Vertex stage
                enc->setVertexBuffer(frameBuf, 0, 0);
                enc->setVertexBytes(&modelGPU, sizeof(ModelGPU), 1);
                enc->setVertexBuffer(draw.vertexBuffer, 0, k_vboSlot);
                // Fragment stage: frame constants
                enc->setFragmentBuffer(frameBuf, 0, 0);
                // Fragment stage: material constants (roughness, metalness, tint, uv crop)
                const MaterialConstantsGPU matConst{ draw.material.roughness,
                                                     draw.material.metalness,
                                                     draw.material.isMirrorSurface ? 1.0f : 0.0f, 0.0f,
                                                     draw.material.tint,
                                                     draw.material.uvOffset,
                                                     draw.material.uvScale,
                                                     glm::vec4(draw.material.sectorAmbient, 0.0f) };
                enc->setFragmentBytes(&matConst, sizeof(MaterialConstantsGPU), 1);
                // Fragment stage: spot lights
                enc->setFragmentBuffer(spotBuf,  0, 3);  // spotCount
                enc->setFragmentBuffer(spotBuf, 16, 4);  // SpotLightGPU[]
                // Fragment stage: material textures (fallback if nullptr)
                enc->setFragmentTexture(
                    draw.material.albedo    ? draw.material.albedo    : fallbackAlbedo,   0);
                enc->setFragmentTexture(
                    draw.material.normalMap ? draw.material.normalMap : fallbackNormal,   1);
                enc->setFragmentTexture(shadowDepthTex, 2);  // shadow depth atlas
                enc->setFragmentTexture(
                    draw.material.emissive  ? draw.material.emissive  : fallbackEmissive, 3);
                enc->setIndexBuffer(draw.indexBuffer, 0, true);
                enc->drawIndexed(draw.indexCount);
            }
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ──────────────────────────────────────────────────────────────────────────────────────────────────────
    // Pass 6.5 — Particles (compute: emit + simulate + compact; render: drawIndirect)
    // For each emitter: run 3 serial compute passes on the GPU then one GPU-driven
    // render pass.  Blends additively (premultiplied alpha) into the HDR buffer.
    // The depth buffer from Pass 6 is used for soft-particle depth fade.
    // ──────────────────────────────────────────────────────────────────────────────────────────────────────
    for (const ParticleEmitterDraw& emDraw : scene.particleEmitters)
    {
        if (!emDraw.pool || !emDraw.atlasTexture) { continue; }

        ParticlePool* pool = emDraw.pool;

        // Pack constants: bake aliveListFlip from the CPU-side pool state so all
        // shaders this frame agree on which list is read vs. write.
        ParticleEmitterConstantsGPU emConst = emDraw.constants;
        emConst.aliveListFlip = pool->aliveListFlip;
        emConst.maxParticles  = pool->maxParticles;

        // Raw pointers for lambda capture (lifetimes owned by pool + emDraw).
        IBuffer*  stateBuf   = pool->stateBuffer.get();
        IBuffer*  deadBuf    = pool->deadList.get();
        IBuffer*  aliveABuf  = pool->aliveListA.get();
        IBuffer*  aliveBBuf  = pool->aliveListB.get();
        IBuffer*  indirectBuf = pool->indirectArgs.get();
        ITexture* atlasTex   = emDraw.atlasTexture;

        const u32 maxP  = pool->maxParticles;
        const u32 spawn = emConst.spawnThisFrame;

        // ── Emit ───────────────────────────────────────────────────────────────
        // Skip if nothing to spawn this frame (common when emission rate is low).
        if (spawn > 0)
        {
            RGComputePassDesc ep;
            ep.name    = "ParticleEmit";
            ep.execute = [=](IComputePassEncoder* enc)
            {
                enc->setComputePipeline(pPartEmit);
                enc->setBytes  (&emConst,   sizeof(emConst), 0);  // EmitterConstants  buffer(0)
                enc->setBuffer (stateBuf,   0,               1);  // stateBuffer       buffer(1)
                enc->setBuffer (deadBuf,    0,               2);  // deadList          buffer(2)
                enc->setBuffer (aliveABuf,  0,               3);  // aliveListA        buffer(3)
                enc->setBuffer (aliveBBuf,  0,               4);  // aliveListB        buffer(4)
                enc->dispatch  (spawn, 1, 1);
            };
            m_graph.addComputePass(std::move(ep));
        }

        // ── Simulate ───────────────────────────────────────────────────────────
        {
            RGComputePassDesc sp;
            sp.name    = "ParticleSimulate";
            sp.execute = [=](IComputePassEncoder* enc)
            {
                enc->setComputePipeline(pPartSimulate);
                enc->setBytes  (&emConst,  sizeof(emConst), 0);
                enc->setBuffer (stateBuf,  0,               1);
                enc->setBuffer (deadBuf,   0,               2);
                enc->setBuffer (aliveABuf, 0,               3);
                enc->setBuffer (aliveBBuf, 0,               4);
                enc->dispatch  (maxP, 1, 1);
            };
            m_graph.addComputePass(std::move(sp));
        }

        // ── Compact ────────────────────────────────────────────────────────────
        {
            RGComputePassDesc cp;
            cp.name    = "ParticleCompact";
            cp.execute = [=](IComputePassEncoder* enc)
            {
                enc->setComputePipeline(pPartCompact);
                enc->setBytes  (&emConst,    sizeof(emConst), 0);
                enc->setBuffer (stateBuf,    0,               1);
                enc->setBuffer (deadBuf,     0,               2);
                enc->setBuffer (aliveABuf,   0,               3);
                enc->setBuffer (aliveBBuf,   0,               4);
                enc->setBuffer (indirectBuf, 0,               5);  // DrawIndirectArgs buffer(5)
                enc->dispatch  (1, 1, 1);
            };
            m_graph.addComputePass(std::move(cp));
        }

        // ── Draw ───────────────────────────────────────────────────────────────
        {
            RGRenderPassDesc dp;
            dp.name             = "ParticleDraw";
            dp.colorOutputs[0]  = hdrId;
            dp.colorOutputCount = 1;
            dp.depthOutput      = gDepthId;
            dp.loadColors       = true;  // blend into existing HDR
            dp.loadDepth        = true;  // test against opaque G-buffer depth
            dp.execute = [=](IRenderPassEncoder* enc)
            {
                enc->setViewport(vp);
                enc->setScissor(sc);
                enc->setRenderPipeline(pPartRender);

                // Vertex stage
                enc->setVertexBuffer(frameBuf,   0, 0);           // FrameConstants     buffer(0)
                enc->setVertexBytes (&emConst, sizeof(emConst), 1); // EmitterConstants buffer(1)
                enc->setVertexBuffer(stateBuf,   0, 2);           // stateBuffer        buffer(2)
                enc->setVertexBuffer(aliveABuf,  0, 3);           // aliveListA         buffer(3)
                enc->setVertexBuffer(aliveBBuf,  0, 4);           // aliveListB         buffer(4)

                // Fragment stage
                enc->setFragmentBuffer (frameBuf,       0, 0);    // FrameConstants     buffer(0)
                enc->setFragmentBytes  (&emConst, sizeof(emConst), 1); // EmitterConst buffer(1)
                enc->setFragmentTexture(atlasTex,          0);    // atlas              texture(0)
                enc->setFragmentTexture(gDepthCopyTex,     1);    // soft particle depth texture(1)
                enc->setFragmentSampler(linSamp,           0);    // sampler(0)

                enc->drawIndirect(indirectBuf, 0);
            };
            m_graph.addRenderPass(std::move(dp));
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 6.6 — Fog composite (compute)  [only if fog.enabled]
    // In-place: reads hdrTex and writes back fogged result to the same texture.
    // Placed after particles so all opaque + transparent + particle HDR content
    // is fogged before TAA accumulates the final frame.
    // ─────────────────────────────────────────────────────────────────────────
    if (scene.fog.enabled)
    {
        RGComputePassDesc p;
        p.name    = "FogComposite";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pFogComposite);
            enc->setTexture (hdrTex,             0);           // hdrTex          (read_write)
            enc->setTexture (froxelIntegrateTex, 1);           // froxelIntegrate (read/sample)
            enc->setTexture (gDepthCopyTex,      2);           // gDepthCopy      (read)
            enc->setBuffer  (frameBuf,  0,        0);          // FrameConstants  buffer(0)
            enc->setBytes   (&fogConst, sizeof(fogConst), 1);  // VolumetricFogConstants  buffer(1)
            enc->setSampler (linSamp,             0);          // sampler(0): linear clamp
            enc->dispatch   (swapW, swapH, 1);
        };
        m_graph.addComputePass(std::move(p));
    }

    // ──────────────────────────────────────────────────────────────────────────────
    // Pass 7 — TAA
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

    // ───────────────────────────────────────────────────────────────────────────
    // Pass 7.5 — Screen-Space Reflections (compute)  [only if ssr.enabled]
    // Reads gNormal, gDepthCopy, taaOut; writes composited result to ssrOut.
    // postTaaColorTex points to ssrOut when enabled, taaOut when disabled.
    // BloomExtract and Tonemap always read postTaaColorTex (zero GPU cost when off).
    // ───────────────────────────────────────────────────────────────────────────
    if (scene.ssr.enabled)
    {
        RGComputePassDesc p;
        p.name    = "SSR";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pSSR);
            enc->setTexture(gNormalTex,    0);               // gNormal     (read)
            enc->setTexture(gDepthCopyTex, 1);               // gDepthCopy  (read)
            enc->setTexture(taaOutTex,     2);               // sceneColor  (sample)
            enc->setTexture(ssrOutTex,     3);               // ssrOut      (write)
            enc->setBuffer (frameBuf,      0, 0);            // FrameConstants  buffer(0)
            enc->setBytes  (&ssrConst, sizeof(ssrConst), 1); // SSRConstants    buffer(1)
            enc->setSampler(linSamp,       0);               // sampler(0): linear clamp
            enc->dispatch  (swapW, swapH, 1);
        };
        m_graph.addComputePass(std::move(p));
    }

    // Routing pointers — zero-cost bypass chain:
    //   postTaaColorTex  = SSR out (or TAA out when SSR disabled)             [existing]
    //   postDofTex       = DoF composite out (or postTaaColorTex when off)    [new]
    //   preTonemapTex    = MB out (or postDofTex when MB disabled)            [new]
    // Tonemap reads preTonemapTex; Color Grade (if enabled) reads tonemapOut.
    ITexture* postTaaColorTex = scene.ssr.enabled        ? ssrOutTex  : taaOutTex;
    ITexture* postDofTex      = scene.dof.enabled        ? dofOutTex  : postTaaColorTex;
    ITexture* preTonemapTex   = scene.motionBlur.enabled ? mbOutTex   : postDofTex;

    // ───────────────────────────────────────────────────────────────────────────
    // Pass 8 — Bloom extract
    // ───────────────────────────────────────────────────────────────────────────
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
            enc->setFragmentTexture(postTaaColorTex, 0);   // post-TAA (SSR or TAA) source
            enc->setFragmentSampler(linSamp,   0);
            enc->setFragmentBuffer(frameBuf,   0, 0);
            enc->draw(3);
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 9 — Bloom horizontal blur (bloomA → bloomB)
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
    // Pass 10 — Bloom vertical blur (bloomB → bloomA)
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

    // ───────────────────────────────────────────────────────────────────────────
    // Pass 15 — DoF: Circle of Confusion (compute)  [only if dof.enabled]
    // ───────────────────────────────────────────────────────────────────────────
    if (scene.dof.enabled)
    {
        RGComputePassDesc p;
        p.name    = "DoFCoC";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pDofCoc);
            enc->setTexture(gDepthCopyTex, 0);              // gDepthCopy  (read)
            enc->setTexture(cocTex,        1);              // cocOut      (write)
            enc->setBuffer (frameBuf,      0, 0);           // FrameConstants  buffer(0)
            enc->setBytes  (&dofConst, sizeof(dofConst), 1); // DoFConstants   buffer(1)
            enc->dispatch  (swapW, swapH, 1);
        };
        m_graph.addComputePass(std::move(p));
    }

    // ───────────────────────────────────────────────────────────────────────────
    // Pass 15.1 — DoF: Poisson-disk blur (compute)  [only if dof.enabled]
    // ───────────────────────────────────────────────────────────────────────────
    if (scene.dof.enabled)
    {
        RGComputePassDesc p;
        p.name    = "DoFBlur";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pDofBlur);
            enc->setTexture(postTaaColorTex, 0);              // sceneColor  (sample)
            enc->setTexture(cocTex,          1);              // cocTex      (read)
            enc->setTexture(dofFarTex,       2);              // dofFarTex   (write)
            enc->setTexture(dofNearTex,      3);              // dofNearTex  (write)
            enc->setBuffer (frameBuf,        0, 0);           // FrameConstants  buffer(0)
            enc->setBytes  (&dofConst, sizeof(dofConst), 1);  // DoFConstants    buffer(1)
            enc->setSampler(linSamp,         0);              // sampler(0): linear clamp
            enc->dispatch  (swapW, swapH, 1);
        };
        m_graph.addComputePass(std::move(p));
    }

    // ───────────────────────────────────────────────────────────────────────────
    // Pass 15.2 — DoF: composite near/far/scene (compute)  [only if dof.enabled]
    // ───────────────────────────────────────────────────────────────────────────
    if (scene.dof.enabled)
    {
        RGComputePassDesc p;
        p.name    = "DoFComposite";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pDofComposite);
            enc->setTexture(postTaaColorTex, 0);              // sceneColor  (sample)
            enc->setTexture(dofNearTex,      1);              // dofNearTex  (read)
            enc->setTexture(dofFarTex,       2);              // dofFarTex   (read)
            enc->setTexture(cocTex,          3);              // cocTex      (read)
            enc->setTexture(dofOutTex,       4);              // dofOut      (write)
            enc->setBuffer (frameBuf,        0, 0);           // FrameConstants  buffer(0)
            enc->setBytes  (&dofConst, sizeof(dofConst), 1);  // DoFConstants    buffer(1)
            enc->setSampler(linSamp,         0);              // sampler(0): linear clamp
            enc->dispatch  (swapW, swapH, 1);
        };
        m_graph.addComputePass(std::move(p));
    }

    // ───────────────────────────────────────────────────────────────────────────
    // Pass 16 — Motion Blur (compute)  [only if motionBlur.enabled]
    // Reads preTonemapTex (postDofTex or postTaaColorTex) as source.
    // ───────────────────────────────────────────────────────────────────────────
    if (scene.motionBlur.enabled)
    {
        RGComputePassDesc p;
        p.name    = "MotionBlur";
        p.execute = [=](IComputePassEncoder* enc)
        {
            enc->setComputePipeline(pMotionBlur);
            enc->setTexture(postDofTex,  0);              // sceneColor  (sample, post-DoF)
            enc->setTexture(gMotionTex,  1);              // gMotionTex  (read)
            enc->setTexture(mbOutTex,    2);              // mbOut       (write)
            enc->setBuffer (frameBuf,    0, 0);           // FrameConstants       buffer(0)
            enc->setBytes  (&mbConst, sizeof(mbConst), 1); // MotionBlurConstants  buffer(1)
            enc->setSampler(linSamp,     0);              // sampler(0): linear clamp
            enc->dispatch  (swapW, swapH, 1);
        };
        m_graph.addComputePass(std::move(p));
    }

    // ── Extended post-chain routing ───────────────────────────────────────────────────────────────────
    // Chain: Tonemap → [CG] → [OptFx] → [FXAA] → swapchain.
    // Each pass writes to an intermediate when at least one pass follows it,
    // or directly to the swapchain when it is the last enabled pass.
    const bool cgEnabled    = scene.colorGrading.enabled;
    const bool optFxEnabled = scene.optionalFx.enabled;
    const bool fxaaEnabled  = (scene.upscaling.mode == UpscalingMode::FXAA);

    const bool anythingAfterTonemap = cgEnabled || optFxEnabled || fxaaEnabled;
    const bool anythingAfterCG      = optFxEnabled || fxaaEnabled;
    const bool anythingAfterOptFx   = fxaaEnabled;

    const RGTextureId tonemapTargetId = anythingAfterTonemap ? tonemapOutTexId : swapId;
    const RGTextureId cgTargetId      = anythingAfterCG      ? cgOutTexId      : swapId;
    const RGTextureId optFxTargetId   = anythingAfterOptFx   ? optFxOutTexId   : swapId;

    // Per-pass read sources (each pass reads the previous pass's output)
    // CG reads tonemapOut; OptFx reads CG out (or tonemapOut); FXAA reads OptFx out (or prior)
    ITexture* optFxReadTex  = cgEnabled    ? cgOutTex    : tonemapOutTex;
    ITexture* fxaaReadTex   = optFxEnabled ? optFxOutTex : optFxReadTex;

    // ───────────────────────────────────────────────────────────────────────────
    // Pass 17 — Tone mapping (render → tonemapOut or swapchain)
    // ───────────────────────────────────────────────────────────────────────────
    {
        RGRenderPassDesc p;
        p.name             = "ToneMapping";
        p.colorOutputs[0]  = tonemapTargetId;   // tonemapOut or swapchain
        p.colorOutputCount = 1;
        p.execute = [=](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pTonemap);
            enc->setFragmentTexture(preTonemapTex, 0);  // post-DoF / post-MB / post-SSR HDR
            enc->setFragmentTexture(bloomATex,     1);  // bloom result
            enc->setFragmentSampler(linSamp,       0);
            enc->setFragmentBuffer(frameBuf,       0, 0);
            enc->draw(3);
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ───────────────────────────────────────────────────────────────────────────
    // Pass 19 — Colour Grading (render → cgOut or swapchain)  [only if colorGrading.enabled]
    // Reads tonemapOut + 3D LUT.  Writes to intermediate when OptFx or FXAA follows.
    // ───────────────────────────────────────────────────────────────────────────
    if (cgEnabled)
    {
        // Use the application-supplied LUT if provided; otherwise fall back to identity.
        ITexture* lutTex = scene.colorGrading.lutTexture
                         ? scene.colorGrading.lutTexture
                         : identityLutTex;

        RGRenderPassDesc p;
        p.name             = "ColorGrading";
        p.colorOutputs[0]  = cgTargetId;    // cgOut (intermediate) or swapchain
        p.colorOutputCount = 1;
        p.execute = [=](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pColorGrade);
            enc->setFragmentTexture(tonemapOutTex, 0);             // tonemapped input
            enc->setFragmentTexture(lutTex,        1);             // 3D LUT
            enc->setFragmentSampler(linSamp,       0);             // sampler(0)
            enc->setFragmentBytes  (&cgConst, sizeof(cgConst), 1); // ColorGradingConstants buffer(1)
            enc->draw(3);
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ───────────────────────────────────────────────────────────────────────────
    // Pass 20 — Optional FX (render → optFxOut or swapchain)  [only if optionalFx.enabled]
    // Applies vignette, film grain, and chromatic aberration in one fullscreen pass.
    // ───────────────────────────────────────────────────────────────────────────
    if (optFxEnabled)
    {
        RGRenderPassDesc p;
        p.name             = "OptionalFX";
        p.colorOutputs[0]  = optFxTargetId;  // optFxOut (intermediate) or swapchain
        p.colorOutputCount = 1;
        p.execute = [=](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pOptFx);
            enc->setFragmentTexture(optFxReadTex, 0);                    // LDR input  texture(0)
            enc->setFragmentBuffer (frameBuf,     0,              0);    // FrameConstants  buffer(0)
            enc->setFragmentBytes  (&optFxConst, sizeof(optFxConst), 1); // OptFxConstants  buffer(1)
            enc->setFragmentSampler(linSamp,      0);                    // sampler(0)
            enc->draw(3);
        };
        m_graph.addRenderPass(std::move(p));
    }

    // ───────────────────────────────────────────────────────────────────────────
    // Pass 21 — FXAA (render → swapchain)  [only if upscaling.mode == FXAA]
    // 9-tap luma-based edge-smoothing pass.  Always the last pass in the chain.
    // ───────────────────────────────────────────────────────────────────────────
    if (fxaaEnabled)
    {
        RGRenderPassDesc p;
        p.name             = "FXAA";
        p.colorOutputs[0]  = swapId;  // FXAA always writes directly to swapchain
        p.colorOutputCount = 1;
        p.execute = [=](IRenderPassEncoder* enc)
        {
            enc->setViewport(vp);
            enc->setScissor(sc);
            enc->setRenderPipeline(pFxaa);
            enc->setFragmentTexture(fxaaReadTex, 0);  // LDR input     texture(0)
            enc->setFragmentBuffer (frameBuf,    0, 0); // FrameConstants buffer(0)
            enc->setFragmentSampler(linSamp,     0);  // sampler(0)
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

    // ── Toggle particle alive-list flip for next frame ────────────────────────
    // Each emitter's simulate kernel read from one list and wrote to the other.
    // Flip tells next frame's shaders which list is now the "current" alive list.
    for (const ParticleEmitterDraw& emDraw : scene.particleEmitters)
    {
        if (emDraw.pool)
            emDraw.pool->aliveListFlip ^= 1u;
    }

    ++m_frameIndex;
}

// ─── renderMirrorPrepass ─────────────────────────────────────────────────────
// Renders each MirrorDraw's reflectedDraws list into mirror.renderTarget using a
// lightweight G-buffer → Lighting → Skybox → Tonemap sub-pipeline.  SSAO is
// bypassed: m_fallbackSsao (a 1×1 white R32Float) is bound at slot 3 instead.

void FrameRenderer::renderMirrorPrepass(IRenderDevice& device,
                                         ICommandQueue& queue,
                                         const SceneView& scene,
                                         const FrameGPU&  mainFrame)
{
    for (const MirrorDraw& mirror : scene.mirrors)
    {
        if (!mirror.renderTarget)          { continue; }
        if (mirror.reflectedDraws.empty()) { continue; }

        const u32 rtW = mirror.rtWidth;
        const u32 rtH = mirror.rtHeight;

        // ── Build reflected FrameGPU ───────────────────────────────────────────────────────────
        FrameGPU mf       = mainFrame;  // inherit sun direction, shadow VP, time, etc.
        mf.view           = mirror.reflectedView;
        mf.proj           = mirror.reflectedProj;
        mf.viewProj       = mirror.reflectedProj * mirror.reflectedView;
        mf.invViewProj    = glm::inverse(mf.viewProj);
        mf.prevViewProj   = mf.viewProj;   // no TAA / motion blur in mirror
        mf.jitter         = glm::vec2(0.0f);
        mf.screenSize     = glm::vec4(
            static_cast<f32>(rtW), static_cast<f32>(rtH),
            1.0f / static_cast<f32>(rtW), 1.0f / static_cast<f32>(rtH));

        // Camera position / direction from the inverse of the reflected view matrix.
        // Column 3 of invView is the world-space eye position;
        // column 2 is the camera +Z (backward) axis, so -column2 is forward.
        const glm::mat4 invView = glm::inverse(mirror.reflectedView);
        mf.cameraPos = glm::vec4(glm::vec3(invView[3]),   0.0f);
        mf.cameraDir = glm::vec4(-glm::vec3(invView[2]),  0.0f);

        {
            void* p = m_mirrorFrameConstBuf->map();
            std::memcpy(p, &mf, sizeof(FrameGPU));
            m_mirrorFrameConstBuf->unmap();
        }

        // ── Build mirror render graph ────────────────────────────────────────────────────────────
        m_mirrorGraph.reset();

        // Transients — explicit sizes so compile’s fallback resolution is never used.
        const RGTextureId mDepthId = m_mirrorGraph.createTexture({
            rtW, rtH, TextureFormat::Depth32Float,
            TextureUsage::DepthStencil | TextureUsage::ShaderRead,
            "MirrorDepth"
        });
        const RGTextureId mAlbedoId = m_mirrorGraph.createTexture({
            rtW, rtH, TextureFormat::RGBA8Unorm,
            TextureUsage::RenderTarget | TextureUsage::ShaderRead,
            "MirrorAlbedo"
        });
        const RGTextureId mNormalId = m_mirrorGraph.createTexture({
            rtW, rtH, TextureFormat::RGBA8Unorm,
            TextureUsage::RenderTarget | TextureUsage::ShaderRead,
            "MirrorNormal"
        });
        const RGTextureId mEmissiveId = m_mirrorGraph.createTexture({
            rtW, rtH, TextureFormat::RGBA16Float,
            TextureUsage::RenderTarget | TextureUsage::ShaderRead,
            "MirrorEmissive"
        });
        const RGTextureId mMotionId = m_mirrorGraph.createTexture({
            rtW, rtH, TextureFormat::RG16Float,
            TextureUsage::RenderTarget | TextureUsage::ShaderRead,
            "MirrorMotion"       // written by G-buffer, not sampled; needed to match 4-RT PSO
        });
        const RGTextureId mHdrId = m_mirrorGraph.createTexture({
            rtW, rtH, TextureFormat::RGBA16Float,
            TextureUsage::RenderTarget | TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
            "MirrorHDR"
        });
        const RGTextureId mFinalId = m_mirrorGraph.importTexture("MirrorRT",
                                                                    mirror.renderTarget);
        // Depth copy for soft-particle fade in the mirror particle pass
        const RGTextureId mDepthCopyId = m_mirrorGraph.createTexture({
            rtW, rtH, TextureFormat::R32Float,
            TextureUsage::ShaderRead | TextureUsage::ShaderWrite,
            "MirrorDepthCopy"
        });

        m_mirrorGraph.compile(device, rtW, rtH);

        ITexture* mDepthTex    = m_mirrorGraph.get(mDepthId);
        ITexture* mDepthCopyTex = m_mirrorGraph.get(mDepthCopyId);
        ITexture* mAlbedoTex   = m_mirrorGraph.get(mAlbedoId);
        ITexture* mNormalTex   = m_mirrorGraph.get(mNormalId);
        ITexture* mEmissiveTex = m_mirrorGraph.get(mEmissiveId);
        ITexture* mHdrTex      = m_mirrorGraph.get(mHdrId);

        // Captured for lambda use
        IBuffer*   mFrameBuf      = m_mirrorFrameConstBuf.get();
        IBuffer*   mLightBuf      = m_lightBuf.get();
        IBuffer*   mSpotBuf       = m_spotLightBuf.get();
        IPipeline* pMirGBuf       = m_mirrorGbufferPSO.get();
        IPipeline* pMirLight      = m_lightingPSO.get();
        IPipeline* pMirSkybox     = m_skyboxPSO.get();
        IPipeline* pMirTonemap    = m_tonemapPSO.get();
        IPipeline* pMirDepthCopy  = m_depthCopyPSO.get();
        IPipeline* pMirPartRender = m_particleRenderPSO.get();
        ISampler*  mRepeatSamp    = m_linearRepeatSampler.get();
        ISampler*  mLinSamp       = m_linearClampSampler.get();
        ITexture*  mShadowTex     = m_shadowDepthTex.get();
        ITexture*  mFallSsao      = m_fallbackSsao.get();
        ITexture*  mFallAlbedo    = m_fallbackAlbedo.get();
        ITexture*  mFallNormal    = m_fallbackNormal.get();
        ITexture*  mFallEmit      = m_fallbackEmissive.get();

        const Viewport    mirVP{ 0.f, 0.f, static_cast<f32>(rtW), static_cast<f32>(rtH) };
        const ScissorRect mirSC{ 0, 0, rtW, rtH };

        const std::vector<MeshDraw>& reflDraws = mirror.reflectedDraws;

        // Pass A — G-buffer (CullMode::None via m_mirrorGbufferPSO)
        {
            RGRenderPassDesc p;
            p.name             = "MirrorGBuffer";
            p.colorOutputs[0]  = mAlbedoId;
            p.colorOutputs[1]  = mNormalId;
            p.colorOutputs[2]  = mEmissiveId;
            p.colorOutputs[3]  = mMotionId;
            p.colorOutputCount = 4;
            p.depthOutput      = mDepthId;
            p.clearDepth       = 1.0f;
            p.execute = [=, &reflDraws](IRenderPassEncoder* enc)
            {
                enc->setViewport(mirVP);
                enc->setScissor(mirSC);
                enc->setRenderPipeline(pMirGBuf);
                enc->setFragmentSampler(mRepeatSamp, 0);
                for (const MeshDraw& draw : reflDraws)
                {
                    const ModelGPU modelGPU{
                        draw.modelMatrix,
                        glm::mat4(glm::inverse(glm::transpose(draw.modelMatrix))),
                        draw.prevModel
                    };
                    enc->setVertexBuffer(mFrameBuf,         0,          0);
                    enc->setVertexBytes (&modelGPU, sizeof(ModelGPU),   1);
                    enc->setVertexBuffer(draw.vertexBuffer, 0, k_vboSlot);
                    enc->setFragmentBuffer(mFrameBuf,       0, 0);
                    const MaterialConstantsGPU matConst{
                        draw.material.roughness, draw.material.metalness,
                        0.0f, 0.0f,
                        draw.material.tint, draw.material.uvOffset, draw.material.uvScale,
                        glm::vec4(draw.material.sectorAmbient, 0.0f)
                    };
                    enc->setFragmentBytes(&matConst, sizeof(MaterialConstantsGPU), 1);
                    enc->setFragmentTexture(
                        draw.material.albedo    ? draw.material.albedo    : mFallAlbedo, 0);
                    enc->setFragmentTexture(
                        draw.material.normalMap ? draw.material.normalMap : mFallNormal, 1);
                    enc->setFragmentTexture(
                        draw.material.emissive  ? draw.material.emissive  : mFallEmit,  2);
                    enc->setIndexBuffer(draw.indexBuffer, 0, true);
                    enc->drawIndexed(draw.indexCount);
                }
            };
            m_mirrorGraph.addRenderPass(std::move(p));
        }

        // Pass B — Deferred lighting (no SSAO: white fallback at slot 3)
        {
            RGComputePassDesc p;
            p.name    = "MirrorLighting";
            p.execute = [=](IComputePassEncoder* enc)
            {
                enc->setComputePipeline(pMirLight);
                enc->setTexture(mAlbedoTex,   0);    // gAlbedoAO
                enc->setTexture(mNormalTex,   1);    // gNormalRoughMet
                enc->setTexture(mDepthTex,    2);    // gDepth
                enc->setTexture(mFallSsao,    3);    // ssaoTex (white = no occlusion)
                enc->setTexture(mHdrTex,      4);    // hdrOut  (write)
                enc->setTexture(mShadowTex,   5);    // shadow depth (PCF)
                enc->setTexture(mEmissiveTex, 6);    // gEmissive
                enc->setBuffer(mFrameBuf, 0, 0);
                enc->setBuffer(mLightBuf, 0, 1);     // lightCount at offset 0
                enc->setBuffer(mLightBuf,16, 2);     // PointLightGPU[] at offset 16
                enc->setBuffer(mSpotBuf,  0, 3);     // spotCount
                enc->setBuffer(mSpotBuf, 16, 4);     // SpotLightGPU[]
                enc->dispatch(rtW, rtH, 1);
            };
            m_mirrorGraph.addComputePass(std::move(p));
        }

        // Pass C — Skybox (sky pixels only, depth test LessEqual)
        {
            RGRenderPassDesc p;
            p.name             = "MirrorSkybox";
            p.colorOutputs[0]  = mHdrId;
            p.colorOutputCount = 1;
            p.depthOutput      = mDepthId;
            p.loadColors       = true;
            p.loadDepth        = true;
            p.execute = [=](IRenderPassEncoder* enc)
            {
                enc->setViewport(mirVP);
                enc->setScissor(mirSC);
                enc->setRenderPipeline(pMirSkybox);
                enc->setFragmentBuffer(mFrameBuf, 0, 0);
                enc->draw(3);
            };
            m_mirrorGraph.addRenderPass(std::move(p));
        }

        // Pass C.5 — Depth copy (Depth32Float → R32Float) for soft-particle fade
        {
            RGComputePassDesc p;
            p.name    = "MirrorDepthCopy";
            p.execute = [=](IComputePassEncoder* enc)
            {
                enc->setComputePipeline(pMirDepthCopy);
                enc->setTexture(mDepthTex,     0);  // source Depth32Float
                enc->setTexture(mDepthCopyTex, 1);  // dest   R32Float
                enc->dispatch(rtW, rtH, 1);
            };
            m_mirrorGraph.addComputePass(std::move(p));
        }

        // Pass C.75 — Particles (render-only; uses previous frame's simulate/compact data).
        // aliveListFlip is XOR'd to address the write-list that compact already counted
        // in indirectArgs->instanceCount at the end of the previous frame.
        for (const ParticleEmitterDraw& emDraw : scene.particleEmitters)
        {
            if (!emDraw.pool || !emDraw.atlasTexture) { continue; }

            ParticlePool* pool = emDraw.pool;

            ParticleEmitterConstantsGPU mirEmConst = emDraw.constants;
            mirEmConst.aliveListFlip = pool->aliveListFlip ^ 1u;
            mirEmConst.maxParticles  = pool->maxParticles;

            IBuffer*  stateBuf    = pool->stateBuffer.get();
            IBuffer*  aliveABuf   = pool->aliveListA.get();
            IBuffer*  aliveBBuf   = pool->aliveListB.get();
            IBuffer*  indirectBuf = pool->indirectArgs.get();
            ITexture* atlasTex    = emDraw.atlasTexture;

            RGRenderPassDesc dp;
            dp.name             = "MirrorParticleDraw";
            dp.colorOutputs[0]  = mHdrId;
            dp.colorOutputCount = 1;
            dp.depthOutput      = mDepthId;
            dp.loadColors       = true;
            dp.loadDepth        = true;
            dp.execute = [=](IRenderPassEncoder* enc)
            {
                enc->setViewport(mirVP);
                enc->setScissor(mirSC);
                enc->setRenderPipeline(pMirPartRender);

                enc->setVertexBuffer(mFrameBuf,  0, 0);
                enc->setVertexBytes (&mirEmConst, sizeof(mirEmConst), 1);
                enc->setVertexBuffer(stateBuf,   0, 2);
                enc->setVertexBuffer(aliveABuf,  0, 3);
                enc->setVertexBuffer(aliveBBuf,  0, 4);

                enc->setFragmentBuffer (mFrameBuf,       0, 0);
                enc->setFragmentBytes  (&mirEmConst, sizeof(mirEmConst), 1);
                enc->setFragmentTexture(atlasTex,         0);
                enc->setFragmentTexture(mDepthCopyTex,    1);
                enc->setFragmentSampler(mLinSamp,         0);

                enc->drawIndirect(indirectBuf, 0);
            };
            m_mirrorGraph.addRenderPass(std::move(dp));
        }

        // Pass D — Tonemap → mirror.renderTarget (BGRA8Unorm)
        // Bloom is bypassed: mFallEmit (black 1×1) contributes nothing.
        {
            RGRenderPassDesc p;
            p.name             = "MirrorTonemap";
            p.colorOutputs[0]  = mFinalId;
            p.colorOutputCount = 1;
            p.execute = [=](IRenderPassEncoder* enc)
            {
                enc->setViewport(mirVP);
                enc->setScissor(mirSC);
                enc->setRenderPipeline(pMirTonemap);
                enc->setFragmentTexture(mHdrTex,  0);   // HDR source
                enc->setFragmentTexture(mFallEmit, 1);  // bloom: black (no bloom)
                enc->setFragmentSampler(mLinSamp,  0);
                enc->setFragmentBuffer (mFrameBuf, 0, 0);
                enc->draw(3);
            };
            m_mirrorGraph.addRenderPass(std::move(p));
        }

        // Submit mirror pre-pass on its own command buffer before the main frame.
        auto mirCmd = queue.createCommandBuffer("MirrorPrepass");
        mirCmd->pushDebugGroup("MirrorPrepass");
        m_mirrorGraph.execute(*mirCmd);
        mirCmd->popDebugGroup();
        mirCmd->commit();
    }
}

} // namespace daedalus::render
