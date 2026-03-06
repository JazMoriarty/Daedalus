// voxel_render_system.h
// ECS render system: queries all entities with TransformComponent +
// VoxelObjectComponent and submits a MeshDraw to SceneView::meshDraws for each.
//
// Voxel objects share the same G-buffer opaque pass as static meshes — no new
// render pass or shader is required.  The palette texture bound as material.albedo
// provides per-face colour via the UV computed in greedyMeshVoxels().
//
// TAA motion vectors: identical treatment to meshRenderSystem — reads
// VoxelObjectComponent::prevModelMatrix into MeshDraw::prevModel, then
// overwrites it with the current model matrix for the next frame.
//
// Usage (per frame, before FrameRenderer::renderFrame):
//   daedalus::render::voxelRenderSystem(world, scene);

#pragma once

#include "daedalus/core/ecs/world.h"
#include "daedalus/core/components/transform_component.h"
#include "daedalus/render/components/voxel_object_component.h"
#include "daedalus/render/scene_view.h"

namespace daedalus::render
{

/// Populate scene.meshDraws from all ECS entities that have both a
/// TransformComponent and a VoxelObjectComponent.
///
/// Entities whose VoxelObjectComponent::vertexBuffer or indexBuffer is null
/// are silently skipped.
///
/// TAA motion vectors: reads VoxelObjectComponent::prevModelMatrix into
/// MeshDraw::prevModel, then overwrites it with the current model matrix
/// so the next frame's delta is correct.
///
/// @param world  ECS world to query.
/// @param scene  SceneView to append MeshDraw entries to.
inline void voxelRenderSystem(daedalus::World& world, SceneView& scene)
{
    world.each<daedalus::TransformComponent, VoxelObjectComponent>(
        [&scene](daedalus::EntityId,
                 daedalus::TransformComponent& transform,
                 VoxelObjectComponent&         vox)
        {
            if (!vox.vertexBuffer || !vox.indexBuffer) { return; }

            const glm::mat4 model = transform.toMatrix();

            MeshDraw draw;
            draw.vertexBuffer = vox.vertexBuffer;
            draw.indexBuffer  = vox.indexBuffer;
            draw.indexCount   = vox.indexCount;
            draw.modelMatrix  = model;
            draw.prevModel    = vox.prevModelMatrix;
            draw.material     = vox.material;

            scene.meshDraws.push_back(draw);

            // Persist current model for next-frame motion vector delta.
            vox.prevModelMatrix = model;
        });
}

} // namespace daedalus::render
