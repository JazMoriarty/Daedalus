// static_mesh_component.h
// ECS component that pairs an entity's TransformComponent with a GPU mesh.
// Non-owning: the owner (typically the application) is responsible for the
// lifetime of vertexBuffer, indexBuffer, and all material textures.
//
// prevModelMatrix is maintained by MeshRenderSystem for TAA motion vectors:
// the system reads it, writes it to MeshDraw::prevModel, then updates it with
// the current frame's model matrix before returning.

#pragma once

#include "daedalus/render/scene_view.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/core/types.h"

#include <glm/glm.hpp>

namespace daedalus::render
{

// ─── StaticMeshComponent ──────────────────────────────────────────────────────

struct StaticMeshComponent
{
    rhi::IBuffer* vertexBuffer    = nullptr;  ///< Interleaved StaticMeshVertex (stride 48B)
    rhi::IBuffer* indexBuffer     = nullptr;  ///< u32 index buffer
    u32           indexCount      = 0;

    Material      material;                   ///< PBR material (nullptr textures → defaults)

    /// Previous-frame model matrix, maintained by MeshRenderSystem for TAA.
    /// Initialise to identity; the system will populate it after the first frame.
    glm::mat4     prevModelMatrix = glm::mat4(1.0f);
};

} // namespace daedalus::render
