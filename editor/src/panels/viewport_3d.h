#pragma once

#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_command_queue.h"
#include "daedalus/render/rhi/i_swapchain.h"
#include "daedalus/render/frame_renderer.h"
#include "daedalus/render/scene_view.h"
#include "daedalus/world/map_data.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace daedalus::editor
{

class EditMapDocument;

/// 3D preview viewport.
/// Renders the current map using the full deferred pipeline into an offscreen
/// texture, then displays it as an ImGui::Image.
class Viewport3D
{
public:
    explicit Viewport3D(std::string shaderLibPath);
    ~Viewport3D() = default;

    Viewport3D(const Viewport3D&)            = delete;
    Viewport3D& operator=(const Viewport3D&) = delete;

    /// Draw the panel for this frame.
    void draw(EditMapDocument&    doc,
              rhi::IRenderDevice& device,
              rhi::ICommandQueue& queue);

private:
    std::string m_shaderLibPath;

    // Offscreen render target.
    std::unique_ptr<rhi::ISwapchain> m_offscreenSwap;
    render::FrameRenderer            m_renderer;
    bool                             m_rendererInit = false;
    unsigned                         m_currentW     = 0;
    unsigned                         m_currentH     = 0;

    // Sector mesh buffers (re-uploaded whenever geometry is dirty).
    std::vector<std::unique_ptr<rhi::IBuffer>> m_vbos;
    std::vector<std::unique_ptr<rhi::IBuffer>> m_ibos;
    std::vector<render::MeshDraw>              m_draws;

    // Orbit camera state.
    float     m_orbitAngle  = 0.0f;
    float     m_orbitRadius = 15.0f;
    float     m_orbitHeight = 6.0f;
    unsigned  m_frameIdx    = 0;
    glm::mat4 m_prevView{1.0f};
    glm::mat4 m_prevProj{1.0f};

    void ensureInit(rhi::IRenderDevice& device, unsigned w, unsigned h);
    void retessellate(rhi::IRenderDevice& device, const world::WorldMapData& map);
};

} // namespace daedalus::editor
