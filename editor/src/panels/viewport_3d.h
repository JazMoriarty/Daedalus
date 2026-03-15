#pragma once

#include "entity_gpu_cache.h"
#include "daedalus/render/i_asset_loader.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_command_queue.h"
#include "daedalus/render/rhi/i_swapchain.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/frame_renderer.h"
#include "daedalus/render/scene_view.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/i_world_map.h"
#include "daedalus/world/i_portal_traversal.h"
#include "daedalus/core/types.h"

#include "imgui.h"
#include "daedalus/editor/editor_layer.h"  // PlayerStart

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace daedalus::editor
{

class EditMapDocument;
class MaterialCatalog;
class AssetBrowserPanel;

/// Which surface type is currently hovered in the 3D viewport.
enum class HoveredSurface : uint8_t { None, Wall, UpperWall, LowerWall, Floor, Ceil };

/// Active transform gizmo mode in the 3D viewport.
enum class GizmoMode : int
{
    None      = 0,
    Translate = 1,
    Rotate    = 2,
    Scale     = 3,
};

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
    void draw(EditMapDocument&      doc,
              render::IAssetLoader& loader,
              rhi::IRenderDevice&   device,
              rhi::ICommandQueue&   queue,
              MaterialCatalog&      catalog,
              AssetBrowserPanel&    assetBrowser);

    /// Enable/disable relative-mouse-mode (mouselook).  Called from main.mm on Tab/Escape.
    void setMouseCapture(SDL_Window* window, bool capture) noexcept;

    [[nodiscard]] bool isMouseCaptured() const noexcept { return m_mouseCaptured; }

    /// Set the active transform gizmo mode.
    void setGizmoMode(GizmoMode mode) noexcept;
    [[nodiscard]] GizmoMode gizmoMode() const noexcept { return m_gizmoMode; }

    /// Snap the 3D camera to the player start position and yaw.
    /// Sets eye height to ps.position.y + 1.75 (standing eye level) and
    /// horizontal yaw to ps.yaw, looking straight ahead (pitch = 0).
    void snapToPlayerStart(const PlayerStart& ps) noexcept;

    /// True if the 3D viewport panel was hovered on the previous frame.
    [[nodiscard]] bool isHovered() const noexcept { return m_isHovered; }

    /// Camera state accessors (used by Viewport2D for the fly-camera overlay).
    [[nodiscard]] glm::vec3 eye()   const noexcept { return m_eye;   }
    [[nodiscard]] float     yaw()   const noexcept { return m_yaw;   }
    [[nodiscard]] float     pitch() const noexcept { return m_pitch; }

    /// Restore all three camera axes at once (called after emap load).
    void setCameraState(glm::vec3 eye, float yaw, float pitch) noexcept
    {
        m_eye   = eye;
        m_yaw   = yaw;
        m_pitch = pitch;
    }

    /// Accumulate raw mouse delta from SDL_EVENT_MOUSE_MOTION while captured.
    /// Called from the main event loop so all motion events within a frame are summed.
    void addMouseDelta(float dx, float dy) noexcept { m_pendingDx += dx; m_pendingDy += dy; }

private:
    std::string m_shaderLibPath;

    // Offscreen render target.
    std::unique_ptr<rhi::ISwapchain> m_offscreenSwap;
    render::FrameRenderer            m_renderer;
    bool                             m_rendererInit = false;
    unsigned                         m_currentW     = 0;
    unsigned                         m_currentH     = 0;

    // Entity GPU resource cache.
    EntityGpuCache m_entityCache;

    // Sector mesh buffers — one inner vector per material batch per sector.
    std::vector<std::vector<std::unique_ptr<rhi::IBuffer>>> m_vbos;
    std::vector<std::vector<std::unique_ptr<rhi::IBuffer>>> m_ibos;
    std::vector<std::vector<render::MeshDraw>>              m_draws;
    std::vector<std::vector<UUID>>                          m_drawMaterialIds;

    // Free-fly camera state.
    glm::vec3 m_eye   = glm::vec3(0.0f, 5.0f, -12.0f);
    float     m_yaw   = 0.0f;     ///< Horizontal rotation, radians (0 = looking toward +Z).
    float     m_pitch = -0.349f;  ///< Vertical rotation, radians (negative = looking down).

    bool        m_mouseCaptured   = false;  ///< True when Tab has locked the cursor for mouselook.
    bool        m_altDragging     = false;  ///< True while Alt+LMB orbit is active.
    glm::vec3   m_altFocus{};              ///< Orbit pivot point for current Alt+drag gesture.

    // Mouse-capture state for fly-mode mouselook (SDL relative-mouse-mode).
    SDL_Window* m_captureWindow = nullptr;

    unsigned  m_frameIdx    = 0;
    float     m_accTime     = 0.0f;  ///< Accumulated time in seconds (passed to SceneView::time).
    glm::mat4 m_prevView{1.0f};
    glm::mat4 m_prevProj{1.0f};

    // LUT texture cache: reloaded whenever m_lutPath changes.
    std::string                          m_lutPath;
    std::unique_ptr<rhi::ITexture>       m_lutTexture;

    // Portal traversal: rebuilt when geometry changes.
    std::unique_ptr<world::IWorldMap>        m_worldMap;
    std::unique_ptr<world::IPortalTraversal> m_portalTraversal;

    // Mirror surface cache: cleared on retessellate, lazily populated in draw().
    struct MirrorEntry
    {
        world::SectorId                sectorId;
        std::size_t                    wallIdx;
        std::unique_ptr<rhi::ITexture> renderTarget;
        std::unique_ptr<rhi::IBuffer>  vbo;
        std::unique_ptr<rhi::IBuffer>  ibo;
    };
    std::vector<MirrorEntry> m_mirrorCache;

    // ─── Gizmo state ──────────────────────────────────────────────────────────
    GizmoMode  m_gizmoMode           = GizmoMode::None;
    int        m_gizmoDragAxis       = -1;      ///< 0=X 1=Y 2=Z; -1 = none
    bool       m_gizmoDragging       = false;
    glm::vec3  m_gizmoEntityOrigPos  = glm::vec3(0.0f);
    float      m_gizmoEntityOrigYaw  = 0.0f;
    glm::vec3  m_gizmoEntityOrigScale= glm::vec3(1.0f);
    bool      m_isHovered           = false;

    // ─── Wall hover / selection state ───────────────────────────────────────────
    world::SectorId m_hoveredSectorId  = world::INVALID_SECTOR_ID;
    std::size_t     m_hoveredWallIdx   = 0;
    HoveredSurface  m_hoveredSurface   = HoveredSurface::None;
    /// Which surface was last clicked to produce a Sector selection (Floor or Ceil).
    HoveredSurface  m_selectedSurface  = HoveredSurface::Floor;
    /// Which wall surface (Wall/UpperWall/LowerWall) produced the current Wall selection.
    HoveredSurface  m_selectedWallSurface = HoveredSurface::Wall;

    // ─── Accumulated mouse delta for mouselook (fed from event loop) ─────────────
    float m_pendingDx = 0.0f;
    float m_pendingDy = 0.0f;
    /// Frames to skip after entering capture mode so the warp-generated
    /// delta spike doesn't snap the camera on entry.
    int   m_captureWarmupFrames = 0;

    // ─── UV editing state (keyboard shortcuts) ──────────────────────────────
    bool            m_uvEditing          = false;
    glm::vec2       m_uvEditOrigOffset   = {};
    glm::vec2       m_uvEditOrigScale    = {1.0f, 1.0f};
    float           m_uvEditOrigRotation = 0.0f;
    world::SectorId m_uvEditSectorId     = world::INVALID_SECTOR_ID;
    std::size_t     m_uvEditWallIdx      = 0;

    void ensureInit(rhi::IRenderDevice& device, unsigned w, unsigned h);
    void retessellate(rhi::IRenderDevice& device, const world::WorldMapData& map);
    void reloadLutIfNeeded(render::IAssetLoader& loader,
                           rhi::IRenderDevice&   device,
                           const std::string&    newPath);
    void drawGizmoAndHandleDrag(EditMapDocument&  doc,
                                const glm::mat4& view,
                                const glm::mat4& proj,
                                const ImVec2&    imageTopLeft,
                                unsigned         viewW,
                                unsigned         viewH);
};

} // namespace daedalus::editor
