// frame_renderer.h
// Orchestrates the full deferred rendering pipeline for one frame.
//
// Pass order:
//   1. Depth prepass  (render)
//   2. G-buffer       (render)
//   3. SSAO           (compute)
//   4. Lighting       (compute, deferred PBR)
//   5. Skybox         (render, procedural sky on background pixels)
//   6. TAA            (render, temporal anti-aliasing)
//   7. Bloom extract  (render)
//   8. Bloom blur H   (render)
//   9. Bloom blur V   (render)
//  10. Tone mapping   (render → swapchain)

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
    /// allocate persistent GPU resources, and build the test-room geometry.
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

    std::unique_ptr<rhi::IPipeline> m_depthPrepassPSO;
    std::unique_ptr<rhi::IPipeline> m_gbufferPSO;
    std::unique_ptr<rhi::IPipeline> m_ssaoPSO;
    std::unique_ptr<rhi::IPipeline> m_lightingPSO;
    std::unique_ptr<rhi::IPipeline> m_skyboxPSO;
    std::unique_ptr<rhi::IPipeline> m_taaPSO;
    std::unique_ptr<rhi::IPipeline> m_bloomExtractPSO;
    std::unique_ptr<rhi::IPipeline> m_bloomBlurHPSO;
    std::unique_ptr<rhi::IPipeline> m_bloomBlurVPSO;
    std::unique_ptr<rhi::IPipeline> m_tonemapPSO;

    // ─── Test-room geometry ───────────────────────────────────────────────────

    std::unique_ptr<rhi::IBuffer> m_roomVBO;        ///< 24 vertices × 48 B
    std::unique_ptr<rhi::IBuffer> m_roomIBO;        ///< 36 u32 indices
    static constexpr u32          k_roomIndexCount = 36;

    // ─── Per-frame GPU buffers ────────────────────────────────────────────────

    std::unique_ptr<rhi::IBuffer> m_frameConstBuf;  ///< FrameGPU  (448 B)
    std::unique_ptr<rhi::IBuffer> m_lightBuf;       ///< u32 header + PointLightGPU[]

    // ─── Persistent textures ─────────────────────────────────────────────────

    /// TAA history ping-pong: [even frame writes to 0, odd frame writes to 1].
    std::unique_ptr<rhi::ITexture> m_taaHistory[2];

    // ─── Sampler ─────────────────────────────────────────────────────────────

    std::unique_ptr<rhi::ISampler> m_linearClampSampler;

    // ─── Render graph ─────────────────────────────────────────────────────────

    RenderGraph m_graph;

    // ─── Frame state ──────────────────────────────────────────────────────────

    u32 m_width      = 0;
    u32 m_height     = 0;
    u32 m_frameIndex = 0;

    // ─── Internal helpers ─────────────────────────────────────────────────────

    void createPSOs(rhi::IRenderDevice& device, const std::string& libPath);
    void createGeometry(rhi::IRenderDevice& device);
    void createPersistentResources(rhi::IRenderDevice& device, u32 w, u32 h);
    void recreateTAAHistory(rhi::IRenderDevice& device, u32 w, u32 h);

    static glm::vec2 haltonJitter(u32 frameIndex) noexcept;
};

} // namespace daedalus::render
