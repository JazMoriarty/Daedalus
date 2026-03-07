// frame_renderer.h
// Orchestrates the full deferred rendering pipeline for one frame.
//
// Pass order:
//   1.   Shadow depth   (render, depth-only from sun POV — 2048×2048)
//   2.   G-buffer       (render, depth + colour in one pass)
//   2.4  DepthCopy      (compute, Depth32Float → R32Float — breaks decal feedback loop)
//   2.5  Decal          (render, alpha-blend into G-buffer RT0+RT1)
//   3.   SSAO           (compute)
//   3b.  SSAOBlur       (compute, bilateral 5×5 depth-aware filter)
//   3c.  FogScatter     (compute, scatter lights into 160×90×64 froxel grid; only if fog.enabled)
//   3d.  FogIntegrate   (compute, front-to-back column integration; only if fog.enabled)
//   4.   Lighting       (compute, deferred PBR + PCF shadow)
//   5.   Skybox         (render, procedural sky on background pixels)
//   6.   Transparent    (render, forward PBR + alpha blend, sorted back-to-front)
//   6.5  Particles      (compute: emit + simulate + compact; render: drawIndirect)
//   6.6  FogComposite   (compute, in-place HDR × transmittance + scatter; only if fog.enabled)
//   7.   TAA            (render, temporal anti-aliasing)
//   7.5  SSR            (compute, screen-space reflections; only if ssr.enabled)
//   8.   Bloom extract  (render)
//   9.   Bloom blur H   (render)
//  10.   Bloom blur V   (render)
//  15.   DoF CoC        (compute, circle of confusion; only if dof.enabled)
//  15.1  DoF Blur       (compute, Poisson-disk near/far layers; only if dof.enabled)
//  15.2  DoF Composite  (compute, blend near/far/scene; only if dof.enabled)
//  16.   Motion Blur    (compute, velocity-cone accumulation; only if motionBlur.enabled)
//  17.   Tone mapping   (render → tonemapOut, or → swapchain when colorGrading disabled)
//  19.   Colour Grading (render, 3D LUT grade → swapchain; only if colorGrading.enabled)

#pragma once

