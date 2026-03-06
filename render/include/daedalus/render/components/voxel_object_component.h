// voxel_object_component.h
// ECS component that pairs an entity's TransformComponent with a greedy-meshed
// voxel object produced by IAssetLoader::loadVox() + greedyMeshVoxels().
//
// Semantically distinct from StaticMeshComponent even though the runtime
// fields are identical — the separate type gives the editor and ECS tooling
// clear provenance (origin is a MagicaVoxel .vox file, not a glTF mesh).
//
// Runtime fields:
//   vertexBuffer / indexBuffer — non-owning; lifetime owned by the application.
//   material.albedo            — non-owning pointer to the 256×1 RGBA8 palette
//                                texture uploaded from VoxMeshResult::paletteRGBA.
//   prevModelMatrix            — maintained by voxelRenderSystem for TAA.

#pragma once

#include "daedalus/render/scene_view.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/core/types.h"

#include <glm/glm.hpp>

namespace daedalus::render
{

// ─── VoxelObjectComponent ─────────────────────────────────────────────────────

struct VoxelObjectComponent
{
    rhi::IBuffer* vertexBuffer    = nullptr;  ///< Interleaved StaticMeshVertex (stride 48B)
    rhi::IBuffer* indexBuffer     = nullptr;  ///< u32 index buffer
    u32           indexCount      = 0;

    Material      material;                   ///< PBR material; albedo = 256×1 palette texture

    /// Previous-frame model matrix, maintained by voxelRenderSystem for TAA.
    /// Initialise to identity; the system populates it after the first frame.
    glm::mat4     prevModelMatrix = glm::mat4(1.0f);
};

} // namespace daedalus::render
