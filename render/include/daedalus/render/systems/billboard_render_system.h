// billboard_render_system.h
// Utilities and ECS system for camera-facing billboard sprite rendering.
//
// Architecture:
//   • A single shared unit-quad (1×1 in the XY plane) is reused by every
//     billboard entity.  Upload it once with makeUnitQuadMesh() + device buffers.
//   • makeBillboardMatrix() builds a spherical-billboard model matrix from the
//     camera view matrix: col0 = camera-right × size.x,
//                          col1 = camera-up    × size.y,
//                          col2 = cross(right, up),
//                          col3 = world position.
//   • billboardRenderSystem() iterates TransformComponent + BillboardSpriteComponent
//     and routes each sprite to the correct draw list based on alphaMode:
//       AlphaMode::Cutout  → scene.meshDraws      (G-buffer, alpha < 0.5 discards)
//       AlphaMode::Blended → scene.transparentDraws (forward pass, alpha blend)
//     For Blended sprites the sprite tint is copied to draw.material.tint.
//
// TAA note:
//   prevModel == modelMatrix each frame (Phase 1D simplification).
//   Sprites that move will show minor TAA ghosting; this can be fixed in a later
//   phase by storing prevTransform in the component.
//
// Usage:
//   // One-time setup:
//   render::MeshData quadData = render::makeUnitQuadMesh();
//   // ... upload quadData → quadVBO, quadIBO ...
//
//   // Per-frame (before sortTransparentDraws and renderFrame):
//   render::billboardRenderSystem(world, scene, view, quadVBO.get(), quadIBO.get());
//   render::sortTransparentDraws(scene);  // sorts scene.transparentDraws back-to-front

#pragma once

