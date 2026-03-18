#include "viewport_3d.h"
#include "asset_browser_panel.h"
#include "catalog/material_catalog.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/render_settings_data.h"
#include "daedalus/editor/light_def.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/sector_tessellator.h"
#include "daedalus/render/i_asset_loader.h"
#include "daedalus/render/vertex_types.h"
#include "daedalus/render/rhi/rhi_types.h"
#include "document/commands/cmd_move_entity.h"
#include "document/commands/cmd_rotate_entity.h"
#include "document/commands/cmd_scale_entity.h"
#include "document/commands/cmd_set_wall_material.h"
#include "document/commands/cmd_set_sector_material.h"
#include "document/commands/cmd_set_wall_uv.h"
#include "daedalus/editor/compound_command.h"
#include "uv_utils.h"

// stb_image (declaration only — implementation compiled once in asset_loader.cpp)
#include "stb_image.h"

#include "imgui.h"

#include <SDL3/SDL.h>
#include <CoreGraphics/CoreGraphics.h>  // CGAssociateMouseAndMouseCursorPosition, CGGetLastMouseDelta
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <string>

namespace daedalus::editor
{

Viewport3D::Viewport3D(std::string shaderLibPath)
    : m_shaderLibPath(std::move(shaderLibPath))
{}

// ─── ensureInit ───────────────────────────────────────────────────────────────

void Viewport3D::ensureInit(rhi::IRenderDevice& device, unsigned w, unsigned h)
{
    if (!m_rendererInit)
    {
        m_offscreenSwap = device.createOffscreenSwapchain(w, h);
        m_renderer.initialize(device, m_shaderLibPath, w, h);
        m_rendererInit = true;
        m_currentW     = w;
        m_currentH     = h;
        return;
    }

    if (w != m_currentW || h != m_currentH)
    {
        m_offscreenSwap->resize(w, h);
        m_renderer.resize(device, w, h);
        m_currentW = w;
        m_currentH = h;
    }
}

// ─── retessellate ─────────────────────────────────────────────────────

void Viewport3D::retessellate(rhi::IRenderDevice& device,
                               const world::WorldMapData& map)
{
    m_vbos.clear();
    m_ibos.clear();
    m_draws.clear();
    m_drawMaterialIds.clear();
    m_mirrorCache.clear();

    if (map.sectors.empty()) return;

    // Use per-material tagged tessellation so each draw call binds its texture.
    const auto sectorBatches = world::tessellateMapTagged(map);
    const std::size_t nSectors = sectorBatches.size();

    m_vbos.resize(nSectors);
    m_ibos.resize(nSectors);
    m_draws.resize(nSectors);
    m_drawMaterialIds.resize(nSectors);

    for (std::size_t si = 0; si < nSectors; ++si)
    {
        const auto& batches = sectorBatches[si];
        const std::size_t nb = batches.size();

        m_vbos[si].resize(nb);
        m_ibos[si].resize(nb);
        m_draws[si].resize(nb);
        m_drawMaterialIds[si].resize(nb);

        for (std::size_t bi = 0; bi < nb; ++bi)
        {
            const render::MeshData& mesh = batches[bi].mesh;
            if (mesh.vertices.empty()) continue;

            {
                rhi::BufferDescriptor d;
                d.size      = mesh.vertices.size() * sizeof(render::StaticMeshVertex);
                d.usage     = rhi::BufferUsage::Vertex;
                d.initData  = mesh.vertices.data();
                d.debugName = "EditorSectorVBO_" + std::to_string(si)
                              + "_" + std::to_string(bi);
                m_vbos[si][bi] = device.createBuffer(d);
            }
            {
                rhi::BufferDescriptor d;
                d.size      = mesh.indices.size() * sizeof(unsigned);
                d.usage     = rhi::BufferUsage::Index;
                d.initData  = mesh.indices.data();
                d.debugName = "EditorSectorIBO_" + std::to_string(si)
                              + "_" + std::to_string(bi);
                m_ibos[si][bi] = device.createBuffer(d);
            }

            render::MeshDraw& draw  = m_draws[si][bi];
            draw.vertexBuffer       = m_vbos[si][bi].get();
            draw.indexBuffer        = m_ibos[si][bi].get();
            draw.indexCount         = static_cast<unsigned>(mesh.indices.size());
            draw.modelMatrix        = glm::mat4(1.0f);
            draw.prevModel          = glm::mat4(1.0f);
            draw.material.sectorAmbient = map.sectors[si].ambientColor
                                          * map.sectors[si].ambientIntensity;
            draw.material.isOutdoor = world::hasFlag(map.sectors[si].flags,
                                                     world::SectorFlags::Outdoors);
            // Albedo filled in during draw() from the MaterialCatalog.

            m_drawMaterialIds[si][bi] = batches[bi].materialId;
        }
    }

    // Rebuild world map for portal traversal.
    m_worldMap = world::makeWorldMap(map);
    if (!m_portalTraversal)
        m_portalTraversal = world::makePortalTraversal();
}

// ─── reloadLutIfNeeded ───────────────────────────────────────────────────────

void Viewport3D::reloadLutIfNeeded(render::IAssetLoader&  loader,
                                    rhi::IRenderDevice&    device,
                                    const std::string&     newPath)
{
    (void)loader;  // available for future IAssetLoader extensions

    if (newPath == m_lutPath) return;  // path unchanged, nothing to do

    m_lutPath    = newPath;
    m_lutTexture = nullptr;  // drop the old texture

    if (newPath.empty()) return;

    // Load the PNG via stb_image (implementation lives in DaedalusRender).
    // Expected format: 1024×32 RGBA8 strip — 32 horizontal 32×32 slices (one per
    // blue channel value), stored in Z-major / G-row / R-column order so that
    // the resulting 3D texture matches the renderer’s identity LUT layout.
    int imgW = 0, imgH = 0, channels = 0;
    stbi_uc* pixels = stbi_load(newPath.c_str(), &imgW, &imgH, &channels, 4);
    if (!pixels)
        return;  // file not found or decode error — identity LUT stays active

    static constexpr int kLutSize = 32;
    const bool validSize = (imgW == kLutSize * kLutSize) && (imgH == kLutSize);
    if (!validSize)
    {
        stbi_image_free(pixels);
        return;  // unrecognised layout — identity LUT stays active
    }

    // Repack from 1024×32 strip layout into Z×G×R linear order.
    // Pixel at strip position (x, y) corresponds to (r=x%32, g=y, b=x/32).
    const std::size_t kPixels = static_cast<std::size_t>(kLutSize * kLutSize * kLutSize);
    std::vector<stbi_uc> lut3d(kPixels * 4u);

    for (int b = 0; b < kLutSize; ++b)
    for (int g = 0; g < kLutSize; ++g)
    for (int r = 0; r < kLutSize; ++r)
    {
        const int   srcX   = b * kLutSize + r;
        const int   srcY   = g;
        const int   srcOff = (srcY * imgW + srcX) * 4;
        const int   dstOff = (b * kLutSize * kLutSize + g * kLutSize + r) * 4;
        lut3d[static_cast<std::size_t>(dstOff) + 0] = pixels[static_cast<std::size_t>(srcOff) + 0];
        lut3d[static_cast<std::size_t>(dstOff) + 1] = pixels[static_cast<std::size_t>(srcOff) + 1];
        lut3d[static_cast<std::size_t>(dstOff) + 2] = pixels[static_cast<std::size_t>(srcOff) + 2];
        lut3d[static_cast<std::size_t>(dstOff) + 3] = pixels[static_cast<std::size_t>(srcOff) + 3];
    }
    stbi_image_free(pixels);

    // Upload as a 32×32×32 3D texture (RGBA8Unorm, linear) for the CG shader.
    rhi::TextureDescriptor d;
    d.width     = static_cast<uint32_t>(kLutSize);
    d.height    = static_cast<uint32_t>(kLutSize);
    d.depth     = static_cast<uint32_t>(kLutSize);
    d.format    = rhi::TextureFormat::RGBA8Unorm;
    d.usage     = rhi::TextureUsage::ShaderRead;
    d.initData  = lut3d.data();
    d.debugName = std::filesystem::path(newPath).filename().string();
    m_lutTexture = device.createTexture(d);
}

// ─── snapToPlayerStart ───────────────────────────────────────────────────────

void Viewport3D::snapToPlayerStart(const PlayerStart& ps) noexcept
{
    constexpr float kEyeHeight = 1.75f;  // metres above player spawn Y
    m_eye   = glm::vec3(ps.position.x,
                        ps.position.y + kEyeHeight,
                        ps.position.z);
    m_yaw   = ps.yaw;
    m_pitch = 0.0f;
    // Update the orbit pivot to 5 units ahead along the new facing direction.
    m_altFocus = m_eye + glm::vec3(std::sin(ps.yaw), 0.0f, std::cos(ps.yaw)) * 5.0f;
}

// ─── setGizmoMode ────────────────────────────────────────────────────────────

void Viewport3D::setGizmoMode(GizmoMode mode) noexcept
{
    m_gizmoMode    = mode;
    m_gizmoDragging = false;
    m_gizmoDragAxis = -1;
}

// ─── drawGizmoAndHandleDrag ───────────────────────────────────────────────────

void Viewport3D::drawGizmoAndHandleDrag(EditMapDocument&  doc,
                                        const glm::mat4& view,
                                        const glm::mat4& proj,
                                        const ImVec2&    imageTopLeft,
                                        unsigned         viewW,
                                        unsigned         viewH)
{
    // Gizmos are suppressed during mouselook.
    if (m_mouseCaptured) return;
    if (m_gizmoMode == GizmoMode::None) return;

    const SelectionState& sel = doc.selection();
    if (sel.type != SelectionType::Entity) return;
    const std::size_t idx = sel.entityIndex;
    if (idx >= doc.entities().size()) return;

    EntityDef& entity = doc.entities()[idx];

    const float fw = static_cast<float>(viewW);
    const float fh = static_cast<float>(viewH);

    // Project world position to screen (image-relative).
    auto project = [&](glm::vec3 world) -> ImVec2
    {
        glm::vec4 clip = proj * view * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f) return ImVec2(-1e5f, -1e5f);
        const float nx = clip.x / clip.w;
        const float ny = clip.y / clip.w;
        return ImVec2(
            imageTopLeft.x + (nx * 0.5f + 0.5f) * fw,
            imageTopLeft.y + (1.0f - (ny * 0.5f + 0.5f)) * fh);
    };

    // Compute a world arm-length so the gizmo appears ~60 px tall on screen.
    const float eyeDist  = std::max(0.1f, glm::length(entity.position - m_eye));
    const float tanHalf  = std::tan(glm::radians(30.0f)); // half of 60° fovY
    const float armLen   = std::max(0.05f, (60.0f / fh) * 2.0f * eyeDist * tanHalf);

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mousePos = io.MousePos;

