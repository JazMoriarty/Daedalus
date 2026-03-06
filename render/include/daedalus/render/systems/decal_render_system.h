// decal_render_system.h
// ECS system that builds SceneView::decalDraws from all entities with a
// TransformComponent and a DecalComponent.
//
// Each matching entity contributes one DecalDraw to the list.  Entities whose
// DecalComponent::albedoTexture is null are silently skipped (treat as disabled).
//
// The system computes:
//   model    = transform.toMatrix()      — local unit-cube → world space
//   invModel = glm::inverse(model)       — world → local unit-cube  (needed by the
//              fragment shader to reconstruct geometry position in decal local space
//              and clip it to [-0.5, 0.5]^3)
//
// Usage (per frame, before FrameRenderer::renderFrame):
//   render::decalRenderSystem(world, scene);

#pragma once

#include "daedalus/core/ecs/world.h"
#include "daedalus/core/components/transform_component.h"
#include "daedalus/render/components/decal_component.h"
#include "daedalus/render/scene_view.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace daedalus::render
{

/// Populate scene.decalDraws from all ECS entities that have both a
/// TransformComponent and a DecalComponent.
///
/// Entities with a null albedoTexture are skipped (treated as disabled).
///
/// The model matrix is derived from transform.toMatrix(); the inverse is
/// computed once here so the fragment shader does not need to invert per pixel.
///
/// @param world  ECS world to query.
/// @param scene  SceneView to append DecalDraw entries to.
inline void decalRenderSystem(daedalus::World& world, SceneView& scene)
{
    world.each<daedalus::TransformComponent, DecalComponent>(
        [&scene](daedalus::EntityId,
                 const daedalus::TransformComponent& transform,
                 const DecalComponent&               decal)
        {
            // Decals without an albedo texture have no visual effect — skip.
            if (!decal.albedoTexture) { return; }

            const glm::mat4 model    = transform.toMatrix();
            const glm::mat4 invModel = glm::inverse(model);

            DecalDraw draw;
            draw.modelMatrix    = model;
            draw.invModelMatrix = invModel;
            draw.albedoTexture  = decal.albedoTexture;
            draw.normalTexture  = decal.normalTexture;
            draw.roughness      = decal.roughness;
            draw.metalness      = decal.metalness;
            draw.opacity        = decal.opacity;

            scene.decalDraws.push_back(draw);
        });
}

} // namespace daedalus::render
