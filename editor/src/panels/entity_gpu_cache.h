// entity_gpu_cache.h
// Per-entity GPU resource cache for the 3D viewport.
//
// Owns textures, vertex/index buffers, and particle pools for each EntityDef
// in the document.  Rebuilt on demand whenever the entity dirty flag is set.
// A per-entry comparison (visualType + assetPath) avoids redundant re-uploads
// for unchanged entries when rebuild() is called.
//
// Usage (per frame):
//   if (doc.isEntityDirty()) { cache.invalidate(); doc.clearEntityDirty(); }
//   cache.rebuild(loader, device, doc.entities(), doc);
//   // ... push sector draws into scene ...
//   cache.populateSceneView(scene, doc.entities(), view);
//   render::sortTransparentDraws(scene);
//   renderer.renderFrame(...);

#pragma once

#include "daedalus/editor/entity_def.h"
#include "daedalus/render/i_asset_loader.h"
#include "daedalus/render/scene_view.h"
#include "daedalus/render/particle_pool.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/render/rhi/i_texture.h"

#include "sprite_anim_math.h"

// Forward declarations to avoid circular header dependencies
namespace daedalus::editor { class MaterialCatalog; }

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace daedalus::editor
{

class EditMapDocument;

// ─── EntityGpuCache ──────────────────────────────────────────────────────────
// Manages GPU-side resources for all entities in the scene.
// Created by Viewport3D and passed the asset loader + device each frame.

class EntityGpuCache
{
public:
    EntityGpuCache()  = default;
    ~EntityGpuCache() = default;

    EntityGpuCache(const EntityGpuCache&)            = delete;
    EntityGpuCache& operator=(const EntityGpuCache&) = delete;

    /// Mark the cache as stale — next rebuild() will re-upload changed entries.
    void invalidate() noexcept;

    /// Sync GPU resources with the current entity list.
    /// Skips entries whose visualType + assetPath are unchanged since last rebuild.
    void rebuild(render::IAssetLoader&         loader,
                 rhi::IRenderDevice&           device,
                 MaterialCatalog&              catalog,
                 const std::vector<EntityDef>& entities,
                 EditMapDocument&              doc);

    /// Append draw calls for all loaded entities into the scene.
    /// Advances per-entity animation timers using scene.deltaTime.
    /// Call render::sortTransparentDraws(scene) after this and before renderFrame.
    void populateSceneView(render::SceneView&            scene,
                           const std::vector<EntityDef>& entities,
                           const glm::mat4&              view);

    /// Get the mesh AABB for an entity in local space (VoxelObject / StaticMesh).
    /// Returns true if the entity has mesh bounds; false otherwise.
    bool getMeshAABB(std::size_t entityIdx, glm::vec3& outMin, glm::vec3& outMax) const;

private:
    struct EntityGpuEntry
    {
        EntityVisualType visualType = {};
        std::string      assetPath;
        bool             loaded     = false;

        // Textures: owned (for voxels/particles/fallbacks) or catalog-referenced.
        // When loading from MaterialCatalog, use albedoPtr/normalPtr (catalog owns lifetime).
        // When creating textures directly (voxels, particles), use albedoTex/normalTex (we own lifetime).
        std::unique_ptr<rhi::ITexture> albedoTex;  ///< Owned texture (voxel palette, particle fallback).
        std::unique_ptr<rhi::ITexture> normalTex;  ///< Owned texture (explicit load for decals).
        rhi::ITexture* albedoPtr = nullptr;  ///< Catalog-referenced texture (not owned).
        rhi::ITexture* normalPtr = nullptr;  ///< Catalog-referenced normal map (not owned).

        // Mesh geometry: VoxelObject / StaticMesh.
        std::unique_ptr<rhi::IBuffer> vbo;
        std::unique_ptr<rhi::IBuffer> ibo;
        unsigned                      indexCount = 0;

        // Mesh axis-aligned bounding box in local space (VoxelObject / StaticMesh).
        glm::vec3 meshAABBMin = {};
        glm::vec3 meshAABBMax = {};

        // Particle simulation buffers: ParticleEmitter only.
        std::unique_ptr<render::ParticlePool> particlePool;
    };

    bool                        m_dirty = true;
    std::vector<EntityGpuEntry> m_entries;
    std::vector<float>          m_animTimes;         ///< Per-entity elapsed time for animation (seconds).
    std::vector<float>          m_spawnAccumulators; ///< Fractional particle spawn accumulator per entity.

    // Shared unit-quad geometry reused by all billboard types.
    std::unique_ptr<rhi::IBuffer> m_quadVBO;
    std::unique_ptr<rhi::IBuffer> m_quadIBO;

    void ensureQuadBuffers(rhi::IRenderDevice& device);

    void loadEntry(EntityGpuEntry&       entry,
                   const EntityDef&      def,
                   render::IAssetLoader& loader,
                   rhi::IRenderDevice&   device,
                   MaterialCatalog&      catalog,
                   EditMapDocument&      doc);
};

} // namespace daedalus::editor
