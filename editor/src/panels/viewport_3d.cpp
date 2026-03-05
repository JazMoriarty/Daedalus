#include "viewport_3d.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/sector_tessellator.h"
#include "daedalus/render/i_asset_loader.h"
#include "daedalus/render/vertex_types.h"
#include "daedalus/render/rhi/rhi_types.h"

#include "imgui.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
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

// ─── retessellate ─────────────────────────────────────────────────────────────

void Viewport3D::retessellate(rhi::IRenderDevice& device,
                               const world::WorldMapData& map)
{
    m_vbos.clear();
    m_ibos.clear();
    m_draws.clear();

    if (map.sectors.empty()) return;

    const auto meshes = world::tessellateMap(map);
    const std::size_t n = meshes.size();

    m_vbos.resize(n);
    m_ibos.resize(n);
    m_draws.resize(n);

    for (std::size_t i = 0; i < n; ++i)
    {
        const render::MeshData& mesh = meshes[i];
        if (mesh.vertices.empty()) continue;

        {
            rhi::BufferDescriptor d;
            d.size      = mesh.vertices.size() * sizeof(render::StaticMeshVertex);
            d.usage     = rhi::BufferUsage::Vertex;
            d.initData  = mesh.vertices.data();
            d.debugName = "EditorSectorVBO_" + std::to_string(i);
            m_vbos[i]   = device.createBuffer(d);
        }
        {
            rhi::BufferDescriptor d;
            d.size      = mesh.indices.size() * sizeof(unsigned);
            d.usage     = rhi::BufferUsage::Index;
            d.initData  = mesh.indices.data();
            d.debugName = "EditorSectorIBO_" + std::to_string(i);
            m_ibos[i]   = device.createBuffer(d);
        }

        render::MeshDraw& draw = m_draws[i];
        draw.vertexBuffer      = m_vbos[i].get();
        draw.indexBuffer       = m_ibos[i].get();
        draw.indexCount        = static_cast<unsigned>(mesh.indices.size());
        draw.modelMatrix       = glm::mat4(1.0f);
        draw.prevModel         = glm::mat4(1.0f);
        // Null material pointers — FrameRenderer uses fallback white/flat-normal.
    }
}

// ─── draw ─────────────────────────────────────────────────────────────────────

void Viewport3D::draw(EditMapDocument&    doc,
                       rhi::IRenderDevice& device,
                       rhi::ICommandQueue& queue)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("3D Viewport");
    ImGui::PopStyleVar();

    const ImVec2 sz = ImGui::GetContentRegionAvail();
    const auto w = static_cast<unsigned>(sz.x > 8.0f ? sz.x : 8.0f);
    const auto h = static_cast<unsigned>(sz.y > 8.0f ? sz.y : 8.0f);

    ensureInit(device, w, h);

    // Retessellate if the map geometry changed.
    if (doc.isGeometryDirty())
    {
        retessellate(device, doc.mapData());
        doc.clearGeometryDirty();
    }

    // ── Orbit camera ──────────────────────────────────────────────────────────
    // Compute map bounds to orbit around the map centre.
    float centreX = 0.0f, centreZ = 0.0f;
    if (!doc.mapData().sectors.empty())
    {
        float minX =  FLT_MAX, maxX = -FLT_MAX;
        float minZ =  FLT_MAX, maxZ = -FLT_MAX;
        for (const auto& sector : doc.mapData().sectors)
        {
            for (const auto& wall : sector.walls)
            {
                minX = std::min(minX, wall.p0.x); maxX = std::max(maxX, wall.p0.x);
                minZ = std::min(minZ, wall.p0.y); maxZ = std::max(maxZ, wall.p0.y);
            }
        }
        centreX = (minX + maxX) * 0.5f;
        centreZ = (minZ + maxZ) * 0.5f;
        m_orbitRadius = std::max(6.0f,
                                  std::max(maxX - minX, maxZ - minZ) * 0.85f);
    }

    m_orbitAngle += 0.006f;
    const glm::vec3 eye{
        centreX + std::cos(m_orbitAngle) * m_orbitRadius,
        m_orbitHeight,
        centreZ + std::sin(m_orbitAngle) * m_orbitRadius
    };
    const glm::vec3 target{centreX, 1.5f, centreZ};
    const glm::vec3 up    {0.0f, 1.0f, 0.0f};

    const float fovY   = glm::radians(60.0f);
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    const glm::mat4 view = glm::lookAtLH(eye, target, up);
    const glm::mat4 proj = glm::perspectiveLH_ZO(fovY, aspect, 0.1f, 200.0f);

    // ── Build SceneView ───────────────────────────────────────────────────────
    render::SceneView scene;
    scene.view      = view;
    scene.proj      = proj;
    scene.prevView  = (m_frameIdx == 0u) ? view : m_prevView;
    scene.prevProj  = (m_frameIdx == 0u) ? proj : m_prevProj;
    scene.cameraPos = eye;
    scene.cameraDir = glm::normalize(target - eye);
    scene.frameIndex = m_frameIdx;

    // Ambient + sun.
    scene.sunDirection = glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));
    scene.sunColor     = glm::vec3(1.0f, 0.95f, 0.8f);
    scene.sunIntensity = 2.0f;
    scene.ambientColor = glm::vec3(0.12f, 0.11f, 0.10f);

    // Fill light over the map centre.
    scene.pointLights.push_back({
        glm::vec3(centreX, std::max(4.0f, m_orbitHeight - 1.0f), centreZ),
        std::max(15.0f, m_orbitRadius * 1.2f),
        glm::vec3(1.0f, 0.94f, 0.82f),
        2.5f
    });

    // Add the sector mesh draws.
    for (const auto& draw : m_draws)
        if (draw.vertexBuffer) scene.meshDraws.push_back(draw);

    // ── Render ────────────────────────────────────────────────────────────────
    m_renderer.renderFrame(device, queue, *m_offscreenSwap, scene, w, h);

    // ── Display ───────────────────────────────────────────────────────────────
    // The offscreen texture is now filled (CB committed to queue).
    // ImGui's CB will execute after it, so sampling is safe.
    rhi::ITexture* tex = m_offscreenSwap->nextDrawable();
    ImGui::Image(reinterpret_cast<ImTextureID>(tex->nativeHandle()),
                 ImVec2(static_cast<float>(w), static_cast<float>(h)));

    // ── Bookkeeping ───────────────────────────────────────────────────────────
    m_prevView = view;
    m_prevProj = proj;
    ++m_frameIdx;

    ImGui::End();
}

} // namespace daedalus::editor