#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_command_queue.h"
#include "daedalus/render/rhi/i_swapchain.h"
#include "daedalus/render/render_graph/render_graph.h"
#include "daedalus/render/scene_view.h"
#include "daedalus/core/types.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace daedalus::render
{

class FrameRenderer
{
public:
    FrameRenderer()  = default;
    ~FrameRenderer() = default;

    FrameRenderer(const FrameRenderer&)            = delete;
    FrameRenderer& operator=(const FrameRenderer&) = delete;

    // ─── Lifecycle ────────────────────────────────────────────────────────────

    /// Load shaders from `shaderLibPath` (.metallib), create all PSOs,
    /// and allocate persistent GPU resources.
    /// Geometry is supplied per-frame via SceneView::meshDraws.
    void initialize(rhi::IRenderDevice& device,
                    const std::string&  shaderLibPath,
                    u32                 width,
                    u32                 height);

    /// Recreate resolution-dependent resources (TAA history, etc.).
    void resize(rhi::IRenderDevice& device, u32 width, u32 height);

    // ─── Per-frame entry point ────────────────────────────────────────────────

    /// Record and submit one full frame.
    /// `scene`  — per-frame camera + lights (geometry is handled internally).
    /// `swapW/H`— current swapchain dimensions (may differ from init if resized).
    void renderFrame(rhi::IRenderDevice& device,
                     rhi::ICommandQueue& queue,
                     rhi::ISwapchain&    swapchain,
                     const SceneView&    scene,
                     u32                 swapW,
                     u32                 swapH);

private:
    // ─── Render pipelines ─────────────────────────────────────────────────────

    std::unique_ptr<rhi::IPipeline> m_gbufferPSO;
    std::unique_ptr<rhi::IPipeline> m_shadowDepthPSO;
    std::unique_ptr<rhi::IPipeline> m_decalPSO;     ///< Pass 2.5: alpha-blend into G-buffer RT0+RT1.
    std::unique_ptr<rhi::IPipeline> m_ssaoPSO;
    std::unique_ptr<rhi::IPipeline> m_ssaoBlurPSO;
    std::unique_ptr<rhi::IPipeline> m_lightingPSO;
    std::unique_ptr<rhi::IPipeline> m_skyboxPSO;
    std::unique_ptr<rhi::IPipeline> m_transparentPSO;  ///< Pass 6: forward PBR alpha-blend.
    std::unique_ptr<rhi::IPipeline> m_taaPSO;
    std::unique_ptr<rhi::IPipeline> m_bloomExtractPSO;
    std::unique_ptr<rhi::IPipeline> m_bloomBlurHPSO;
    std::unique_ptr<rhi::IPipeline> m_bloomBlurVPSO;
    std::unique_ptr<rhi::IPipeline> m_tonemapPSO;
    std::unique_ptr<rhi::IPipeline> m_depthCopyPSO;  ///< Pass 2.4: Depth32Float → R32Float copy (compute).

    // ── Particle pipelines (Pass 6.5) ───────────────────────────────────────────────────────

    std::unique_ptr<rhi::IPipeline> m_particleEmitPSO;      ///< Compute: spawns new particles from dead list.
    std::unique_ptr<rhi::IPipeline> m_particleSimulatePSO;  ///< Compute: physics, curl noise, lifetime.
    std::unique_ptr<rhi::IPipeline> m_particleCompactPSO;   ///< Compute: writes DrawIndirectArgs, resets read list.
    std::unique_ptr<rhi::IPipeline> m_particleRenderPSO;    ///< Render: GPU-driven procedural quad draw.

    // ── Volumetric fog pipelines (Passes 3c, 3d, 6.6) ──────────────────────────────────
    // All three passes are skipped entirely when SceneView::fog.enabled is false.

    std::unique_ptr<rhi::IPipeline> m_fogScatterPSO;    ///< Compute: scatter lights into 160×90×64 froxel grid.
    std::unique_ptr<rhi::IPipeline> m_fogIntegratePSO;  ///< Compute: front-to-back column integration.
    std::unique_ptr<rhi::IPipeline> m_fogCompositePSO;  ///< Compute: in-place HDR × transmittance + scatter.

    // ── Screen-space reflections pipeline (Pass 7.5) ──────────────────────────────────────────────────────
    // Skipped entirely when SceneView::ssr.enabled is false (zero GPU cost).

    std::unique_ptr<rhi::IPipeline> m_ssrPSO;  ///< Compute: world-space ray march, Fresnel blend.

    // ── Depth-of-field pipelines (Passes 15, 15.1, 15.2) ────────────────────────────────────────────
    // All three passes are skipped when SceneView::dof.enabled is false.

    std::unique_ptr<rhi::IPipeline> m_dofCocPSO;        ///< Compute: depth → signed CoC.
    std::unique_ptr<rhi::IPipeline> m_dofBlurPSO;       ///< Compute: Poisson-disk near/far blur.
    std::unique_ptr<rhi::IPipeline> m_dofCompositePSO;  ///< Compute: blend near/far layers.

    // ── Motion blur pipeline (Pass 16) ────────────────────────────────────────────────────────────
    // Skipped when SceneView::motionBlur.enabled is false (zero GPU cost).

    std::unique_ptr<rhi::IPipeline> m_motionBlurPSO;    ///< Compute: velocity-cone accumulation.

    // ── Colour grading pipeline (Pass 19) ──────────────────────────────────────────────────────
    // Skipped when SceneView::colorGrading.enabled is false; in that case
    // Tonemap writes directly to the swapchain (zero extra cost).

    std::unique_ptr<rhi::IPipeline> m_colorGradePSO;    ///< Render: 3D LUT grade → swapchain.

    // ─── Froxel 3D textures (persistent, reused across frames) ────────────────────────────────
    // RGBA16Float, 160×90×64.  Allocated in createPersistentResources; imported
    // into the render graph every frame so the graph can execute the fog passes.

    std::unique_ptr<rhi::ITexture> m_froxelScatterTex;    ///< Per-froxel (in-scatter.rgb, extinction.a).
    std::unique_ptr<rhi::ITexture> m_froxelIntegrateTex;  ///< Integrated (accumulated scatter.rgb, transmittance.a).

    // ─── Identity LUT (persistent, colour grading fallback) ─────────────────────────────────
    // 32×32×32 RGBA8Unorm 3D texture.  Populated once in createPersistentResources
    // with a procedural identity LUT.  Bound at texture(1) in the colour grading
    // pass when SceneView::colorGrading.lutTexture is nullptr.

    std::unique_ptr<rhi::ITexture> m_identityLutTex;  ///< Passthrough 3D LUT (r=R, g=G, b=B).

    // ─── Unit cube mesh (shared by all decal draws) ─────────────────────────────────

    std::unique_ptr<rhi::IBuffer> m_unitCubeVBO;  ///< 8 StaticMeshVertex positions (384 B)
    std::unique_ptr<rhi::IBuffer> m_unitCubeIBO;  ///< 36 u32 indices, CCW front-faces

    // ─── Per-frame GPU buffers ────────────────────────────────────────────────

    std::unique_ptr<rhi::IBuffer> m_frameConstBuf;  ///< FrameGPU  (512 B)
    std::unique_ptr<rhi::IBuffer> m_lightBuf;       ///< u32 header + PointLightGPU[]

    /// Spot light constant buffer (SpotLightGPU, 64 B) — uploaded every frame.
    std::unique_ptr<rhi::IBuffer> m_spotLightBuf;

    /// 1×u32 atomic fragment counter for decal diagnostics.
    /// Zeroed each frame by CPU; incremented by decal_frag for every live fragment.
    /// CPU reads on frame N+1 to report how many decal fragments rendered on frame N.
    std::unique_ptr<rhi::IBuffer> m_decalDebugBuf;

    // ─── Persistent textures ─────────────────────────────────────────────────

    /// TAA history ping-pong: [even frame writes to 0, odd frame writes to 1].
    std::unique_ptr<rhi::ITexture> m_taaHistory[2];

    /// Sun shadow depth map (2048×2048 Depth32Float, persistent).
    std::unique_ptr<rhi::ITexture> m_shadowDepthTex;

    /// 1×1 fallback textures bound when a MeshDraw material slot is nullptr.
    std::unique_ptr<rhi::ITexture> m_fallbackAlbedo;    ///< Opaque white  (RGBA8Unorm)
    std::unique_ptr<rhi::ITexture> m_fallbackNormal;    ///< Flat normal   (RGBA8Unorm, 128,128,255,255)
    std::unique_ptr<rhi::ITexture> m_fallbackEmissive;  ///< Black emissive (RGBA8Unorm)

    // ─── Samplers ─────────────────────────────────────────────────────────────

    std::unique_ptr<rhi::ISampler> m_linearClampSampler;
    std::unique_ptr<rhi::ISampler> m_linearRepeatSampler; ///< Used by G-buffer texture sampling.

    // ─── Render graph ─────────────────────────────────────────────────────────

    RenderGraph m_graph;

    // ─── Frame state ──────────────────────────────────────────────────────────

    u32 m_width      = 0;
    u32 m_height     = 0;
    u32 m_frameIndex = 0;

    // ─── Internal helpers ─────────────────────────────────────────────────────

    void createPSOs(rhi::IRenderDevice& device, const std::string& libPath);
    void createPersistentResources(rhi::IRenderDevice& device, u32 w, u32 h);
    void recreateTAAHistory(rhi::IRenderDevice& device, u32 w, u32 h);

    static glm::vec2 haltonJitter(u32 frameIndex) noexcept;
};

} // namespace daedalus::render