    // Euclidean distance between two ImVec2.
    auto dist2 = [](ImVec2 a, ImVec2 b)
    {
        const float dx = a.x - b.x, dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    };

    ImVec2 sp0 = project(entity.position);

    // ─── Translate / Scale gizmos ─────────────────────────────────────────
    if (m_gizmoMode == GizmoMode::Translate || m_gizmoMode == GizmoMode::Scale)
    {
        static const glm::vec3 kAxes[3]  = {{1,0,0},{0,1,0},{0,0,1}};
        static const ImU32     kColors[3] = {
            IM_COL32(230,  60,  60, 220),  // X – red
            IM_COL32( 60, 210,  60, 220),  // Y – green
            IM_COL32( 60, 100, 230, 220),  // Z – blue
        };
        constexpr float kHitRadius = 9.0f;

        for (int a = 0; a < 3; ++a)
        {
            ImVec2 sp1 = project(entity.position + kAxes[a] * armLen);

            // Highlight hovered / active axis.
            const bool active  = m_gizmoDragging && m_gizmoDragAxis == a;
            const bool hovered = !m_gizmoDragging && dist2(mousePos, sp1) < kHitRadius;
            ImU32 col = (active || hovered)
                ? IM_COL32(255, 255, 100, 255) : kColors[a];

            dl->AddLine(sp0, sp1, col, 2.5f);
            if (m_gizmoMode == GizmoMode::Translate)
                dl->AddCircleFilled(sp1, 5.5f, col);
            else
                dl->AddRectFilled(
                    ImVec2(sp1.x - 5.0f, sp1.y - 5.0f),
                    ImVec2(sp1.x + 5.0f, sp1.y + 5.0f), col);

            // Begin drag when mouse pressed over handle.
            if (!m_gizmoDragging &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                dist2(mousePos, sp1) < kHitRadius)
            {
                m_gizmoDragging       = true;
                m_gizmoDragAxis       = a;
                m_gizmoEntityOrigPos  = entity.position;
                m_gizmoEntityOrigYaw  = entity.yaw;
                m_gizmoEntityOrigScale= entity.scale;
            }
        }

        // Apply drag each frame.
        if (m_gizmoDragging && m_gizmoDragAxis >= 0 &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const glm::vec3 axisW = kAxes[m_gizmoDragAxis];
            // Project a unit step along the axis to get the screen direction.
            ImVec2 refSp0 = project(entity.position);
            ImVec2 refSp1 = project(entity.position + axisW);
            const float sdx = refSp1.x - refSp0.x;
            const float sdy = refSp1.y - refSp0.y;
            const float slen = std::sqrt(sdx * sdx + sdy * sdy);
            if (slen > 0.5f)
            {
                const float proj1D =
                    io.MouseDelta.x * (sdx / slen) +
                    io.MouseDelta.y * (sdy / slen);
                const float worldDelta = proj1D / slen; // slen px = 1 world unit

                if (m_gizmoMode == GizmoMode::Translate)
                    entity.position += axisW * worldDelta;
                else
                {
                    entity.scale[m_gizmoDragAxis] += worldDelta;
                    entity.scale[m_gizmoDragAxis] =
                        std::max(entity.scale[m_gizmoDragAxis], 0.01f);
                }
                doc.markEntityDirty();
            }
        }
        // End drag: push undo-able command.
        else if (m_gizmoDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_gizmoDragging = false;
            if (m_gizmoMode == GizmoMode::Translate &&
                entity.position != m_gizmoEntityOrigPos)
            {
                doc.pushCommand(std::make_unique<CmdMoveEntity>(
                    doc, idx, m_gizmoEntityOrigPos, entity.position));
            }
            else if (m_gizmoMode == GizmoMode::Scale &&
                     entity.scale != m_gizmoEntityOrigScale)
            {
                doc.pushCommand(std::make_unique<CmdScaleEntity>(
                    doc, idx, m_gizmoEntityOrigScale, entity.scale));
            }
        }
    }

    // ─── Rotate gizmo (yaw ring) ────────────────────────────────────────────
    else if (m_gizmoMode == GizmoMode::Rotate)
    {
        constexpr float kRingR    = 42.0f;
        constexpr float kHitWidth =  6.0f;
        constexpr float kRotSens  =  0.02f;  // radians per screen pixel

        const float dToCenter = dist2(mousePos, sp0);
        const bool hovering = std::abs(dToCenter - kRingR) < kHitWidth;
        const bool active   = m_gizmoDragging;
        ImU32 col = (active || hovering)
            ? IM_COL32(255, 200,  50, 255)
            : IM_COL32(200, 200, 200, 200);

        dl->AddCircle(sp0, kRingR, col, 64, 2.5f);

        // Yaw tick line to show current orientation.
        const float cosY = std::cos(entity.yaw), sinY = std::sin(entity.yaw);
        // Project the +Z facing direction onto screen.
        ImVec2 fwdScreen = project(entity.position + glm::vec3(sinY, 0.0f, cosY) * armLen);
        dl->AddLine(sp0, fwdScreen, col, 2.0f);

        if (!m_gizmoDragging &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovering)
        {
            m_gizmoDragging       = true;
            m_gizmoEntityOrigYaw  = entity.yaw;
            m_gizmoEntityOrigPos  = entity.position;
            m_gizmoEntityOrigScale= entity.scale;
        }

        if (m_gizmoDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            entity.yaw += io.MouseDelta.x * kRotSens;
            doc.markEntityDirty();
        }
        else if (m_gizmoDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_gizmoDragging = false;
            if (entity.yaw != m_gizmoEntityOrigYaw)
                doc.pushCommand(std::make_unique<CmdRotateEntity>(
                    doc, idx, m_gizmoEntityOrigYaw, entity.yaw));
        }
    }
}

// ─── setMouseCapture ──────────────────────────────────────────────────────────

void Viewport3D::setMouseCapture(SDL_Window* window, bool capture) noexcept
{
    m_mouseCaptured = capture;
    if (capture)
    {
        m_captureWindow = window;
        SDL_SetWindowMouseGrab(window, true);
        SDL_HideCursor();
        // Freeze cursor without needing SDL keyboard focus, so mouselook
        // works regardless of window activation order.  Raw movement is then
        // polled via CGGetLastMouseDelta() in draw() each frame.
        CGAssociateMouseAndMouseCursorPosition(false);
        // Drain the pre-capture accumulated delta and skip warmup frames.
        { int32_t _x = 0, _y = 0; CGGetLastMouseDelta(&_x, &_y); }
        m_pendingDx            = 0.0f;
        m_pendingDy            = 0.0f;
        m_captureWarmupFrames  = 2;
    }
    else
    {
        CGAssociateMouseAndMouseCursorPosition(true);  // unfreeze cursor
        SDL_ShowCursor();
        if (m_captureWindow)
            SDL_SetWindowMouseGrab(m_captureWindow, false);
        m_captureWindow = nullptr;
        m_altDragging   = false;
    }
}

// ─── Ray picking helpers ──────────────────────────────────────────────────

// Möller–Trumbore ray/triangle test.
// outT receives the ray parameter on success.
static bool rayTriangleIntersect(
    const glm::vec3& orig, const glm::vec3& dir,
    const glm::vec3& v0,   const glm::vec3& v1, const glm::vec3& v2,
    float& outT) noexcept
{
    constexpr float kEps = 1e-8f;
    const glm::vec3 edge1 = v1 - v0;
    const glm::vec3 edge2 = v2 - v0;
    const glm::vec3 h     = glm::cross(dir, edge2);
    const float     det   = glm::dot(edge1, h);
    if (std::abs(det) < kEps) return false;
    const float     invDet = 1.0f / det;
    const glm::vec3 s      = orig - v0;
    const float     u      = glm::dot(s, h) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 q = glm::cross(s, edge1);
    const float     v = glm::dot(dir, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = glm::dot(edge2, q) * invDet;
    if (t < 0.0f) return false;
    outT = t;
    return true;
}

// Ray-AABB intersection test for entity bounding boxes.
// Returns true if the ray intersects the box, with outT receiving the entry distance.
static bool rayBoxIntersect(
    const glm::vec3& rayOrig, const glm::vec3& rayDir,
    const glm::vec3& boxMin,  const glm::vec3& boxMax,
    float& outT) noexcept
{
    constexpr float kEps = 1e-6f;
    float tmin = 0.0f;
    float tmax = FLT_MAX;
    
    for (int i = 0; i < 3; ++i)
    {
        if (std::abs(rayDir[i]) < kEps)
        {
            // Ray parallel to slab i
            if (rayOrig[i] < boxMin[i] || rayOrig[i] > boxMax[i])
                return false;
        }
        else
        {
            const float invD = 1.0f / rayDir[i];
            float t0 = (boxMin[i] - rayOrig[i]) * invD;
            float t1 = (boxMax[i] - rayOrig[i]) * invD;
            if (t0 > t1) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmin > tmax) return false;
        }
    }
    
    if (tmin < 0.0f && tmax < 0.0f) return false;  // box behind ray
    outT = (tmin >= 0.0f) ? tmin : tmax;
    return outT >= 0.0f;
}

// Build a ray from a mouse position in image-space.
// Uses near+far unprojection and returns the normalized direction.
static glm::vec3 screenToWorldRay(
    ImVec2 mousePos, ImVec2 imageTopLeft,
    unsigned viewW, unsigned viewH,
    const glm::vec3& eye,
    const glm::mat4& view, const glm::mat4& proj) noexcept
{
    const float fw = static_cast<float>(viewW);
    const float fh = static_cast<float>(viewH);
    const float ndcX = (mousePos.x - imageTopLeft.x) / fw * 2.0f - 1.0f;
    const float ndcY = 1.0f - (mousePos.y - imageTopLeft.y) / fh * 2.0f;

    // Unproject near/far points (Z in [0,1] for LH_ZO projection).
    const glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farW  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearW /= nearW.w;
    farW  /= farW.w;
    return glm::normalize(glm::vec3(farW - nearW));
}

// ─── draw ─────────────────────────────────────────────────────────────────

