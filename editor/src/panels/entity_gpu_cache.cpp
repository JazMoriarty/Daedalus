#include "entity_gpu_cache.h"

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
        auto result = loader.loadTexture(device, def.assetPath, /*sRGB=*/true);
        if (!result)
        {
            doc.log("EntityGpuCache: failed to load billboard texture: " + def.assetPath);
            break;
        }
        entry.albedoTex = std::move(*result);
        entry.loaded    = true;
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
        auto albedo = loader.loadTexture(device, def.assetPath, /*sRGB=*/true);
        if (!albedo)
        {
            doc.log("EntityGpuCache: failed to load decal albedo: " + def.assetPath);
            break;
        }
        entry.albedoTex = std::move(*albedo);

        if (!def.decalMat.normalPath.empty())
        {
            auto normal = loader.loadTexture(device, def.decalMat.normalPath, /*sRGB=*/false);
            if (normal)
                entry.normalTex = std::move(*normal);
        }
        entry.loaded = true;
        break;
    }

    case EntityVisualType::ParticleEmitter:
    {
        if (def.assetPath.empty()) break;
        auto atlas = loader.loadTexture(device, def.assetPath, /*sRGB=*/true);
        if (!atlas)
        {
            doc.log("EntityGpuCache: failed to load particle atlas: " + def.assetPath);
            break;
        }
        entry.albedoTex   = std::move(*atlas);
        entry.particlePool = render::createParticlePool(device, k_maxParticles);
        entry.loaded       = true;
        break;
    }
    }
}

// ─── rebuild ──────────────────────────────────────────────────────────────────

void EntityGpuCache::rebuild(render::IAssetLoader&         loader,
                               rhi::IRenderDevice&           device,
                               const std::vector<EntityDef>& entities,
                               EditMapDocument&              doc)
{
    if (!m_dirty) return;

    ensureQuadBuffers(device);

    m_entries.resize(entities.size());
    m_animTimes.resize(entities.size(), 0.0f);

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

        // Reset GPU resources, reload, and reset animation timer.
        entry         = {};
        m_animTimes[i] = 0.0f;
        loadEntry(entry, def, loader, device, doc);
    }

    m_dirty = false;
}

// ─── populateSceneView ────────────────────────────────────────────────────────

void EntityGpuCache::populateSceneView(render::SceneView&            scene,
                                        const std::vector<EntityDef>& entities,
                                        const glm::mat4&              view)
{
    const std::size_t n = std::min(m_entries.size(), entities.size());

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
            draw.material.albedo   = entry.albedoTex.get();
            draw.material.emissive = entry.albedoTex.get();  // self-illuminated: full colour regardless of NdotL
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
            draw.material.albedo    = entry.albedoTex.get();
            draw.material.emissive  = nullptr;
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
            model = glm::rotate(model, def.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, def.scale);

            render::MeshDraw draw;
            draw.vertexBuffer       = entry.vbo.get();
            draw.indexBuffer        = entry.ibo.get();
            draw.indexCount         = entry.indexCount;
            draw.modelMatrix        = model;
            draw.prevModel          = model;
            draw.material.albedo    = entry.albedoTex.get();  // palette tex (vox) or nullptr (static mesh)
            draw.material.roughness = 0.8f;
            draw.material.metalness = 0.0f;
            draw.material.tint      = def.tint;

            scene.meshDraws.push_back(draw);
            break;
        }

        case EntityVisualType::Decal:
        {
            if (!entry.albedoTex) break;

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, def.position);
            model = glm::rotate(model, def.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, def.scale);

            render::DecalDraw decal;
            decal.modelMatrix    = model;
            decal.invModelMatrix = glm::inverse(model);
            decal.albedoTexture  = entry.albedoTex.get();
            decal.normalTexture  = entry.normalTex.get();   // may be nullptr
            decal.roughness      = def.decalMat.roughness;
            decal.metalness      = def.decalMat.metalness;
            decal.opacity        = def.decalMat.opacity;

            scene.decalDraws.push_back(decal);
            break;
        }

        case EntityVisualType::ParticleEmitter:
        {
            if (!entry.particlePool || !entry.albedoTex) break;

            render::ParticleEmitterDraw emitter;
            emitter.pool         = entry.particlePool.get();
            emitter.atlasTexture = entry.albedoTex.get();

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
            c.spawnThisFrame = static_cast<unsigned>(p.emissionRate * scene.deltaTime);
            c.frameIndex     = scene.frameIndex;
            c.aliveListFlip  = entry.particlePool->aliveListFlip;
            c.atlasSize      = { static_cast<float>(def.anim.cols),
                                  static_cast<float>(def.anim.rows) };
            c.atlasFrameRate  = def.anim.frameRate;
            c.turbulenceScale = 0.0f;
            c.emissiveScale   = 1.0f;
            c.velocityStretch = 0.0f;
            c.softRange       = 0.5f;

            scene.particleEmitters.push_back(std::move(emitter));
            break;
        }
        }
    }
}

} // namespace daedalus::editor
