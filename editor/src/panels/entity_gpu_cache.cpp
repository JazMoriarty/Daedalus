#include "entity_gpu_cache.h"

#include "../catalog/material_catalog.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/render/systems/billboard_render_system.h"
#include "daedalus/render/rhi/rhi_types.h"
#include "daedalus/render/vertex_types.h"

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <filesystem>

namespace daedalus::editor
{

static constexpr unsigned k_maxParticles = 1024u;

// ─── invalidate ──────────────────────────────────────────────────────────────

void EntityGpuCache::invalidate() noexcept
{
    m_dirty = true;
}

// ─── ensureQuadBuffers ────────────────────────────────────────────────────────

void EntityGpuCache::ensureQuadBuffers(rhi::IRenderDevice& device)
{
    if (m_quadVBO && m_quadIBO) return;

    const render::MeshData quad = render::makeUnitQuadMesh();

    {
        rhi::BufferDescriptor d;
        d.size      = quad.vertices.size() * sizeof(render::StaticMeshVertex);
        d.usage     = rhi::BufferUsage::Vertex;
        d.initData  = quad.vertices.data();
        d.debugName = "EntityQuadVBO";
        m_quadVBO   = device.createBuffer(d);
    }
    {
        rhi::BufferDescriptor d;
        d.size      = quad.indices.size() * sizeof(unsigned);
        d.usage     = rhi::BufferUsage::Index;
        d.initData  = quad.indices.data();
        d.debugName = "EntityQuadIBO";
        m_quadIBO   = device.createBuffer(d);
    }
}

// ─── loadEntry ────────────────────────────────────────────────────────────────

void EntityGpuCache::loadEntry(EntityGpuEntry&       entry,
                                const EntityDef&      def,
                                render::IAssetLoader& loader,
                                rhi::IRenderDevice&   device,
                                MaterialCatalog&      catalog,
                                EditMapDocument&      doc)
{
    entry.visualType = def.visualType;
    entry.assetPath  = def.assetPath;
    entry.loaded     = false;

    switch (def.visualType)
    {
    case EntityVisualType::BillboardCutout:
    case EntityVisualType::BillboardBlended:
    case EntityVisualType::AnimatedBillboard:
    case EntityVisualType::RotatedSpriteSet:
    {
        if (def.assetPath.empty()) break;
        
        // Look up material in catalog to get UUID and auto-detect companions.
        const MaterialEntry* matEntry = catalog.findByPath(def.assetPath);
        if (!matEntry)
        {
            // Fallback: load directly if not in catalog (e.g., external path).
            auto result = loader.loadTexture(device, def.assetPath, /*sRGB=*/true);
            if (!result)
            {
                doc.log("EntityGpuCache: failed to load billboard texture: " + def.assetPath);
                break;
            }
            entry.albedoTex = std::move(*result);
        }
        else
        {
            // Load via catalog to get auto-detected companion maps.
            entry.albedoPtr = catalog.getOrLoadTexture(matEntry->uuid, device, loader);
            entry.normalPtr = catalog.getOrLoadNormalMap(matEntry->uuid, device, loader);
        }
        
        entry.loaded = true;
        break;
    }

    case EntityVisualType::VoxelObject:
    {
        if (def.assetPath.empty()) break;
        auto result = loader.loadVox(def.assetPath);
        if (!result)
        {
            doc.log("EntityGpuCache: failed to load .vox file: " + def.assetPath);
            break;
        }
        const render::MeshData& mesh = result->mesh;
        if (mesh.vertices.empty()) break;

        {
            rhi::BufferDescriptor d;
            d.size      = mesh.vertices.size() * sizeof(render::StaticMeshVertex);
            d.usage     = rhi::BufferUsage::Vertex;
            d.initData  = mesh.vertices.data();
            d.debugName = "VoxEntityVBO";
            entry.vbo   = device.createBuffer(d);
        }
        {
            rhi::BufferDescriptor d;
            d.size      = mesh.indices.size() * sizeof(unsigned);
            d.usage     = rhi::BufferUsage::Index;
            d.initData  = mesh.indices.data();
            d.debugName = "VoxEntityIBO";
            entry.ibo   = device.createBuffer(d);
        }
        entry.indexCount = static_cast<unsigned>(mesh.indices.size());

        // Calculate mesh AABB in local space.
        entry.meshAABBMin = glm::vec3(FLT_MAX);
        entry.meshAABBMax = glm::vec3(-FLT_MAX);
        for (const auto& v : mesh.vertices)
        {
            const glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
            entry.meshAABBMin = glm::min(entry.meshAABBMin, p);
            entry.meshAABBMax = glm::max(entry.meshAABBMax, p);
        }

        // Upload 256×1 RGBA8Unorm palette texture.
        {
            rhi::TextureDescriptor td;
            td.width     = 256u;
            td.height    = 1u;
            td.format    = rhi::TextureFormat::RGBA8Unorm;
            td.usage     = rhi::TextureUsage::ShaderRead;
            td.initData  = result->paletteRGBA.data();
            td.debugName = "VoxPaletteTex";
            entry.albedoTex = device.createTexture(td);
        }
        entry.loaded = true;
        break;
    }

    case EntityVisualType::StaticMesh:
    {
        if (def.assetPath.empty()) break;
        auto result = loader.loadMesh(def.assetPath);
        if (!result)
        {
            doc.log("EntityGpuCache: failed to load mesh: " + def.assetPath);
            break;
        }
        const render::MeshData& mesh = *result;
        if (mesh.vertices.empty()) break;

        {
            rhi::BufferDescriptor d;
            d.size      = mesh.vertices.size() * sizeof(render::StaticMeshVertex);
            d.usage     = rhi::BufferUsage::Vertex;
            d.initData  = mesh.vertices.data();
            d.debugName = "StaticMeshVBO";
            entry.vbo   = device.createBuffer(d);
        }
        {
            rhi::BufferDescriptor d;
            d.size      = mesh.indices.size() * sizeof(unsigned);
            d.usage     = rhi::BufferUsage::Index;
            d.initData  = mesh.indices.data();
            d.debugName = "StaticMeshIBO";
            entry.ibo   = device.createBuffer(d);
        }
        entry.indexCount = static_cast<unsigned>(mesh.indices.size());

        // Calculate mesh AABB in local space.
        entry.meshAABBMin = glm::vec3(FLT_MAX);
        entry.meshAABBMax = glm::vec3(-FLT_MAX);
        for (const auto& v : mesh.vertices)
        {
            const glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
            entry.meshAABBMin = glm::min(entry.meshAABBMin, p);
            entry.meshAABBMax = glm::max(entry.meshAABBMax, p);
        }

        // Load albedo texture if the glTF material provided a path.
        if (!mesh.albedoPath.empty())
        {
            // albedoPath is relative to the glTF file's directory.
            const std::filesystem::path dir  =
                std::filesystem::path(def.assetPath).parent_path();
            const std::filesystem::path texPath = dir / mesh.albedoPath;

            auto albedo = loader.loadTexture(device, texPath, /*sRGB=*/true);
            if (albedo)
                entry.albedoTex = std::move(*albedo);
            else
                doc.log("EntityGpuCache: could not load albedo '" +
                        texPath.string() + "' for mesh '" + def.assetPath + "'");
        }

        entry.loaded = true;
        break;
    }

    case EntityVisualType::Decal:
    {
        if (def.assetPath.empty()) break;
        
        // Look up material in catalog for auto-detected companions.
        const MaterialEntry* matEntry = catalog.findByPath(def.assetPath);
        if (!matEntry)
        {
            // Fallback: load directly if not in catalog.
            auto albedo = loader.loadTexture(device, def.assetPath, /*sRGB=*/true);
            if (!albedo)
            {
                doc.log("EntityGpuCache: failed to load decal albedo: " + def.assetPath);
                break;
            }
            entry.albedoTex = std::move(*albedo);
        }
        else
        {
            entry.albedoPtr = catalog.getOrLoadTexture(matEntry->uuid, device, loader);
            // For decals: use explicit normalPath if specified, otherwise use auto-detected.
            if (!def.decalMat.normalPath.empty())
            {
                auto normal = loader.loadTexture(device, def.decalMat.normalPath, /*sRGB=*/false);
                if (normal)
                    entry.normalTex = std::move(*normal);
            }
            else
            {
                entry.normalPtr = catalog.getOrLoadNormalMap(matEntry->uuid, device, loader);
            }
        }
        
        entry.loaded = true;
        break;
    }

    case EntityVisualType::ParticleEmitter:
    {
        // Try to load the user-specified atlas texture via catalog.
        if (!def.assetPath.empty())
        {
            const MaterialEntry* matEntry = catalog.findByPath(def.assetPath);
            if (matEntry)
            {
                entry.albedoPtr = catalog.getOrLoadTexture(matEntry->uuid, device, loader);
            }
            else
            {
                // Fallback: load directly if not in catalog.
                auto atlas = loader.loadTexture(device, def.assetPath, /*sRGB=*/true);
                if (atlas)
                    entry.albedoTex = std::move(*atlas);
                else
                    doc.log("EntityGpuCache: failed to load particle atlas: " + def.assetPath);
            }
        }

        // Fall back to a 1x1 white texture so the emitter renders without an atlas.
        if (!entry.albedoTex)
        {
            rhi::TextureDescriptor td;
            td.width     = 1u;
            td.height    = 1u;
            td.format    = rhi::TextureFormat::RGBA8Unorm;
            td.usage     = rhi::TextureUsage::ShaderRead;
            const uint8_t white[4] = {255, 255, 255, 255};
            td.initData  = white;
            td.debugName = "ParticleDefaultAtlas";
            entry.albedoTex = device.createTexture(td);
        }

        entry.particlePool = render::createParticlePool(device, k_maxParticles);
        entry.loaded       = true;
        break;
    }
    }
}

// ─── rebuild ──────────────────────────────────────────────────────────────────

void EntityGpuCache::rebuild(render::IAssetLoader&         loader,
                               rhi::IRenderDevice&           device,
                               MaterialCatalog&              catalog,
                               const std::vector<EntityDef>& entities,
                               EditMapDocument&              doc)
{
    if (!m_dirty) return;

    ensureQuadBuffers(device);

    m_entries.resize(entities.size());
    m_animTimes.resize(entities.size(), 0.0f);
    m_spawnAccumulators.resize(entities.size(), 0.0f);

    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        const EntityDef& def   = entities[i];
        EntityGpuEntry&  entry = m_entries[i];

        // Skip if this entry is already valid for the same asset.
        if (entry.loaded &&
            entry.visualType == def.visualType &&
            entry.assetPath  == def.assetPath)
        {
            continue;
        }

        // Reset GPU resources, reload, and reset per-entity timers.
        entry                   = {};
        m_animTimes[i]          = 0.0f;
        m_spawnAccumulators[i]  = 0.0f;
        loadEntry(entry, def, loader, device, catalog, doc);
    }