void Viewport3D::draw(EditMapDocument&      doc,
                       render::IAssetLoader& loader,
                       rhi::IRenderDevice&   device,
                       rhi::ICommandQueue&   queue,
                       MaterialCatalog&      catalog,
                       AssetBrowserPanel&    assetBrowser,
                       bool                  entityRotating,
                       std::size_t           entityRotIdx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("3D Viewport");
    ImGui::PopStyleVar();

    // Cache for main.mm key handling (used next frame).
    m_isHovered = ImGui::IsWindowHovered();

    const ImVec2 sz = ImGui::GetContentRegionAvail();
    const auto w = static_cast<unsigned>(sz.x > 8.0f ? sz.x : 8.0f);
    const auto h = static_cast<unsigned>(sz.y > 8.0f ? sz.y : 8.0f);

    ensureInit(device, w, h);

    // Skip rendering if paused (e.g. Test Map app is running), but still display
    // the last rendered frame and update the panel UI.
    if (m_renderingPaused)
    {
        rhi::ITexture* tex = m_offscreenSwap->nextDrawable();
        ImGui::Image(reinterpret_cast<ImTextureID>(tex->nativeHandle()),
                     ImVec2(static_cast<float>(w), static_cast<float>(h)));
        
        // Show pause indicator
        ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[PAUSED] Test Map running");
        
        ImGui::End();
        return;
    }

    // Retessellate if the map geometry changed.
    if (doc.isGeometryDirty())
    {
        retessellate(device, doc.mapData());
        doc.clearGeometryDirty();
    }

    // Rebuild entity GPU cache if entities changed.
    if (doc.isEntityDirty())
    {
        m_entityCache.invalidate();
        doc.clearEntityDirty();
    }
    m_entityCache.rebuild(loader, device, catalog, doc.entities(), doc);

    // ── Free-fly camera ───────────────────────────────────────────────────────
    const ImGuiIO& io = ImGui::GetIO();
    const float dt = std::min(io.DeltaTime, 0.05f);  // cap at 50 ms – prevents lurch on heavy frames (file I/O, asset loads)

    // Helper: build the forward unit vector from current yaw / pitch.
    auto makeFwd = [&](float yaw, float pitch) -> glm::vec3
    {
        return {
            std::sin(yaw)  * std::cos(pitch),
            std::sin(pitch),
            std::cos(yaw)  * std::cos(pitch)
        };
    };

    glm::vec3 fwd = makeFwd(m_yaw, m_pitch);

    if (m_mouseCaptured)
    {
        // ── Fly mode: mouselook FIRST, then WASD ──────────────────────────────
        // Consuming rotation before movement means WASD uses the current frame's
        // look direction, eliminating the one-frame lag that causes the "pull"
        // sensation when turning while moving.

        // Step 1 – read raw hardware delta via CGGetLastMouseDelta.
        // Warmup frames drain any startup spike; after warmup, each call
        // returns only movement accumulated since the previous call.
        if (m_captureWarmupFrames > 0)
        {
            --m_captureWarmupFrames;
            // Drain accumulated delta during warmup to start clean.
            { int32_t _x = 0, _y = 0; CGGetLastMouseDelta(&_x, &_y); }
            m_pendingDx = 0.0f;
            m_pendingDy = 0.0f;
        }
        else
        {
            int32_t cgDx = 0, cgDy = 0;
            CGGetLastMouseDelta(&cgDx, &cgDy);
            m_pendingDx = 0.0f;
            m_pendingDy = 0.0f;
            constexpr float kMaxDelta = 200.0f;
            constexpr float kSens = 0.003f;
            m_yaw   += std::clamp(static_cast<float>(cgDx), -kMaxDelta, kMaxDelta) * kSens;
            m_pitch -= std::clamp(static_cast<float>(cgDy), -kMaxDelta, kMaxDelta) * kSens;
            m_pitch  = std::clamp(m_pitch,
                                   -glm::pi<float>() * 0.45f,
                                    glm::pi<float>() * 0.45f);
            fwd = makeFwd(m_yaw, m_pitch);
        }

        // Step 2 – derive right from the now-current fwd so A/D strafes in the
        // correct direction even on frames where the player turned sharply.
        const glm::vec3 right = glm::normalize(
            glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), fwd));

        // Step 3 – apply WASD / Q / E movement.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const bool  fast = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        const float spd  = (fast ? 5.0f : 1.0f) * 8.0f * dt;

        if (keys[SDL_SCANCODE_W]) m_eye += fwd                          * spd;
        if (keys[SDL_SCANCODE_S]) m_eye -= fwd                          * spd;
        if (keys[SDL_SCANCODE_A]) m_eye -= right                        * spd;
        if (keys[SDL_SCANCODE_D]) m_eye += right                        * spd;
        if (keys[SDL_SCANCODE_Q]) m_eye -= glm::vec3(0.0f, 1.0f, 0.0f) * spd;
        if (keys[SDL_SCANCODE_E]) m_eye += glm::vec3(0.0f, 1.0f, 0.0f) * spd;
    }
    else
    {
        // ── Navigation mode: Alt+LMB orbit + scroll dolly ─────────────────────
        if (ImGui::IsWindowHovered())
        {
            // Begin an orbit gesture when Alt+LMB is clicked inside the panel.
            if (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                m_altDragging = true;
                // Pivot 10 units ahead (or closer to the map centre on F-frame).
                m_altFocus = m_eye + fwd * 10.0f;
            }

            // Scroll: dolly forward/backward along view direction.
            if (io.MouseWheel != 0.0f)
                m_eye += fwd * io.MouseWheel * 0.5f;
        }

        // End orbit gesture when Alt or LMB is released.
        if (!io.KeyAlt || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_altDragging = false;

        if (m_altDragging)
        {
            const float dist = std::max(0.1f, glm::length(m_eye - m_altFocus));
            // Clamp per-frame delta to prevent orbit from jumping on heavy frames.
            constexpr float kMaxOrbitDelta = 50.0f;  // px
            m_yaw   += std::clamp(io.MouseDelta.x, -kMaxOrbitDelta, kMaxOrbitDelta) * 0.008f;
            m_pitch -= std::clamp(io.MouseDelta.y, -kMaxOrbitDelta, kMaxOrbitDelta) * 0.008f;
            m_pitch  = std::clamp(m_pitch,
                                   -glm::pi<float>() * 0.45f,
                                    glm::pi<float>() * 0.45f);
            // Swing the eye around the fixed pivot.
            fwd   = makeFwd(m_yaw, m_pitch);
            m_eye = m_altFocus - fwd * dist;
        }

        // P key — snap camera to player start (eye height + yaw); no fly-mode capture.
        if (ImGui::IsWindowHovered() && ImGui::IsKeyPressed(ImGuiKey_P, /*repeat=*/false))
        {
            if (const auto& ps = doc.playerStart(); ps.has_value())
                snapToPlayerStart(*ps);
        }

        // F key — frame the selected sectors (or all sectors if nothing selected).
        if (ImGui::IsWindowHovered() && ImGui::IsKeyPressed(ImGuiKey_F, /*repeat=*/false))
        {
            const auto& sel     = doc.selection();
            const auto& sectors = doc.mapData().sectors;

            float minX = FLT_MAX, maxX = -FLT_MAX;
            float minY = FLT_MAX, maxY = -FLT_MAX;
            float minZ = FLT_MAX, maxZ = -FLT_MAX;

            auto expandSector = [&](world::SectorId sid)
            {
                if (sid >= static_cast<world::SectorId>(sectors.size())) return;
                const auto& s = sectors[sid];
                minY = std::min(minY, s.floorHeight);
                maxY = std::max(maxY, s.ceilHeight);
                for (const auto& wl : s.walls)
                {
                    minX = std::min(minX, wl.p0.x); maxX = std::max(maxX, wl.p0.x);
                    minZ = std::min(minZ, wl.p0.y); maxZ = std::max(maxZ, wl.p0.y);
                }
            };

            if (sel.type == SelectionType::Sector && !sel.sectors.empty())
            {
                for (auto sid : sel.sectors)
                    expandSector(sid);
            }
            else
            {
                for (std::size_t i = 0; i < sectors.size(); ++i)
                    expandSector(static_cast<world::SectorId>(i));
            }

            if (minX < maxX)
            {
                const float cx     = (minX + maxX) * 0.5f;
                const float cy     = (minY + maxY) * 0.5f;
                const float cz     = (minZ + maxZ) * 0.5f;
                const float radius = std::max({maxX - minX, maxY - minY, maxZ - minZ}) * 0.75f;

                // Preserve current yaw; tilt down 30° to look at the selection.
                m_pitch = glm::radians(-30.0f);
                fwd     = makeFwd(m_yaw, m_pitch);
                m_eye   = glm::vec3(cx, cy, cz) - fwd * std::max(2.0f, radius);
                // Update pivot to the selection centre for the next orbit.
                m_altFocus = glm::vec3(cx, cy, cz);
            }
        }
    }

    // ── Map AABB for fill light (computed once per frame) ─────────────────────
    float fillX = m_eye.x, fillZ = m_eye.z;
    if (!doc.mapData().sectors.empty())
    {
        float minX = FLT_MAX, maxX = -FLT_MAX;
        float minZ = FLT_MAX, maxZ = -FLT_MAX;
        for (const auto& sector : doc.mapData().sectors)
            for (const auto& wl : sector.walls)
            {
                minX = std::min(minX, wl.p0.x); maxX = std::max(maxX, wl.p0.x);
                minZ = std::min(minZ, wl.p0.y); maxZ = std::max(maxZ, wl.p0.y);
            }
        fillX = (minX + maxX) * 0.5f;
        fillZ = (minZ + maxZ) * 0.5f;
    }

    // ── Build view / projection matrices ─────────────────────────────────────
    const glm::vec3 target = m_eye + fwd;
    const float fovY   = glm::radians(60.0f);
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    const glm::mat4 view = glm::lookAtLH(m_eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspectiveLH_ZO(fovY, aspect, 0.1f, 500.0f);

    // ── Build SceneView ───────────────────────────────────────────────────────
    render::SceneView scene;
    scene.view       = view;
    scene.proj       = proj;
    scene.prevView   = (m_frameIdx == 0u) ? view : m_prevView;
    scene.prevProj   = (m_frameIdx == 0u) ? proj : m_prevProj;
    scene.cameraPos  = m_eye;
    scene.cameraDir  = fwd;
    scene.frameIndex = m_frameIdx;
    scene.deltaTime  = dt;
    m_accTime        += dt;
    scene.time        = m_accTime;

    // Lighting from document SceneSettings.
    const auto& ss      = doc.sceneSettings();
    scene.sunDirection  = ss.sunDirection;
    scene.sunColor      = ss.sunColor;
    scene.sunIntensity  = ss.sunIntensity;
    // Per-sector ambient is baked into each draw's material.sectorAmbient;
    // zero the global ambient to avoid double-counting.
    scene.ambientColor  = glm::vec3(0.0f);

    // Editor fill light: only injected when the scene has no user-placed lights,
    // so a new/empty scene stays visible.  Suppressed once the user adds their
    // own lights so the editor preview matches the actual game lighting.
    if (doc.lights().empty())
    {
        scene.pointLights.push_back({
            glm::vec3(fillX, 8.0f, fillZ),
            20.0f,
            glm::vec3(1.0f, 0.94f, 0.82f),
            2.5f
        });
    }

    // Editor-placed lights from the document.
    for (const auto& ld : doc.lights())
    {
        if (ld.type == LightType::Point)
        {
            scene.pointLights.push_back({
                ld.position,
                ld.radius,
                ld.color,
                ld.intensity
            });
        }
        else if (ld.type == LightType::Spot)
        {
            scene.spotLights.push_back({
                ld.position,
                ld.direction,
                ld.innerConeAngle,
                ld.outerConeAngle,
                ld.range,
                ld.color,
                ld.intensity
            });
        }
    }

    // ── Apply render settings ───────────────────────────────────────────────
    {
        const RenderSettingsData& r = doc.renderSettings();

        // Volumetric fog
        scene.fog.enabled    = r.fog.enabled;
        scene.fog.density    = r.fog.density;
        scene.fog.anisotropy = r.fog.anisotropy;
        scene.fog.scattering = r.fog.scattering;
        scene.fog.fogNear    = r.fog.fogNear;
        scene.fog.fogFar     = r.fog.fogFar;
        scene.fog.ambientFog = glm::vec3(r.fog.ambientFogR,
                                         r.fog.ambientFogG,
                                         r.fog.ambientFogB);

        // Screen-space reflections
        scene.ssr.enabled         = r.ssr.enabled;
        scene.ssr.maxDistance     = r.ssr.maxDistance;
        scene.ssr.thickness       = r.ssr.thickness;
        scene.ssr.roughnessCutoff = r.ssr.roughnessCutoff;
        scene.ssr.fadeStart       = r.ssr.fadeStart;
        scene.ssr.maxSteps        = r.ssr.maxSteps;

        // Depth of field
        scene.dof.enabled        = r.dof.enabled;
        scene.dof.focusDistance  = r.dof.focusDistance;
        scene.dof.focusRange     = r.dof.focusRange;
        scene.dof.bokehRadius    = r.dof.bokehRadius;
        scene.dof.nearTransition = r.dof.nearTransition;
        scene.dof.farTransition  = r.dof.farTransition;

        // Motion blur
        scene.motionBlur.enabled      = r.motionBlur.enabled;
        scene.motionBlur.shutterAngle = r.motionBlur.shutterAngle;
        scene.motionBlur.numSamples   = r.motionBlur.numSamples;

        // Colour grading: load LUT on path change, bind to scene.
        reloadLutIfNeeded(loader, device, r.colorGrading.lutPath);
        scene.colorGrading.enabled    = r.colorGrading.enabled;
        scene.colorGrading.intensity  = r.colorGrading.intensity;
        scene.colorGrading.lutTexture = m_lutTexture.get();  // nullptr = identity LUT

        // Optional FX
        scene.optionalFx.enabled           = r.optionalFx.enabled;
        scene.optionalFx.caAmount          = r.optionalFx.caAmount;
        scene.optionalFx.vignetteIntensity = r.optionalFx.vignetteIntensity;
        scene.optionalFx.vignetteRadius    = r.optionalFx.vignetteRadius;
        scene.optionalFx.grainAmount       = r.optionalFx.grainAmount;

        // Upscaling
        scene.upscaling.mode = r.upscaling.fxaaEnabled
            ? render::UpscalingMode::FXAA
            : render::UpscalingMode::None;

        // Ray tracing
        scene.renderMode           = r.rt.enabled
            ? render::RenderMode::RayTraced
            : render::RenderMode::Rasterized;
        scene.rt.maxBounces        = r.rt.maxBounces;
        scene.rt.samplesPerPixel   = r.rt.samplesPerPixel;
        scene.rt.denoise           = r.rt.denoise;
    }

    // ── Refresh material albedo textures from catalog (lazy, each frame) ───────
    for (std::size_t si = 0; si < m_draws.size(); ++si)
        for (std::size_t bi = 0; bi < m_draws[si].size(); ++bi)
        {
            const UUID& uuid = m_drawMaterialIds[si][bi];
            m_draws[si][bi].material.albedo = uuid.isValid()
                ? catalog.getOrLoadTexture(uuid, device, loader)
                : nullptr;
        }

    // ── Submit sector draws, filtered through portal traversal when possible ────
    if (m_worldMap && !m_draws.empty())
    {
        const world::SectorId camSector =
            m_worldMap->findSector(glm::vec2(m_eye.x, m_eye.z));

        if (camSector != world::INVALID_SECTOR_ID && m_portalTraversal)
        {
            // Camera is inside a known sector: submit only the visible set.
            const glm::mat4 viewProj = proj * view;
            const auto visible =
                m_portalTraversal->traverse(*m_worldMap, camSector, viewProj);

            for (const auto& vs : visible)
            {
                const std::size_t si = static_cast<std::size_t>(vs.sectorId);
                if (si < m_draws.size())
                {
                    // Convert the NDC portal window to a pixel scissor rect and
                    // stamp it onto every draw in this sector batch so the
                    // G-buffer pass clips geometry to the visible opening.
                    const float fW = static_cast<float>(m_currentW);
                    const float fH = static_cast<float>(m_currentH);
                    const float px0 = (vs.windowMin.x + 1.0f) * 0.5f * fW;
                    const float py0 = (1.0f - vs.windowMax.y) * 0.5f * fH;
                    const float px1 = (vs.windowMax.x + 1.0f) * 0.5f * fW;
                    const float py1 = (1.0f - vs.windowMin.y) * 0.5f * fH;
                    const i32 sx = static_cast<i32>(std::max(0.0f, std::floor(px0)));
                    const i32 sy = static_cast<i32>(std::max(0.0f, std::floor(py0)));
                    const u32 ex = static_cast<u32>(std::min(fW, std::ceil(px1)));
                    const u32 ey = static_cast<u32>(std::min(fH, std::ceil(py1)));
                    const rhi::ScissorRect scissor{
                        sx, sy,
                        ex > static_cast<u32>(sx) ? ex - static_cast<u32>(sx) : 0u,
                        ey > static_cast<u32>(sy) ? ey - static_cast<u32>(sy) : 0u
                    };

                    for (auto draw : m_draws[si])
                    {
                        if (!draw.vertexBuffer) continue;
                        draw.scissorValid = true;
                        draw.scissorRect  = scissor;
                        scene.meshDraws.push_back(draw);
                    }
                }
            }
        }
        else
        {
            // Camera outside map — fall back to submitting all sectors.
            for (const auto& sectorDraws : m_draws)
                for (const auto& draw : sectorDraws)
                    if (draw.vertexBuffer) scene.meshDraws.push_back(draw);
        }
    }
    else
    {
        // No world map yet (empty map) — submit all.
        for (const auto& sectorDraws : m_draws)
            for (const auto& draw : sectorDraws)
                if (draw.vertexBuffer) scene.meshDraws.push_back(draw);
    }

    // ── Mirror surfaces ────────────────────────────────────────────────────────
    // Snapshot sector draws first so reflectedDraws never contain mirror surfaces
    // themselves (prevents feedback and matches the MirrorDraw contract in scene_view.h).
    {
        const std::vector<render::MeshDraw> sectorSnapshot = scene.meshDraws;
        const auto& sectors = doc.mapData().sectors;

        for (std::size_t si = 0; si < sectors.size(); ++si)
        {
            const world::Sector& sector = sectors[si];
            const std::size_t    n      = sector.walls.size();

            for (std::size_t wi = 0; wi < n; ++wi)
            {
                const world::Wall& wall = sector.walls[wi];
                if (!world::hasFlag(wall.flags, world::WallFlags::Mirror)) continue;

                // Wall tangent and inward normal (matches appendWallQuad convention).
                const glm::vec2 p0     = wall.p0;
                const glm::vec2 p1     = sector.walls[(wi + 1) % n].p0;
                const float     dx     = p1.x - p0.x;
                const float     dz     = p1.y - p0.y;
                const float     len    = std::sqrt(dx * dx + dz * dz);
                if (len < 1e-6f) continue;
                const float invLen = 1.0f / len;
                const float wallTx = dx * invLen;  // tangent X
                const float wallTz = dz * invLen;  // tangent Z
                const float nx     = -wallTz;       // inward normal X
                const float nz     =  wallTx;       // inward normal Z

                // Find or lazily create the MirrorEntry for this wall.
                MirrorEntry* entry = nullptr;
                for (auto& e : m_mirrorCache)
                    if (e.sectorId == static_cast<world::SectorId>(si) && e.wallIdx == wi)
                    { entry = &e; break; }

                if (!entry)
                {
                    MirrorEntry newEntry;
                    newEntry.sectorId = static_cast<world::SectorId>(si);
                    newEntry.wallIdx  = wi;

                    // 512×512 BGRA8Unorm render target.
                    {
                        rhi::TextureDescriptor td;
                        td.width     = 512u;
                        td.height    = 512u;
                        td.format    = rhi::TextureFormat::BGRA8Unorm;
                        td.usage     = rhi::TextureUsage::RenderTarget |
                                       rhi::TextureUsage::ShaderRead;
                        td.debugName = "Mirror_" + std::to_string(si) +
                                       "_" + std::to_string(wi);
                        newEntry.renderTarget = device.createTexture(td);
                    }

                    // Quad: p0-floor, p1-floor, p1-ceil, p0-ceil.
                    // Winding matches appendWallQuad: {0,2,1}, {0,3,2}.
                    const float yFloor = sector.floorHeight;
                    const float yCeil  = sector.ceilHeight;
                    render::StaticMeshVertex verts[4];
                    verts[0] = {{ p0.x, yFloor, p0.y }, { nx, 0.0f, nz }, { 0.0f, 1.0f }, { wallTx, 0.0f, wallTz, 1.0f }};
                    verts[1] = {{ p1.x, yFloor, p1.y }, { nx, 0.0f, nz }, { 1.0f, 1.0f }, { wallTx, 0.0f, wallTz, 1.0f }};
                    verts[2] = {{ p1.x, yCeil,  p1.y }, { nx, 0.0f, nz }, { 1.0f, 0.0f }, { wallTx, 0.0f, wallTz, 1.0f }};
                    verts[3] = {{ p0.x, yCeil,  p0.y }, { nx, 0.0f, nz }, { 0.0f, 0.0f }, { wallTx, 0.0f, wallTz, 1.0f }};
                    const unsigned indices[6] = { 0u, 2u, 1u, 0u, 3u, 2u };

                    {
                        rhi::BufferDescriptor bd;
                        bd.size      = sizeof(verts);
                        bd.usage     = rhi::BufferUsage::Vertex;
                        bd.initData  = verts;
                        bd.debugName = "MirrorVBO_" + std::to_string(si) +
                                       "_" + std::to_string(wi);
                        newEntry.vbo = device.createBuffer(bd);
                    }
                    {
                        rhi::BufferDescriptor bd;
                        bd.size      = sizeof(indices);
                        bd.usage     = rhi::BufferUsage::Index;
                        bd.initData  = indices;
                        bd.debugName = "MirrorIBO_" + std::to_string(si) +
                                       "_" + std::to_string(wi);
                        newEntry.ibo = device.createBuffer(bd);
                    }

                    m_mirrorCache.push_back(std::move(newEntry));
                    entry = &m_mirrorCache.back();
                }

                if (!entry->renderTarget || !entry->vbo || !entry->ibo) continue;

                // Reflect m_eye and fwd across the mirror plane.
                const glm::vec3 mirrorN(nx, 0.0f, nz);
                const glm::vec3 wallMid(
                    (p0.x + p1.x) * 0.5f,
                    (sector.floorHeight + sector.ceilHeight) * 0.5f,
                    (p0.y + p1.y) * 0.5f);
                const float     mirrorD  = glm::dot(mirrorN, wallMid);
                const float     eyeDist  = glm::dot(mirrorN, m_eye) - mirrorD;
                const glm::vec3 reflEye  = m_eye - 2.0f * eyeDist * mirrorN;
                const glm::vec3 reflFwd  = fwd   - 2.0f * glm::dot(mirrorN, fwd) * mirrorN;
                const glm::mat4 reflView = glm::lookAtLH(
                    reflEye, reflEye + reflFwd, glm::vec3(0.0f, 1.0f, 0.0f));

                // Populate MirrorDraw.
                render::MirrorDraw md;
                md.renderTarget   = entry->renderTarget.get();
                md.rtWidth        = 512u;
                md.rtHeight       = 512u;
                md.reflectedView  = reflView;
                md.reflectedProj  = proj;
                md.reflectedDraws = sectorSnapshot;
                scene.mirrors.push_back(std::move(md));

                // Mirror surface draw (isMirrorSurface + albedo = render target).
                render::MeshDraw surfDraw;
                surfDraw.vertexBuffer          = entry->vbo.get();
                surfDraw.indexBuffer           = entry->ibo.get();
                surfDraw.indexCount            = 6u;
                surfDraw.modelMatrix           = glm::mat4(1.0f);
                surfDraw.prevModel             = glm::mat4(1.0f);
                surfDraw.material.isMirrorSurface = true;
                surfDraw.material.albedo          = entry->renderTarget.get();
                surfDraw.material.roughness        = 0.0f;
                scene.meshDraws.push_back(std::move(surfDraw));
            }
        }
    }

    // Entities: populate then backfill global ambient so entity draws receive
    // correct ambient lighting (they have no sector, so use the map global value).
    {
        const glm::vec3 globalAmb = doc.mapData().globalAmbientColor
                                    * doc.mapData().globalAmbientIntensity;
        const std::size_t meshBefore  = scene.meshDraws.size();
        const std::size_t transBefore = scene.transparentDraws.size();
        m_entityCache.populateSceneView(scene, doc.entities(), view);
        for (std::size_t i = meshBefore; i < scene.meshDraws.size(); ++i)
            scene.meshDraws[i].material.sectorAmbient = globalAmb;
        for (std::size_t i = transBefore; i < scene.transparentDraws.size(); ++i)
            scene.transparentDraws[i].material.sectorAmbient = globalAmb;
    }
    render::sortTransparentDraws(scene);

    // ── Render ────────────────────────────────────────────────────────────────
    m_renderer.renderFrame(device, queue, *m_offscreenSwap, scene, w, h);

    // ── Display ─────────────────────────────────────────────────────────────
    rhi::ITexture* tex = m_offscreenSwap->nextDrawable();
    const ImVec2 imageTopLeft = ImGui::GetCursorScreenPos();
    ImGui::Image(reinterpret_cast<ImTextureID>(tex->nativeHandle()),
                 ImVec2(static_cast<float>(w), static_cast<float>(h)));

    // ── Drag-and-drop material target ────────────────────────────────────────
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("MATERIAL_UUID"))
        {
            const UUID droppedUuid =
                *static_cast<const UUID*>(payload->Data);
            if (m_hoveredSectorId != world::INVALID_SECTOR_ID)
            {
                if (m_hoveredSurface == HoveredSurface::Wall)
                    doc.pushCommand(std::make_unique<CmdSetWallMaterial>(
                        doc, m_hoveredSectorId, m_hoveredWallIdx,
                        WallSurface::Front, droppedUuid));
                else if (m_hoveredSurface == HoveredSurface::UpperWall)
                    doc.pushCommand(std::make_unique<CmdSetWallMaterial>(
                        doc, m_hoveredSectorId, m_hoveredWallIdx,
                        WallSurface::Upper, droppedUuid));
                else if (m_hoveredSurface == HoveredSurface::LowerWall)
                    doc.pushCommand(std::make_unique<CmdSetWallMaterial>(
                        doc, m_hoveredSectorId, m_hoveredWallIdx,
                        WallSurface::Lower, droppedUuid));
                else if (m_hoveredSurface == HoveredSurface::Floor)
                    doc.pushCommand(std::make_unique<CmdSetSectorMaterial>(
                        doc, m_hoveredSectorId, SectorSurface::Floor, droppedUuid));
                else if (m_hoveredSurface == HoveredSurface::Ceil)
                    doc.pushCommand(std::make_unique<CmdSetSectorMaterial>(
                        doc, m_hoveredSectorId, SectorSurface::Ceil, droppedUuid));
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Overlay: show a hint when mouse is captured.
    if (m_mouseCaptured)
    {
        ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
        ImGui::TextDisabled("[Tab] release cursor   WASD = move   Q/E = up/down   Shift = fast");
    }
    else
    {
        ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
        ImGui::TextDisabled("[Tab] capture   Alt+drag = orbit   Scroll = dolly   F = frame   P = player   M = pick surface material");
    }

    // RT mode badge — top-right corner.  Gives instant confirmation of which
    // render path is active: green = RT, red = RT requested but no HW, grey = rasterized.
    {
        const bool  rtEnabled = doc.renderSettings().rt.enabled;
        const bool  rtActive  = rtEnabled && device.supportsRayTracing();
        const char* label     = rtActive  ? "RT"
                              : rtEnabled ? "RT (no HW support)"
                              :             "RAST";
        const ImVec2 textSz = ImGui::CalcTextSize(label);
        const float  winW   = ImGui::GetWindowWidth();
        ImGui::SetCursorPos(ImVec2(winW - textSz.x - 8.0f, 8.0f));
        if (rtActive)
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", label);
        else if (rtEnabled)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", label);
        else
            ImGui::TextDisabled("%s", label);
    }

    // ── Wall / floor / ceiling hover ray + click selection ─────────────────────
    const ImGuiIO& io3d = ImGui::GetIO();
    if (!m_mouseCaptured && ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        const ImVec2 mp = io3d.MousePos;
        const float fw = static_cast<float>(w);
        const float fh = static_cast<float>(h);
        const bool inImage = mp.x >= imageTopLeft.x && mp.y >= imageTopLeft.y
                          && mp.x < imageTopLeft.x + fw
                          && mp.y < imageTopLeft.y + fh;

        if (inImage)
        {
            const glm::vec3 rayDir = screenToWorldRay(
                mp, imageTopLeft, w, h, m_eye, view, proj);
            const auto& sectors = doc.mapData().sectors;

            float           closestT   = FLT_MAX;
            world::SectorId hitSector  = world::INVALID_SECTOR_ID;
            std::size_t     hitWall    = 0;
            HoveredSurface  hitSurface = HoveredSurface::None;
            std::size_t     hitEntity  = SIZE_MAX;

            // ── Entity bounding boxes (test first for correct depth priority) ──────────
            const auto& entities = doc.entities();
            for (std::size_t ei = 0; ei < entities.size(); ++ei)
            {
                const EntityDef& ent = entities[ei];
                // Build AABB from entity position and scale
                const glm::vec3 halfExtents = ent.scale * 0.5f;
                const glm::vec3 boxMin = ent.position - halfExtents;
                const glm::vec3 boxMax = ent.position + halfExtents;
                
                float t;
                if (rayBoxIntersect(m_eye, rayDir, boxMin, boxMax, t) && t < closestT)
                {
                    closestT = t;
                    hitEntity = ei;
                }
            }

            // ── Wall triangles: solid walls + portal upper/lower strips ──────────
            for (std::size_t si = 0; si < sectors.size(); ++si)
            {
                const world::Sector& sec = sectors[si];
                const std::size_t    ns  = sec.walls.size();
                for (std::size_t wi = 0; wi < ns; ++wi)
                {
                    const world::Wall& wall = sec.walls[wi];
                    const glm::vec2 p0 = wall.p0;
                    const glm::vec2 p1 = sec.walls[(wi + 1) % ns].p0;

                    // Test two triangles of a wall quad spanning [yBottom, yTop].
                    auto testStrip = [&](float yBottom, float yTop, HoveredSurface surf)
                    {
                        if (yBottom >= yTop) return;
                        const glm::vec3 bl = {p0.x, yBottom, p0.y};
                        const glm::vec3 br = {p1.x, yBottom, p1.y};
                        const glm::vec3 tr = {p1.x, yTop,    p1.y};
                        const glm::vec3 tl = {p0.x, yTop,    p0.y};
                        float t;
                        if (rayTriangleIntersect(m_eye, rayDir, bl, tr, br, t) && t < closestT)
                        { closestT = t; hitSector = static_cast<world::SectorId>(si); hitWall = wi; hitSurface = surf; }
                        if (rayTriangleIntersect(m_eye, rayDir, bl, tl, tr, t) && t < closestT)
                        { closestT = t; hitSector = static_cast<world::SectorId>(si); hitWall = wi; hitSurface = surf; }
                    };

                    if (wall.portalSectorId == world::INVALID_SECTOR_ID)
                    {
                        testStrip(sec.floorHeight, sec.ceilHeight, HoveredSurface::Wall);
                    }
                    else
                    {
                        const auto adjId = static_cast<std::size_t>(wall.portalSectorId);
                        if (adjId >= sectors.size()) continue;
                        const world::Sector& adj = sectors[adjId];
                        if (adj.ceilHeight < sec.ceilHeight)    // upper strip
                            testStrip(adj.ceilHeight, sec.ceilHeight, HoveredSurface::UpperWall);
                        if (adj.floorHeight > sec.floorHeight)  // lower strip
                            testStrip(sec.floorHeight, adj.floorHeight, HoveredSurface::LowerWall);
                    }
                }
            }

            // ── Floor and ceiling (horizontal plane + point-in-sector) ─────────────
            // 2-D ray-casting point-in-polygon (XZ plane).
            auto pointInSector = [](const world::Sector& sec, glm::vec2 pt) -> bool
            {
                const std::size_t n = sec.walls.size();
                int crossings = 0;
                for (std::size_t i = 0; i < n; ++i)
                {
                    const glm::vec2 a = sec.walls[i].p0;
                    const glm::vec2 b = sec.walls[(i + 1) % n].p0;
                    if ((a.y <= pt.y) != (b.y <= pt.y))
                        if (pt.x < a.x + (pt.y - a.y) / (b.y - a.y) * (b.x - a.x))
                            ++crossings;
                }
                return (crossings & 1) != 0;
            };

            constexpr float kMinT = 1e-3f;
            if (std::abs(rayDir.y) > kMinT)
            {
                for (std::size_t si = 0; si < sectors.size(); ++si)
                {
                    const world::Sector& sec = sectors[si];

                    const float tFloor = (sec.floorHeight - m_eye.y) / rayDir.y;
                    if (tFloor > kMinT && tFloor < closestT)
                    {
                        const glm::vec2 xz(m_eye.x + tFloor * rayDir.x,
                                           m_eye.z + tFloor * rayDir.z);
                        if (pointInSector(sec, xz))
                        { closestT = tFloor; hitSector = static_cast<world::SectorId>(si);
                          hitSurface = HoveredSurface::Floor; }
                    }

                    const float tCeil = (sec.ceilHeight - m_eye.y) / rayDir.y;
                    if (tCeil > kMinT && tCeil < closestT)
                    {
                        const glm::vec2 xz(m_eye.x + tCeil * rayDir.x,
                                           m_eye.z + tCeil * rayDir.z);
                        if (pointInSector(sec, xz))
                        { closestT = tCeil; hitSector = static_cast<world::SectorId>(si);
                          hitSurface = HoveredSurface::Ceil; }
                    }
                }
            }

            m_hoveredSectorId = hitSector;
            m_hoveredWallIdx  = hitWall;
            m_hoveredSurface  = hitSurface;
            m_hoveredEntityIdx = hitEntity;

            // Left-click: select hovered surface/entity, or deselect if already selected.
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io3d.KeyAlt)
            {
                SelectionState& sel = doc.selection();
                if (hitEntity != SIZE_MAX)
                {
                    // Entity hit: toggle entity selection
                    if (sel.type == SelectionType::Entity && sel.entityIndex == hitEntity)
                        sel.clear();
                    else
                    {
                        sel.type = SelectionType::Entity;
                        sel.entityIndex = hitEntity;
                    }
                }
                else if (hitSector != world::INVALID_SECTOR_ID)
                {
                    if (hitSurface == HoveredSurface::Wall ||
                        hitSurface == HoveredSurface::UpperWall ||
                        hitSurface == HoveredSurface::LowerWall)
                    {
                        // Toggle: clicking the same wall deselects it.
                        if (sel.type == SelectionType::Wall &&
                            sel.wallSectorId == hitSector && sel.wallIndex == hitWall)
                            sel.clear();
                        else
                        {
                            sel.type              = SelectionType::Wall;
                            sel.wallSectorId      = hitSector;
                            sel.wallIndex         = hitWall;
                            m_selectedWallSurface = hitSurface;
                        }
                    }
                    else
                    {
                        // Floor or ceiling: select/deselect the owning sector.
                        if (sel.type == SelectionType::Sector &&
                            !sel.sectors.empty() && sel.sectors[0] == hitSector &&
                            m_selectedSurface == hitSurface)
                            sel.clear();
                        else
                        {
                            sel.type          = SelectionType::Sector;
                            sel.sectors       = {hitSector};
                            m_selectedSurface = hitSurface;
                        }
                    }
                }
            }
        }
        else
        {
            m_hoveredSectorId = world::INVALID_SECTOR_ID;
            m_hoveredSurface  = HoveredSurface::None;
            m_hoveredEntityIdx = SIZE_MAX;
        }
    }
    else if (m_mouseCaptured)
    {
        m_hoveredSectorId = world::INVALID_SECTOR_ID;
        m_hoveredSurface  = HoveredSurface::None;
        m_hoveredEntityIdx = SIZE_MAX;
    }

    // ── Wall outline overlay ────────────────────────────────────────────────────────────
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float fw = static_cast<float>(w);
        const float fh = static_cast<float>(h);
        const auto& sectors = doc.mapData().sectors;
        const glm::mat4 VP   = proj * view;
        constexpr float kNearEps = 1e-4f;

        // Project a clip-space point to ImGui screen space.
        auto projClip = [&](const glm::vec4& c) -> ImVec2
        {
            return ImVec2(
                imageTopLeft.x + (c.x / c.w * 0.5f + 0.5f) * fw,
                imageTopLeft.y + (1.0f - (c.y / c.w * 0.5f + 0.5f)) * fh);
        };

        // Draw the boundary edges of a sector polygon at a given Y height.
        // Uses Sutherland-Hodgman near-plane clipping on the full polygon so the
        // near-plane cap edge is generated when multiple vertices are behind the camera.
        auto drawSectorOutline = [&](world::SectorId sid, float y, ImU32 lineCol)
        {
            if (sid >= static_cast<world::SectorId>(sectors.size())) return;
            const world::Sector& sec = sectors[sid];
            const std::size_t ns = sec.walls.size();
            if (ns < 2) return;

            // Build clip-space polygon.
            std::vector<glm::vec4> in(ns);
            for (std::size_t i = 0; i < ns; ++i)
                in[i] = VP * glm::vec4(sec.walls[i].p0.x, y, sec.walls[i].p0.y, 1.0f);

            // Sutherland-Hodgman clip against near plane (w > kNearEps).
            std::vector<glm::vec4> poly;
            poly.reserve(ns + 1);
            for (std::size_t i = 0; i < ns; ++i)
            {
                const glm::vec4& cur  = in[i];
                const glm::vec4& next = in[(i + 1) % ns];
                const bool curIn  = cur.w  > kNearEps;
                const bool nextIn = next.w > kNearEps;
                if (curIn)
                    poly.push_back(cur);
                if (curIn != nextIn)
                {
                    const float t = (kNearEps - cur.w) / (next.w - cur.w);
                    poly.push_back(glm::mix(cur, next, t));
                }
            }
            if (poly.size() < 2) return;

            for (std::size_t i = 0; i < poly.size(); ++i)
                dl->AddLine(projClip(poly[i]), projClip(poly[(i + 1) % poly.size()]),
                            lineCol, 2.0f);
        };

        // Draw a wall quad outline using Sutherland-Hodgman near-plane clipping.
        // Clips all 4 corners as a polygon so the near-plane "cap" edge is always
        // drawn even when multiple corners are behind the camera.
        // yBottom/yTop define the vertical extent of the quad (strip support).
        auto drawWallOutline = [&](world::SectorId sid, std::size_t wi,
                                   float yBottom, float yTop,
                                   ImU32 fillCol, ImU32 lineCol)
        {
            if (sid >= static_cast<world::SectorId>(sectors.size())) return;
            const world::Sector& sec = sectors[sid];
            const std::size_t    ns  = sec.walls.size();
            if (wi >= ns) return;
            const glm::vec2 p0 = sec.walls[wi].p0;
            const glm::vec2 p1 = sec.walls[(wi + 1) % ns].p0;

            // Clip-space corners: bl, br, tr, tl (CCW order).
            const glm::vec4 in[4] = {
                VP * glm::vec4(p0.x, yBottom, p0.y, 1.0f),
                VP * glm::vec4(p1.x, yBottom, p1.y, 1.0f),
                VP * glm::vec4(p1.x, yTop,    p1.y, 1.0f),
                VP * glm::vec4(p0.x, yTop,    p0.y, 1.0f),
            };

            // Sutherland-Hodgman clip against w > kNearEps (near plane).
            // A quad can yield at most 5 vertices after one half-plane clip.
            glm::vec4 poly[5];
            int n = 0;
            for (int i = 0; i < 4; ++i)
            {
                const glm::vec4& cur  = in[i];
                const glm::vec4& next = in[(i + 1) % 4];
                const bool curIn  = cur.w  > kNearEps;
                const bool nextIn = next.w > kNearEps;
                if (curIn)
                    poly[n++] = cur;
                if (curIn != nextIn)
                {
                    const float t = (kNearEps - cur.w) / (next.w - cur.w);
                    poly[n++] = glm::mix(cur, next, t);
                }
            }
            if (n < 2) return;

            // Draw outline edges.
            for (int i = 0; i < n; ++i)
                dl->AddLine(projClip(poly[i]), projClip(poly[(i + 1) % n]),
                            lineCol, 2.0f);

            // Fill: fan triangulation from poly[0].
            if (fillCol & IM_COL32(0, 0, 0, 255))
                for (int i = 1; i + 1 < n; ++i)
                    dl->AddTriangleFilled(projClip(poly[0]),
                                         projClip(poly[i]),
                                         projClip(poly[i + 1]), fillCol);
        };

        // Helper: get strip y-range for a hovered/selected portal wall surface.
        // Returns false if the adjacent sector is not available.
        auto getStripY = [&](world::SectorId sid, std::size_t wi,
                             HoveredSurface surf,
                             float& yBot, float& yTop) -> bool
        {
            if (sid >= static_cast<world::SectorId>(sectors.size())) return false;
            const world::Sector& sec = sectors[sid];
            if (wi >= sec.walls.size()) return false;
            const world::SectorId adjSid = sec.walls[wi].portalSectorId;
            if (adjSid == world::INVALID_SECTOR_ID ||
                static_cast<std::size_t>(adjSid) >= sectors.size()) return false;
            const world::Sector& adj = sectors[adjSid];
            yBot = (surf == HoveredSurface::LowerWall) ? sec.floorHeight : adj.ceilHeight;
            yTop = (surf == HoveredSurface::LowerWall) ? adj.floorHeight : sec.ceilHeight;
            return yBot < yTop;
        };

        // Hover outlines.
        if (m_hoveredSectorId != world::INVALID_SECTOR_ID)
        {
            const world::Sector& hovSec = sectors[m_hoveredSectorId];
            if (m_hoveredSurface == HoveredSurface::Wall)
                drawWallOutline(m_hoveredSectorId, m_hoveredWallIdx,
                                hovSec.floorHeight, hovSec.ceilHeight,
                                0, IM_COL32(255, 255, 255, 200));
            else if (m_hoveredSurface == HoveredSurface::UpperWall ||
                     m_hoveredSurface == HoveredSurface::LowerWall)
            {
                float yBot = hovSec.floorHeight, yTop = hovSec.ceilHeight;
                if (getStripY(m_hoveredSectorId, m_hoveredWallIdx, m_hoveredSurface, yBot, yTop))
                    drawWallOutline(m_hoveredSectorId, m_hoveredWallIdx,
                                    yBot, yTop, 0, IM_COL32(255, 255, 255, 200));
            }
            else if (m_hoveredSurface == HoveredSurface::Floor)
                drawSectorOutline(m_hoveredSectorId,
                                  hovSec.floorHeight,
                                  IM_COL32(255, 255, 255, 200));
            else if (m_hoveredSurface == HoveredSurface::Ceil)
                drawSectorOutline(m_hoveredSectorId,
                                  hovSec.ceilHeight,
                                  IM_COL32(255, 255, 255, 200));
        }

        // Selection outlines.
        const SelectionState& sel2 = doc.selection();
        if (sel2.type == SelectionType::Wall &&
            sel2.wallSectorId != world::INVALID_SECTOR_ID)
        {
            const world::SectorId wSid = sel2.wallSectorId;
            const std::size_t     wWi  = sel2.wallIndex;
            float yBot = 0.0f, yTop = 0.0f;
            bool  useStrip = false;
            if (wSid < static_cast<world::SectorId>(sectors.size()) && wWi < sectors[wSid].walls.size())
            {
                const world::Sector& wSec = sectors[wSid];
                if (m_selectedWallSurface == HoveredSurface::UpperWall ||
                    m_selectedWallSurface == HoveredSurface::LowerWall)
                    useStrip = getStripY(wSid, wWi, m_selectedWallSurface, yBot, yTop);
                if (!useStrip)
                { yBot = wSec.floorHeight; yTop = wSec.ceilHeight; }
            }
            drawWallOutline(wSid, wWi, yBot, yTop,
                            0, IM_COL32(255, 200, 0, 220));
        }
        else if (sel2.type == SelectionType::Sector && !sel2.sectors.empty())
        {
            const world::SectorId sid = sel2.sectors[0];
            if (sid < static_cast<world::SectorId>(sectors.size()))
            {
                const float selY = (m_selectedSurface == HoveredSurface::Ceil)
                    ? sectors[sid].ceilHeight
                    : sectors[sid].floorHeight;
                drawSectorOutline(sid, selY, IM_COL32(255, 200, 0, 220));
            }
        }
        
        // ── Entity box outlines ────────────────────────────────────
        // Draw a 3D wireframe box in world space.
        auto drawBoxOutline = [&](const glm::vec3& center, const glm::vec3& halfExtents, ImU32 col)
        {
            // 8 corners of the box
            const glm::vec3 corners[8] = {
                center + glm::vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z),
                center + glm::vec3( halfExtents.x, -halfExtents.y, -halfExtents.z),
                center + glm::vec3( halfExtents.x, -halfExtents.y,  halfExtents.z),
                center + glm::vec3(-halfExtents.x, -halfExtents.y,  halfExtents.z),
                center + glm::vec3(-halfExtents.x,  halfExtents.y, -halfExtents.z),
                center + glm::vec3( halfExtents.x,  halfExtents.y, -halfExtents.z),
                center + glm::vec3( halfExtents.x,  halfExtents.y,  halfExtents.z),
                center + glm::vec3(-halfExtents.x,  halfExtents.y,  halfExtents.z),
            };
            
            // Project to clip space
            glm::vec4 clip[8];
            for (int i = 0; i < 8; ++i)
                clip[i] = VP * glm::vec4(corners[i], 1.0f);
            
            // Draw 12 edges with proper near-plane clipping
            auto drawEdge = [&](int i0, int i1)
            {
                const glm::vec4& c0 = clip[i0];
                const glm::vec4& c1 = clip[i1];
                const bool v0In = c0.w > kNearEps;
                const bool v1In = c1.w > kNearEps;
                
                if (!v0In && !v1In) return;  // Both behind camera
                
                if (v0In && v1In)
                {
                    // Both in front - draw directly
                    dl->AddLine(projClip(c0), projClip(c1), col, 2.0f);
                }
                else
                {
                    // One vertex behind camera - clip to near plane
                    const float t = (kNearEps - c0.w) / (c1.w - c0.w);
                    const glm::vec4 clipped = glm::mix(c0, c1, t);
                    
                    if (v0In)
                        dl->AddLine(projClip(c0), projClip(clipped), col, 2.0f);
                    else
                        dl->AddLine(projClip(clipped), projClip(c1), col, 2.0f);
                }
            };
            
            // Bottom face
            drawEdge(0, 1); drawEdge(1, 2); drawEdge(2, 3); drawEdge(3, 0);
            // Top face
            drawEdge(4, 5); drawEdge(5, 6); drawEdge(6, 7); drawEdge(7, 4);
            // Vertical edges
            drawEdge(0, 4); drawEdge(1, 5); drawEdge(2, 6); drawEdge(3, 7);
        };
        
        // Hovered entity outline
        if (m_hoveredEntityIdx != SIZE_MAX && m_hoveredEntityIdx < doc.entities().size())
        {
            const EntityDef& ent = doc.entities()[m_hoveredEntityIdx];
            glm::vec3 aabbMin, aabbMax;
            
            // For StaticMesh/VoxelObject, use actual mesh AABB; otherwise fall back to scale-based box.
            if (m_entityCache.getMeshAABB(m_hoveredEntityIdx, aabbMin, aabbMax))
            {
                // Build entity transform matrix.
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, ent.position);
                model = glm::rotate(model, ent.yaw,   glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::rotate(model, ent.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
                model = glm::rotate(model, ent.roll,  glm::vec3(0.0f, 0.0f, 1.0f));
                model = glm::scale(model, ent.scale);
                
                // Transform all 8 corners of the local AABB to world space.
                const glm::vec3 localCorners[8] = {
                    {aabbMin.x, aabbMin.y, aabbMin.z},
                    {aabbMax.x, aabbMin.y, aabbMin.z},
                    {aabbMax.x, aabbMin.y, aabbMax.z},
                    {aabbMin.x, aabbMin.y, aabbMax.z},
                    {aabbMin.x, aabbMax.y, aabbMin.z},
                    {aabbMax.x, aabbMax.y, aabbMin.z},
                    {aabbMax.x, aabbMax.y, aabbMax.z},
                    {aabbMin.x, aabbMax.y, aabbMax.z},
                };
                
                glm::vec3 worldMin(FLT_MAX), worldMax(-FLT_MAX);
                for (const auto& corner : localCorners)
                {
                    const glm::vec3 worldCorner = glm::vec3(model * glm::vec4(corner, 1.0f));
                    worldMin = glm::min(worldMin, worldCorner);
                    worldMax = glm::max(worldMax, worldCorner);
                }
                
                const glm::vec3 center = (worldMin + worldMax) * 0.5f;
                const glm::vec3 halfExtents = (worldMax - worldMin) * 0.5f;
                drawBoxOutline(center, halfExtents, IM_COL32(255, 255, 255, 200));
            }
            else
            {
                // Billboard/non-mesh entities: use scale-based collision box.
                const glm::vec3 halfExtents = ent.scale * 0.5f;
                drawBoxOutline(ent.position, halfExtents, IM_COL32(255, 255, 255, 200));
            }
        }
        
        // Selected entity outline (yellow, drawn on top if same entity)
        if (sel2.type == SelectionType::Entity && sel2.entityIndex < doc.entities().size())
        {
            const EntityDef& ent = doc.entities()[sel2.entityIndex];
            glm::vec3 aabbMin, aabbMax;
            
            // For StaticMesh/VoxelObject, use actual mesh AABB; otherwise fall back to scale-based box.
            if (m_entityCache.getMeshAABB(sel2.entityIndex, aabbMin, aabbMax))
            {
                // Build entity transform matrix.
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, ent.position);
                model = glm::rotate(model, ent.yaw,   glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::rotate(model, ent.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
                model = glm::rotate(model, ent.roll,  glm::vec3(0.0f, 0.0f, 1.0f));
                model = glm::scale(model, ent.scale);
                
                // Transform all 8 corners of the local AABB to world space.
                const glm::vec3 localCorners[8] = {
                    {aabbMin.x, aabbMin.y, aabbMin.z},
                    {aabbMax.x, aabbMin.y, aabbMin.z},
                    {aabbMax.x, aabbMin.y, aabbMax.z},
                    {aabbMin.x, aabbMin.y, aabbMax.z},
                    {aabbMin.x, aabbMax.y, aabbMin.z},
                    {aabbMax.x, aabbMax.y, aabbMin.z},
                    {aabbMax.x, aabbMax.y, aabbMax.z},
                    {aabbMin.x, aabbMax.y, aabbMax.z},
                };
                
                glm::vec3 worldMin(FLT_MAX), worldMax(-FLT_MAX);
                for (const auto& corner : localCorners)
                {
                    const glm::vec3 worldCorner = glm::vec3(model * glm::vec4(corner, 1.0f));
                    worldMin = glm::min(worldMin, worldCorner);
                    worldMax = glm::max(worldMax, worldCorner);
                }
                
                const glm::vec3 center = (worldMin + worldMax) * 0.5f;
                const glm::vec3 halfExtents = (worldMax - worldMin) * 0.5f;
                drawBoxOutline(center, halfExtents, IM_COL32(255, 200, 0, 220));
            }
            else
            {
                // Billboard/non-mesh entities: use scale-based collision box.
                const glm::vec3 halfExtents = ent.scale * 0.5f;
                drawBoxOutline(ent.position, halfExtents, IM_COL32(255, 200, 0, 220));
            }
        }
    }

    // ── M key: open in-viewport material picker popup ────────────────────────
    if (!m_mouseCaptured && ImGui::IsWindowHovered(ImGuiHoveredFlags_None))
    {
        const SelectionState& selM = doc.selection();
        if (ImGui::IsKeyPressed(ImGuiKey_M, /*repeat=*/false) &&
            selM.type == SelectionType::Wall)
        {
            // Use the hovered surface to pick the right material slot so hovering
            // an upper/lower strip and pressing M assigns to that slot.
            const world::SectorId capSid  = selM.wallSectorId;
            const std::size_t     capWi   = selM.wallIndex;
            WallSurface           capSurf = WallSurface::Front;
            const char*           capLbl  = "Wall Front";
            if (m_hoveredSectorId == capSid && m_hoveredWallIdx == capWi)
            {
                if      (m_hoveredSurface == HoveredSurface::UpperWall)
                { capSurf = WallSurface::Upper; capLbl = "Wall Upper"; }
                else if (m_hoveredSurface == HoveredSurface::LowerWall)
                { capSurf = WallSurface::Lower; capLbl = "Wall Lower"; }
            }
            assetBrowser.openPicker(
                [&doc, &catalog, capSid, capWi, capSurf](const UUID& uuid)
                {
                    // Compute wall dimensions at the moment of assignment.
                    const auto& sectors = doc.mapData().sectors;
                    if (capSid >= static_cast<world::SectorId>(sectors.size())) return;
                    const auto& sec = sectors[capSid];
                    if (capWi >= sec.walls.size()) return;
                    const glm::vec2 p0  = sec.walls[capWi].p0;
                    const glm::vec2 p1  = sec.walls[(capWi + 1) % sec.walls.size()].p0;
                    const float wallLen = glm::length(p1 - p0);
                    const float wallHt  = sec.ceilHeight - sec.floorHeight;

                    // Pixel-perfect scale if dims are known, else stretch to fit.
                    glm::vec2 newScale{wallLen, wallHt};
                    const MaterialEntry* entry = catalog.find(uuid);
                    if (entry && entry->texWidth > 0 && entry->texHeight > 0)
                        newScale = computePixelPerfectUVScale(entry->texWidth, entry->texHeight);

                    std::vector<std::unique_ptr<ICommand>> steps;
                    steps.push_back(std::make_unique<CmdSetWallMaterial>(
                        doc, capSid, capWi, capSurf, uuid));
                    steps.push_back(std::make_unique<CmdSetWallUV>(
                        doc, capSid, capWi, glm::vec2{0.0f, 0.0f}, newScale, 0.0f));
                    doc.pushCommand(std::make_unique<CompoundCommand>(
                        "Apply Wall Material", std::move(steps)));
                },
                capLbl);
        }
    }

    // ── UV keyboard shortcuts ──────────────────────────────────────────────────
    // Active when the 3D viewport is hovered, not in mouselook, and a wall is
    // selected.  A single press applies a discrete change and pushes an undo entry.
    if (!m_mouseCaptured && ImGui::IsWindowHovered(ImGuiHoveredFlags_None) &&
        !ImGui::GetIO().WantCaptureKeyboard)
    {
        const SelectionState& selUV = doc.selection();
        if (selUV.type == SelectionType::Wall)
        {
            const world::SectorId uvSid = selUV.wallSectorId;
            const std::size_t     uvWi  = selUV.wallIndex;
            auto& uvSectors = doc.mapData().sectors;

            if (uvSid < static_cast<world::SectorId>(uvSectors.size()) &&
                uvWi  < uvSectors[uvSid].walls.size())
            {
                world::Wall& uvWall = uvSectors[uvSid].walls[uvWi];

                // Helper: push a CmdSetWallUV with new values (constructor reads
                // the CURRENT wall as "old", so call it before modifying the wall).
                auto pushUV = [&](glm::vec2 newOff, glm::vec2 newSc, float newRot)
                {
                    doc.pushCommand(std::make_unique<CmdSetWallUV>(
                        doc, uvSid, uvWi, newOff, newSc, newRot));
                };

                constexpr float kNudge  = 0.1f;
                constexpr float kScale  = 0.1f;
                constexpr float kRotStep = glm::pi<float>() / 12.0f;  // 15°

                const bool altHeld   = ImGui::GetIO().KeyAlt;
                const bool shiftHeld = ImGui::GetIO().KeyShift;

                // Arrow keys: nudge offset.  Alt+Arrow is reserved for alignment (below).
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !altHeld)
                    pushUV({uvWall.uvOffset.x, uvWall.uvOffset.y + kNudge},
                           uvWall.uvScale, uvWall.uvRotation);
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !altHeld)
                    pushUV({uvWall.uvOffset.x, uvWall.uvOffset.y - kNudge},
                           uvWall.uvScale, uvWall.uvRotation);
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !altHeld)
                    pushUV({uvWall.uvOffset.x - kNudge, uvWall.uvOffset.y},
                           uvWall.uvScale, uvWall.uvRotation);
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !altHeld)
                    pushUV({uvWall.uvOffset.x + kNudge, uvWall.uvOffset.y},
                           uvWall.uvScale, uvWall.uvRotation);

                // [ / ] — scale U.
                if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
                    pushUV(uvWall.uvOffset,
                           {std::max(0.01f, uvWall.uvScale.x - kScale), uvWall.uvScale.y},
                           uvWall.uvRotation);
                if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))
                    pushUV(uvWall.uvOffset,
                           {uvWall.uvScale.x + kScale, uvWall.uvScale.y},
                           uvWall.uvRotation);

                // , / . — scale V;  Shift+,/. — rotate ±15°.
                // Checked with a single IsKeyPressed per key to prevent double-fire.
                if (ImGui::IsKeyPressed(ImGuiKey_Comma))
                {
                    if (shiftHeld)
                        pushUV(uvWall.uvOffset, uvWall.uvScale,
                               uvWall.uvRotation - kRotStep);
                    else
                        pushUV(uvWall.uvOffset,
                               {uvWall.uvScale.x, std::max(0.01f, uvWall.uvScale.y - kScale)},
                               uvWall.uvRotation);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Period))
                {
                    if (shiftHeld)
                        pushUV(uvWall.uvOffset, uvWall.uvScale,
                               uvWall.uvRotation + kRotStep);
                    else
                        pushUV(uvWall.uvOffset,
                               {uvWall.uvScale.x, uvWall.uvScale.y + kScale},
                               uvWall.uvRotation);
                }

                // H — flip U.  Shift+H — flip V.
                if (ImGui::IsKeyPressed(ImGuiKey_H, /*repeat=*/false))
                {
                    if (shiftHeld)
                        pushUV(uvWall.uvOffset,
                               {uvWall.uvScale.x, -uvWall.uvScale.y},
                               uvWall.uvRotation);
                    else
                        pushUV(uvWall.uvOffset,
                               {-uvWall.uvScale.x, uvWall.uvScale.y},
                               uvWall.uvRotation);
                }

                // Backspace — reset UVs to default.
                if (ImGui::IsKeyPressed(ImGuiKey_Backspace, /*repeat=*/false))
                    pushUV({0.0f, 0.0f}, {1.0f, 1.0f}, 0.0f);

                // N — pixel-perfect scale using the front material's native dimensions.
                // (P is reserved for snap-to-player-start; see navigation block above.)
                if (ImGui::IsKeyPressed(ImGuiKey_N, /*repeat=*/false))
                {
                    const MaterialEntry* entry = catalog.find(uvWall.frontMaterialId);
                    if (entry && entry->texWidth > 0 && entry->texHeight > 0)
                        pushUV(uvWall.uvOffset,
                               computePixelPerfectUVScale(entry->texWidth, entry->texHeight),
                               uvWall.uvRotation);
                }

                // Shift+/ — square scale: set V scale equal to U scale.
                if (ImGui::IsKeyPressed(ImGuiKey_Slash, /*repeat=*/false) && shiftHeld)
                    pushUV(uvWall.uvOffset,
                           {uvWall.uvScale.x, uvWall.uvScale.x},
                           uvWall.uvRotation);

                // C — copy UV to clipboard.
                if (ImGui::IsKeyPressed(ImGuiKey_C, /*repeat=*/false))
                {
                    m_uvClipValid    = true;
                    m_uvClipOffset   = uvWall.uvOffset;
                    m_uvClipScale    = uvWall.uvScale;
                    m_uvClipRotation = uvWall.uvRotation;
                }

                // Enter — paste UV from clipboard.
                // Shift+Enter pastes scale+rotation only (preserves current offset).
                if (ImGui::IsKeyPressed(ImGuiKey_Enter, /*repeat=*/false) && m_uvClipValid)
                {
                    if (shiftHeld)
                        pushUV(uvWall.uvOffset, m_uvClipScale, m_uvClipRotation);
                    else
                        pushUV(m_uvClipOffset, m_uvClipScale, m_uvClipRotation);
                }

                // Alt+Left — align offset so this wall continues from the left (previous) wall.
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, /*repeat=*/false) && altHeld)
                {
                    const auto& sec  = uvSectors[uvSid];
                    const std::size_t n = sec.walls.size();
                    const std::size_t prevWi = (uvWi + n - 1) % n;
                    const world::Wall& prevW  = sec.walls[prevWi];
                    const float prevLen =
                        glm::length(sec.walls[uvWi].p0 - prevW.p0);
                    const float prevScaleX =
                        (prevW.uvScale.x > 1e-6f) ? prevW.uvScale.x : 1.0f;
                    pushUV({prevW.uvOffset.x + prevLen / prevScaleX, prevW.uvOffset.y},
                           uvWall.uvScale, uvWall.uvRotation);
                }

                // Alt+Right — align offset so this wall ends where the right (next) wall begins.
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, /*repeat=*/false) && altHeld)
                {
                    const auto& sec  = uvSectors[uvSid];
                    const std::size_t n = sec.walls.size();
                    const std::size_t nextWi = (uvWi + 1) % n;
                    const world::Wall& nextW  = sec.walls[nextWi];
                    const glm::vec2 curP1 = nextW.p0;
                    const float curLen =
                        glm::length(curP1 - uvWall.p0);
                    const float curScaleX =
                        (uvWall.uvScale.x > 1e-6f) ? uvWall.uvScale.x : 1.0f;
                    pushUV({nextW.uvOffset.x - curLen / curScaleX, nextW.uvOffset.y},
                           uvWall.uvScale, uvWall.uvRotation);
                }
            }
        }
    }

    // ── Gizmo overlay (drawn on top of the image) ──────────────────────
    drawGizmoAndHandleDrag(doc, view, proj, imageTopLeft, w, h);

    // ── Forward-vector overlay (shown while 2D viewport is rotating an entity) ───
    if (entityRotating && entityRotIdx < doc.entities().size())
    {
        const EntityDef& ed  = doc.entities()[entityRotIdx];
        const float      fw  = static_cast<float>(w);
        const float      fh  = static_cast<float>(h);

        auto project3d = [&](glm::vec3 world) -> ImVec2
        {
            glm::vec4 clip = proj * view * glm::vec4(world, 1.0f);
            if (clip.w <= 0.0f) return ImVec2(-1e5f, -1e5f);
            const float nx = clip.x / clip.w;
            const float ny = clip.y / clip.w;
            return ImVec2(
                imageTopLeft.x + (nx * 0.5f + 0.5f) * fw,
                imageTopLeft.y + (1.0f - (ny * 0.5f + 0.5f)) * fh);
        };

        const glm::vec3 fwdDir(std::sin(ed.yaw), 0.0f, std::cos(ed.yaw));
        constexpr float kVecLen = 3.0f;
        const ImVec2 from = project3d(ed.position);
        const ImVec2 to   = project3d(ed.position + fwdDir * kVecLen);

        ImDrawList* dlFwd = ImGui::GetWindowDrawList();
        constexpr ImU32 kFwdCol = IM_COL32(255, 200, 50, 230);
        dlFwd->AddLine(from, to, kFwdCol, 2.5f);

        // Arrowhead at tip.
        const float ddx  = to.x - from.x;
        const float ddy  = to.y - from.y;
        const float dlen = std::sqrt(ddx * ddx + ddy * ddy);
        if (dlen > 4.0f)
        {
            const float ux = ddx / dlen,  uy = ddy / dlen;
            const float px = -uy,         py =  ux;
            dlFwd->AddTriangleFilled(
                to,
                ImVec2{to.x - ux * 8.0f + px * 4.0f, to.y - uy * 8.0f + py * 4.0f},
                ImVec2{to.x - ux * 8.0f - px * 4.0f, to.y - uy * 8.0f - py * 4.0f},
                kFwdCol);
        }
    }

    // ── Bookkeeping ───────────────────────────────────────────────────────────
    m_prevView = view;
    m_prevProj = proj;
    ++m_frameIdx;

    ImGui::End();
}

} // namespace daedalus::editor
