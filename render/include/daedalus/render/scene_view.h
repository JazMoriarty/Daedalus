// scene_view.h
// Per-frame scene description passed from the application to FrameRenderer.

#pragma once

#include "daedalus/core/types.h"
#include "daedalus/render/rhi/i_buffer.h"

#include <glm/glm.hpp>
#include <vector>

namespace daedalus::render
{

// ─── MeshDraw ─────────────────────────────────────────────────────────────────
// A single draw call: geometry pointers + per-instance transform.

struct MeshDraw
{
    rhi::IBuffer* vertexBuffer  = nullptr;  ///< Interleaved vertices (stride 48B)
    rhi::IBuffer* indexBuffer   = nullptr;  ///< u32 indices
    u32           indexCount    = 0;

    glm::mat4     modelMatrix   = glm::mat4(1.0f);
    glm::mat4     prevModel     = glm::mat4(1.0f);  ///< For TAA motion vectors
};

// ─── PointLight ───────────────────────────────────────────────────────────────

struct PointLight
{
    glm::vec3 position;
    f32       radius    = 1.0f;
    glm::vec3 color     = glm::vec3(1.0f);
    f32       intensity = 1.0f;
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

    // ─── Geometry ─────────────────────────────────────────────────────────────

    std::vector<MeshDraw> meshDraws;

    // ─── Timing ───────────────────────────────────────────────────────────────

    f32 time      = 0.0f;
    f32 deltaTime = 0.0f;
    u32 frameIndex = 0;
};

} // namespace daedalus::render
