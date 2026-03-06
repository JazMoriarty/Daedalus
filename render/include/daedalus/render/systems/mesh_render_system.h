// mesh_render_system.h
// ECS render system: queries all entities with TransformComponent +
// StaticMeshComponent and submits a MeshDraw to SceneView::meshDraws for each.
//
// TAA motion vectors:
//   Each StaticMeshComponent stores the previous-frame model matrix in
//   prevModelMatrix.  The system reads it into MeshDraw::prevModel, then
//   overwrites it with the current model matrix so the next frame's delta
//   is correct.
//
// Usage (per frame, before FrameRenderer::renderFrame):
//   daedalus::render::meshRenderSystem(world, scene);

#pragma once

#include "daedalus/core/ecs/world.h"
#include "daedalus/core/components/transform_component.h"
#include "daedalus/render/components/static_mesh_component.h"
#include "daedalus/render/scene_view.h"

namespace daedalus::render
{

/// Populate scene.meshDraws from all ECS entities that have both a
/// TransformComponent and a StaticMeshComponent.
///
/// Entities whose StaticMeshComponent::vertexBuffer or indexBuffer is null
/// are silently skipped.
///
/// TAA motion vectors: reads StaticMeshComponent::prevModelMatrix into
/// MeshDraw::prevModel, then overwrites it with the current model matrix
/// so the next frame's delta is correct.
///
/// @param world  ECS world to query.
/// @param scene  SceneView to append MeshDraw entries to.
inline void meshRenderSystem(daedalus::World& world, SceneView& scene)
{
    world.each<daedalus::TransformComponent, StaticMeshComponent>(
        [&scene](daedalus::EntityId,
                 daedalus::TransformComponent& transform,
                 StaticMeshComponent&          mesh)
        {
            if (!mesh.vertexBuffer || !mesh.indexBuffer) { return; }

            const glm::mat4 model = transform.toMatrix();

            MeshDraw draw;
            draw.vertexBuffer = mesh.vertexBuffer;
            draw.indexBuffer  = mesh.indexBuffer;
            draw.indexCount   = mesh.indexCount;
            draw.modelMatrix  = model;
            draw.prevModel    = mesh.prevModelMatrix;
            draw.material     = mesh.material;

            scene.meshDraws.push_back(draw);

            // Persist current model for next-frame motion vector delta.
            mesh.prevModelMatrix = model;
        });
}

} // namespace daedalus::render