#include "daedalus/core/ecs/world.h"
#include "daedalus/core/components/transform_component.h"
#include "daedalus/render/components/billboard_sprite_component.h"
#include "daedalus/render/i_asset_loader.h"   // for MeshData / StaticMeshVertex
#include "daedalus/render/scene_view.h"
#include "daedalus/render/rhi/i_buffer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace daedalus::render
{

/// Build a unit 1×1 quad in the local XY plane, ready for GPU upload.
///
/// Normal = (0,0,1), tangent = (1,0,0,w=+1).
/// UV layout (Metal convention: (0,0) = top-left):
///   v0=BL (0,1)   v1=BR (1,1)   v2=TR (1,0)   v3=TL (0,0)
/// Index winding (0,2,1),(0,3,2) matches the box-mesh vertical-face convention
/// so both render identically through the G-buffer pipeline.
///
/// @return  CPU-side MeshData with 4 vertices and 6 indices.
[[nodiscard]] inline MeshData makeUnitQuadMesh()
{
    using V = StaticMeshVertex;

    // v0 = BL, v1 = BR, v2 = TR, v3 = TL
    //   pos          normal      uv       tangent(xyz,w)
    V v0{}; v0.pos[0]=-0.5f; v0.pos[1]=-0.5f; v0.pos[2]=0.0f;
            v0.normal[0]=0.0f; v0.normal[1]=0.0f; v0.normal[2]=1.0f;
            v0.uv[0]=0.0f; v0.uv[1]=1.0f;
            v0.tangent[0]=1.0f; v0.tangent[1]=0.0f; v0.tangent[2]=0.0f; v0.tangent[3]=1.0f;

    V v1{}; v1.pos[0]= 0.5f; v1.pos[1]=-0.5f; v1.pos[2]=0.0f;
            v1.normal[0]=0.0f; v1.normal[1]=0.0f; v1.normal[2]=1.0f;
            v1.uv[0]=1.0f; v1.uv[1]=1.0f;
            v1.tangent[0]=1.0f; v1.tangent[1]=0.0f; v1.tangent[2]=0.0f; v1.tangent[3]=1.0f;

    V v2{}; v2.pos[0]= 0.5f; v2.pos[1]= 0.5f; v2.pos[2]=0.0f;
            v2.normal[0]=0.0f; v2.normal[1]=0.0f; v2.normal[2]=1.0f;
            v2.uv[0]=1.0f; v2.uv[1]=0.0f;
            v2.tangent[0]=1.0f; v2.tangent[1]=0.0f; v2.tangent[2]=0.0f; v2.tangent[3]=1.0f;

    V v3{}; v3.pos[0]=-0.5f; v3.pos[1]= 0.5f; v3.pos[2]=0.0f;
            v3.normal[0]=0.0f; v3.normal[1]=0.0f; v3.normal[2]=1.0f;
            v3.uv[0]=0.0f; v3.uv[1]=0.0f;
            v3.tangent[0]=1.0f; v3.tangent[1]=0.0f; v3.tangent[2]=0.0f; v3.tangent[3]=1.0f;

    MeshData mesh;
    mesh.vertices = { v0, v1, v2, v3 };
    // Winding (0,2,1),(0,3,2) — matches box-mesh vertical face convention.
    mesh.indices  = { 0u, 2u, 1u,
                      0u, 3u, 2u };
    return mesh;
}

/// Build a spherical-billboard model matrix that orients the unit quad to
/// face the camera at every angle.
///
/// Camera right and up axes are extracted from the view matrix column vectors
/// (view[col][row] in column-major GLM storage):
///   right = { view[0][0], view[1][0], view[2][0] }
///   up    = { view[0][1], view[1][1], view[2][1] }
///
/// The resulting matrix maps:
///   local X → world camera-right  (scaled by size.x)
///   local Y → world camera-up     (scaled by size.y)
///   local Z → cross(right, up)    (points away from camera into the scene)
///   origin  → world position
///
/// @param position  World-space centre of the sprite.
/// @param size      Width (x) and height (y) in world units.
/// @param view      Current camera view matrix.
/// @return          Model matrix ready for MeshDraw::modelMatrix.
[[nodiscard]] inline glm::mat4 makeBillboardMatrix(const glm::vec3& position,
                                                   const glm::vec2& size,
                                                   const glm::mat4& view) noexcept
{
    // Camera axes from view matrix (row-major extraction from column-major glm).
    const glm::vec3 right = { view[0][0], view[1][0], view[2][0] };
    const glm::vec3 up    = { view[0][1], view[1][1], view[2][1] };
    const glm::vec3 fwd   = glm::cross(right, up);   // points away from camera (into scene)

    glm::mat4 m(1.0f);
    m[0] = glm::vec4(right * size.x, 0.0f);
    m[1] = glm::vec4(up    * size.y, 0.0f);
    m[2] = glm::vec4(fwd,            0.0f);
    m[3] = glm::vec4(position,       1.0f);
    return m;
}

/// Populate scene draw lists from all ECS entities that have both a
/// TransformComponent and a BillboardSpriteComponent.
///
/// Routing by alphaMode:
///   AlphaMode::Cutout  → scene.meshDraws       (G-buffer, hard alpha cutout)
///   AlphaMode::Blended → scene.transparentDraws (forward transparency pass)
///
/// Uses the shared unit-quad buffers for geometry; a per-entity spherical
/// billboard model matrix is computed via makeBillboardMatrix() each frame.
///
/// @param world        ECS world to query.
/// @param scene        SceneView to append draw entries to.
/// @param view         Current camera view matrix (used for billboard orientation).
/// @param unitQuadVBO  Shared vertex buffer from makeUnitQuadMesh(); non-owning.
/// @param unitQuadIBO  Shared index buffer from makeUnitQuadMesh(); non-owning.
inline void billboardRenderSystem(daedalus::World&  world,
                                  SceneView&         scene,
                                  const glm::mat4&   view,
                                  rhi::IBuffer*      unitQuadVBO,
                                  rhi::IBuffer*      unitQuadIBO)
{
    world.each<daedalus::TransformComponent, BillboardSpriteComponent>(
        [&](daedalus::EntityId,
            const daedalus::TransformComponent& transform,
            const BillboardSpriteComponent&     sprite)
        {
            const glm::mat4 model =
                makeBillboardMatrix(transform.position, sprite.size, view);

            MeshDraw draw;
            draw.vertexBuffer       = unitQuadVBO;
            draw.indexBuffer        = unitQuadIBO;
            draw.indexCount         = 6u;
            draw.modelMatrix        = model;
            draw.prevModel          = model;   // Phase 1D: no per-sprite prev tracking
            draw.material.albedo    = sprite.texture;
            draw.material.normalMap = nullptr; // flat normal (engine default)
            draw.material.emissive  = nullptr; // no emission
            draw.material.roughness = 1.0f;
            draw.material.metalness = 0.0f;

            if (sprite.alphaMode == AlphaMode::Blended)
            {
                // Copy tint for the forward transparent shader; route to transparent list.
                draw.material.tint = sprite.tint;
                scene.transparentDraws.push_back(draw);
            }
            else
            {
                // Cutout: tint is irrelevant — keep default (1,1,1,1); route to G-buffer.
                scene.meshDraws.push_back(draw);
            }
        });
}

} // namespace daedalus::render