    m_dirty = false;
}

// ─── populateSceneView ────────────────────────────────────────────────────────

void EntityGpuCache::populateSceneView(render::SceneView&            scene,
                                        const std::vector<EntityDef>& entities,
                                        const glm::mat4&              view)
{
    const std::size_t n = std::min(m_entries.size(), entities.size());

    // Helper: get albedo texture (catalog-ref or owned).
    auto getAlbedo = [](const EntityGpuEntry& e) -> rhi::ITexture* {
        return e.albedoPtr ? e.albedoPtr : e.albedoTex.get();
    };
    // Helper: get normal map texture (catalog-ref or owned).
    auto getNormal = [](const EntityGpuEntry& e) -> rhi::ITexture* {
        return e.normalPtr ? e.normalPtr : e.normalTex.get();
    };

    for (std::size_t i = 0; i < n; ++i)
    {
        const EntityGpuEntry& entry = m_entries[i];
        const EntityDef&      def   = entities[i];

        if (!entry.loaded) continue;

        switch (def.visualType)
        {
        case EntityVisualType::RotatedSpriteSet:
        case EntityVisualType::BillboardCutout:
        case EntityVisualType::AnimatedBillboard:
        {
            const glm::vec2 size(def.scale.x, def.scale.y);
            const glm::mat4 model = render::makeBillboardMatrix(def.position, size, view);

            render::MeshDraw draw;
            draw.vertexBuffer      = m_quadVBO.get();
            draw.indexBuffer       = m_quadIBO.get();
            draw.indexCount        = 6u;
            draw.modelMatrix       = model;
            draw.prevModel         = model;
            draw.material.albedo    = getAlbedo(entry);
            draw.material.emissive  = getAlbedo(entry);  // self-illuminated: full colour regardless of NdotL
            draw.material.normalMap = getNormal(entry);
            draw.material.roughness = 1.0f;
            draw.material.metalness = 0.0f;
            draw.material.tint      = def.tint;

            // AnimatedBillboard: advance timer and compute UV crop for current frame.
            if (def.visualType == EntityVisualType::AnimatedBillboard &&
                def.anim.cols > 0u && def.anim.rows > 0u &&
                def.anim.frameCount > 0u)
            {
                m_animTimes[i] += scene.deltaTime;

                const uint32_t frame = spriteFrameIndex(
                    m_animTimes[i], def.anim.frameRate, def.anim.frameCount);

                const float fCols = static_cast<float>(def.anim.cols);
                const float fRows = static_cast<float>(def.anim.rows);
                const uint32_t col = frame % def.anim.cols;
                const uint32_t row = frame / def.anim.cols;

                draw.material.uvScale  = { 1.0f / fCols, 1.0f / fRows };
                draw.material.uvOffset = { static_cast<float>(col) / fCols,
                                           static_cast<float>(row) / fRows };
            }
            // RotatedSpriteSet: select column by horizontal viewing angle;
            // advance row by animation timer.
            else if (def.visualType == EntityVisualType::RotatedSpriteSet &&
                     def.rotatedSprite.directionCount > 0u)
            {
                const auto& rs = def.rotatedSprite;

                // Extract camera X/Z forward from view matrix (row 2 = cam -Z).
                // We only need the horizontal angle to the entity.
                const glm::vec3 toEntity = def.position - glm::vec3(
                    view[3][0] * (-1.0f), view[3][1] * (-1.0f), view[3][2] * (-1.0f));
                // Horizontal angle of entity relative to camera forward in XZ plane.
                const float camYaw = std::atan2(view[2][0], view[2][2]);
                const float entYaw = std::atan2(toEntity.x, toEntity.z);
                const float relAngle = entYaw - camYaw - def.yaw;

                const uint32_t dirCol  = rotatedSpriteFrameIndex(relAngle, rs.directionCount);

                // Advance animation row independently.
                m_animTimes[i] += scene.deltaTime;
                const uint32_t totalAnimFrames = (rs.animRows > 0u) ? rs.animRows : 1u;
                const uint32_t animRow = spriteFrameIndex(
                    m_animTimes[i], rs.frameRate, totalAnimFrames);

                const float fCols = static_cast<float>(rs.animCols > 0u ? rs.animCols : 1u);
                const float fRows = static_cast<float>(totalAnimFrames);

                draw.material.uvScale  = { 1.0f / fCols, 1.0f / fRows };
                draw.material.uvOffset = { static_cast<float>(dirCol)  / fCols,
                                           static_cast<float>(animRow) / fRows };
            }

            scene.meshDraws.push_back(draw);
            break;
        }

        case EntityVisualType::BillboardBlended:
        {
            const glm::vec2 size(def.scale.x, def.scale.y);
            const glm::mat4 model = render::makeBillboardMatrix(def.position, size, view);

            render::MeshDraw draw;
            draw.vertexBuffer       = m_quadVBO.get();
            draw.indexBuffer        = m_quadIBO.get();
            draw.indexCount         = 6u;
            draw.modelMatrix        = model;
            draw.prevModel          = model;
            draw.material.albedo    = getAlbedo(entry);
            draw.material.emissive  = nullptr;
            draw.material.normalMap = getNormal(entry);
            draw.material.roughness = 1.0f;
            draw.material.metalness = 0.0f;
            draw.material.tint      = def.tint;

            scene.transparentDraws.push_back(draw);
            break;
        }

        case EntityVisualType::VoxelObject:
        case EntityVisualType::StaticMesh:
        {
            if (!entry.vbo || !entry.ibo) break;

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, def.position);
            model = glm::rotate(model, def.yaw,   glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, def.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, def.roll,  glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::scale(model, def.scale);

            render::MeshDraw draw;
            draw.vertexBuffer       = entry.vbo.get();
            draw.indexBuffer        = entry.ibo.get();
            draw.indexCount         = entry.indexCount;
            draw.modelMatrix        = model;
            draw.prevModel          = model;
            draw.material.albedo    = getAlbedo(entry);  // palette tex (vox) or catalog-ref texture
            draw.material.normalMap = getNormal(entry);
            draw.material.roughness = 0.8f;
            draw.material.metalness = 0.0f;
            draw.material.tint      = def.tint;

            scene.meshDraws.push_back(draw);
            break;
        }

        case EntityVisualType::Decal:
        {
            rhi::ITexture* albedo = getAlbedo(entry);
            if (!albedo) break;

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, def.position);
            model = glm::rotate(model, def.yaw,   glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, def.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, def.roll,  glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::scale(model, def.scale);

            render::DecalDraw decal;
            decal.modelMatrix    = model;
            decal.invModelMatrix = glm::inverse(model);
            decal.albedoTexture  = albedo;
            decal.normalTexture  = getNormal(entry);  // may be nullptr
            decal.roughness      = def.decalMat.roughness;
            decal.metalness      = def.decalMat.metalness;
            decal.opacity        = def.decalMat.opacity;
            decal.zIndex         = def.decalMat.zIndex;

            scene.decalDraws.push_back(decal);
            break;
        }

        case EntityVisualType::ParticleEmitter:
        {
            rhi::ITexture* atlas = getAlbedo(entry);
            if (!entry.particlePool || !atlas) break;

            render::ParticleEmitterDraw emitter;
            emitter.pool         = entry.particlePool.get();
            emitter.atlasTexture = atlas;

            const auto& p = def.particle;
            auto&       c = emitter.constants;

            c.emitterPos     = def.position;
            c.emissionRate   = p.emissionRate;
            c.emitDir        = p.emitDir;
            c.coneHalfAngle  = p.coneHalfAngle;
            c.speedMin       = p.speedMin;
            c.speedMax       = p.speedMax;
            c.lifetimeMin    = p.lifetimeMin;
            c.lifetimeMax    = p.lifetimeMax;
            c.colorStart     = p.colorStart;
            c.colorEnd       = p.colorEnd;
            c.sizeStart      = p.sizeStart;
            c.sizeEnd        = p.sizeEnd;
            c.drag           = p.drag;
            c.gravity        = p.gravity;
            c.maxParticles   = k_maxParticles;
            // Accumulate fractional spawns so low emission rates still spawn
            // particles over time instead of always truncating to 0.
            m_spawnAccumulators[i] += p.emissionRate * scene.deltaTime;
            const unsigned toSpawn  = static_cast<unsigned>(m_spawnAccumulators[i]);
            m_spawnAccumulators[i] -= static_cast<float>(toSpawn);
            c.spawnThisFrame        = toSpawn;
            c.frameIndex     = scene.frameIndex;
            c.aliveListFlip  = entry.particlePool->aliveListFlip;
            c.atlasSize      = { static_cast<float>(def.anim.cols),
                                  static_cast<float>(def.anim.rows) };
            c.atlasFrameRate  = def.anim.frameRate;
            c.turbulenceScale = 0.0f;
            c.emissiveScale   = 1.0f;
            c.velocityStretch = 0.0f;
            c.softRange       = p.softRange;
            c.emissiveStart   = p.emissiveStart;
            c.emissiveEnd     = p.emissiveEnd;

            // If this emitter is flagged as a light emitter, inject a dynamic
            // point light at the emitter origin.  Intensity is derived from the
            // birth-emissive multiplier so the "lights off" slider kills the light too.
            if (p.emitsLight)
            {
                render::PointLight pl;
                pl.position  = def.position;
                pl.color     = glm::vec3(p.colorStart);
                pl.radius    = 5.0f;
                pl.intensity = p.emissiveStart * 10.0f;
                scene.pointLights.push_back(pl);
            }

            // Shadow volume (RT mode only): derive a world-space AABB large enough
            // to contain the oldest, fastest, largest possible particle.
            // extent = max travel distance + half the birth billboard radius.
            if (p.shadowDensity > 0.0f)
            {
                const float extent        = p.speedMax * p.lifetimeMax + p.sizeStart * 0.5f;
                emitter.hasShadowVolume  = true;
                emitter.shadowVolumeMin  = def.position - glm::vec3(extent);
                emitter.shadowVolumeMax  = def.position + glm::vec3(extent);
                emitter.shadowDensity    = p.shadowDensity;
                // Derive emissive emission from birth colour + emissive multiplier.
                // Used by the RT path tracer to illuminate surrounding geometry
                // (floor, ceiling, walls, objects) from the glowing particle cloud.
                emitter.emissiveColor     = glm::vec3(p.colorStart);
                emitter.emissiveIntensity = p.emissiveStart;
            }

            scene.particleEmitters.push_back(std::move(emitter));
            break;
        }
        }
    }
}

// ─── getMeshAABB ──────────────────────────────────────────────────────────────

bool EntityGpuCache::getMeshAABB(std::size_t entityIdx, glm::vec3& outMin, glm::vec3& outMax) const
{
    if (entityIdx >= m_entries.size())
        return false;

    const EntityGpuEntry& entry = m_entries[entityIdx];

    // Only VoxelObject and StaticMesh have mesh AABBs.
    if (!entry.loaded ||
        (entry.visualType != EntityVisualType::VoxelObject &&
         entry.visualType != EntityVisualType::StaticMesh))
    {
        return false;
    }

    outMin = entry.meshAABBMin;
    outMax = entry.meshAABBMax;
    return true;
}

} // namespace daedalus::editor
