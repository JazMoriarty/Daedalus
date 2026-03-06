// scene_view.h
// Per-frame scene description passed from the application to FrameRenderer.

#pragma once

#include "daedalus/core/types.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/render/rhi/i_texture.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <vector>

namespace daedalus::render
{

// ─── Material ─────────────────────────────────────────────────────────────────
// PBR material properties for a single draw call.
// All texture pointers are non-owning; the owner is responsible for lifetime.
// nullptr textures fall back to 1×1 engine-owned defaults in FrameRenderer.

struct Material
{
    rhi::ITexture* albedo    = nullptr;  ///< sRGB albedo map.  nullptr → opaque white.
    rhi::ITexture* normalMap = nullptr;  ///< Tangent-space normal map. nullptr → flat (0,0,1).
    rhi::ITexture* emissive  = nullptr;  ///< Linear emissive map.     nullptr → black.
    f32 roughness = 0.5f;               ///< Scalar override (0 = mirror, 1 = fully rough).
    f32 metalness = 0.0f;               ///< Scalar override (0 = dielectric, 1 = metal).

    /// Per-draw albedo tint (rgb) and opacity multiplier (a).
    /// Ignored by the opaque G-buffer shader; consumed by the transparent forward shader.
    /// Default: opaque white (no tint, no opacity change).
    glm::vec4 tint = glm::vec4(1.0f);
};

// ─── MeshDraw ─────────────────────────────────────────────────────────────────
// A single draw call: geometry pointers + per-instance transform + material.

struct MeshDraw
{
    rhi::IBuffer*  vertexBuffer = nullptr;         ///< Interleaved vertices (stride 48B)
    rhi::IBuffer*  indexBuffer  = nullptr;         ///< u32 indices
    u32            indexCount   = 0;

    glm::mat4      modelMatrix  = glm::mat4(1.0f);
    glm::mat4      prevModel    = glm::mat4(1.0f); ///< For TAA motion vectors

    Material       material;                       ///< PBR material for this draw
};

// ─── PointLight ───────────────────────────────────────────────────────────────

struct PointLight
{
    glm::vec3 position;
    f32       radius    = 1.0f;
    glm::vec3 color     = glm::vec3(1.0f);
    f32       intensity = 1.0f;
};

// ─── SpotLight ────────────────────────────────────────────────────────────────
// A cone-shaped light that casts a shadow map.

struct SpotLight
{
    glm::vec3 position;
    glm::vec3 direction;                          ///< Normalised, points into cone
    f32       innerConeAngle = glm::radians(15.0f); ///< radians — full brightness inside
    f32       outerConeAngle = glm::radians(30.0f); ///< radians — falloff to zero at edge
    f32       range          = 10.0f;
    glm::vec3 color          = glm::vec3(1.0f);
    f32       intensity      = 10.0f;
};

// ─── SceneView ────────────────────────────────────────────────────────────────
// Complete frame description: camera + lights + draw list.

struct SceneView
{
    // ─── Camera ───────────────────────────────────────────────────────────────

    glm::mat4 view     = glm::mat4(1.0f);
    glm::mat4 proj     = glm::mat4(1.0f);
    glm::mat4 prevView = glm::mat4(1.0f);  ///< Previous frame view (TAA)
    glm::mat4 prevProj = glm::mat4(1.0f);  ///< Previous frame proj (TAA)

    glm::vec3 cameraPos = glm::vec3(0.0f);
    glm::vec3 cameraDir = glm::vec3(0.0f, 0.0f, -1.0f);

    // ─── Sun / directional light ──────────────────────────────────────────────

    glm::vec3 sunDirection = glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));
    glm::vec3 sunColor     = glm::vec3(1.0f, 0.95f, 0.8f);
    f32       sunIntensity = 3.0f;
    glm::vec3 ambientColor = glm::vec3(0.05f, 0.05f, 0.08f);

    // ─── Point lights ─────────────────────────────────────────────────────────

    std::vector<PointLight> pointLights;

    // ─── Spot lights ───────────────────────────────────────────────────────────────
    // Only the first spot light casts a shadow map.

    std::vector<SpotLight> spotLights;

    // ─── Mesh draw list ─────────────────────────────────────────────────
    // Populated by the application each frame.  FrameRenderer iterates this
    // list for both the shadow depth pass and the G-buffer pass.

    std::vector<MeshDraw> meshDraws;

    // ─── Transparent draw list ─────────────────────────────────────────────────────────
    // Alpha-blended draws rendered in the forward transparency pass (Pass 6).
    // Must be sorted back-to-front with sortTransparentDraws() before submission
    // to FrameRenderer.  Not submitted to the shadow or G-buffer passes.

    std::vector<MeshDraw> transparentDraws;

    // ─── Timing ───────────────────────────────────────────────────────────────────────────

    f32 time      = 0.0f;
    f32 deltaTime = 0.0f;
    u32 frameIndex = 0;
};

// ─── sortTransparentDraws ───────────────────────────────────────────────────────────────────────
// Sort SceneView::transparentDraws back-to-front (farthest first) by squared
// distance from scene.cameraPos.  Must be called after all transparent draws
// are appended and before FrameRenderer::renderFrame.
//
// Position is taken from the translation column of each draw's modelMatrix,
// which is exact for billboard sprites and a good approximation for meshes.
//
// @param scene  SceneView whose transparentDraws list is sorted in place.
inline void sortTransparentDraws(SceneView& scene) noexcept
{
    const glm::vec3 cam = scene.cameraPos;
    std::sort(scene.transparentDraws.begin(), scene.transparentDraws.end(),
        [&cam](const MeshDraw& a, const MeshDraw& b) noexcept
        {
            const glm::vec3 pa = glm::vec3(a.modelMatrix[3]);
            const glm::vec3 pb = glm::vec3(b.modelMatrix[3]);
            const float da = glm::dot(pa - cam, pa - cam);
            const float db = glm::dot(pb - cam, pb - cam);
            return da > db;  // back-to-front: farthest draw first
        });
}

} // namespace daedalus::render
