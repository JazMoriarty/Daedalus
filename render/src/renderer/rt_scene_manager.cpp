// rt_scene_manager.cpp
// Implementation of RTSceneManager — acceleration structure and material table
// management for the ray tracing render path.

#include "daedalus/render/rt_scene_manager.h"
#include "daedalus/render/vertex_types.h"
#include "daedalus/render/rhi/rhi_types.h"

#include <glm/glm.hpp>
#include <cstring>
#include <unordered_set>

namespace daedalus::render
{

// ─── update ───────────────────────────────────────────────────────────────────
// Called once per frame when RT mode is active.  Rebuilds BLAS cache (lazy),
// TLAS (every frame), material table, and texture table.

void RTSceneManager::update(
    rhi::IRenderDevice& device,
    std::span<const MeshDraw> opaqueDraws,
    std::span<const MeshDraw> transparentDraws)
{
    // Reset per-frame state.
    m_textureIndexMap.clear();
    m_textureList.clear();
    m_uniqueBuffers.clear();
    m_uniqueBLAS.clear();

    // Reserve index 0 for the engine's default white texture (bound by the
    // caller).  nullptr texture pointers in materials map to this index.
    m_textureList.push_back(nullptr);

    const u32 totalDraws = static_cast<u32>(opaqueDraws.size() + transparentDraws.size());
    if (totalDraws == 0)
    {
        m_tlas.reset();
        m_materialTableBuf.reset();
        m_instanceCount = 0;
        return;
    }

    // ─── Build instance + material lists ──────────────────────────────────────

    std::vector<rhi::AccelStructInstanceDesc> instanceDescs;
    std::vector<RTMaterialGPU>                materials;
    instanceDescs.reserve(totalDraws);
    materials.reserve(totalDraws);

    // Flat primitive data array — one entry per triangle, deduplicated per BLAS.
    std::vector<RTPrimitiveDataGPU> primData;
    m_primDataOffsetCache.clear();

    // Track unique VB/IB and BLAS for useResource (set-based dedup).
    std::unordered_set<rhi::IBuffer*> seenBuffers;
    std::unordered_set<rhi::IAccelerationStructure*> seenBLAS;

    auto processDraw = [&](const MeshDraw& draw, u32 instanceId)
    {
        rhi::IAccelerationStructure* blas = getOrCreateBLAS(device, draw);
        if (!blas) return;  // skip draws with missing geometry

        // Collect unique BLAS for useResource residency declarations.
        if (seenBLAS.insert(blas).second)
            m_uniqueBLAS.push_back(blas);

        // Collect unique VB/IB for useResource.
        if (draw.vertexBuffer && seenBuffers.insert(draw.vertexBuffer).second)
            m_uniqueBuffers.push_back(draw.vertexBuffer);
        if (draw.indexBuffer && seenBuffers.insert(draw.indexBuffer).second)
            m_uniqueBuffers.push_back(draw.indexBuffer);

        // Instance descriptor.
        rhi::AccelStructInstanceDesc inst{};
        inst.blas       = blas;
        inst.instanceId = instanceId;
        inst.mask       = 0xFF;

        // Copy column-major 4×4 transform (glm is column-major).
        static_assert(sizeof(glm::mat4) == 16 * sizeof(float));
        std::memcpy(inst.transform, &draw.modelMatrix, 16 * sizeof(float));

        instanceDescs.push_back(inst);

        // Primitive data: deduplicated by BLAS key (same VB+IB = same triangles).
        const u32 primOffset = buildPrimitiveData(draw, primData);

        // Material table entry.
        RTMaterialGPU mat{};
        mat.albedoTextureIndex   = registerTexture(draw.material.albedo);
        mat.normalTextureIndex   = registerTexture(draw.material.normalMap);
        mat.emissiveTextureIndex = registerTexture(draw.material.emissive);
        mat.roughness            = draw.material.roughness;
        mat.metalness            = draw.material.metalness;
        mat.primitiveDataOffset  = primOffset;
        mat.tint                 = draw.material.tint;
        mat.uvOffset             = draw.material.uvOffset;
        mat.uvScale              = draw.material.uvScale;
        mat.sectorAmbient        = glm::vec4(draw.material.sectorAmbient,
                                               draw.material.isOutdoor ? 1.0f : 0.0f);

        materials.push_back(mat);
    };

    u32 instanceId = 0;
    for (const auto& draw : opaqueDraws)
        processDraw(draw, instanceId++);
    for (const auto& draw : transparentDraws)
        processDraw(draw, instanceId++);

    m_instanceCount = static_cast<u32>(instanceDescs.size());
    if (m_instanceCount == 0)
    {
        m_tlas.reset();
        m_materialTableBuf.reset();
        return;
    }

    // ─── Rebuild TLAS ─────────────────────────────────────────────────────────

    if (m_tlas)
    {
        device.rebuildAccelStruct(
            *m_tlas,
            {},  // no geometry (TLAS, not BLAS)
            instanceDescs,
            rhi::AccelStructBuildMode::Build);
    }
    else
    {
        m_tlas = device.createInstanceAccelStruct(instanceDescs);
    }

    // ─── Upload material table ──────────────────────────────────────────────────

    const u32 matBufSize = m_instanceCount * static_cast<u32>(sizeof(RTMaterialGPU));

    if (!m_materialTableBuf || m_materialTableBuf->size() < matBufSize)
    {
        rhi::BufferDescriptor desc{};
        desc.size      = matBufSize;
        desc.usage     = rhi::BufferUsage::Storage;
        desc.initData  = materials.data();
        desc.debugName = "RT_MaterialTable";
        m_materialTableBuf = device.createBuffer(desc);
    }
    else
    {
        // Reuse existing buffer — just upload new data.
        void* dst = m_materialTableBuf->map();
        if (dst)
        {
            std::memcpy(dst, materials.data(), matBufSize);
            m_materialTableBuf->unmap();
        }
    }

    // ─── Upload primitive data buffer ────────────────────────────────────────────

    if (!primData.empty())
    {
        const u32 primBufSize =
            static_cast<u32>(primData.size() * sizeof(RTPrimitiveDataGPU));

        if (!m_primitiveDataBuf || m_primitiveDataBuf->size() < primBufSize)
        {
            rhi::BufferDescriptor desc{};
            desc.size      = primBufSize;
            desc.usage     = rhi::BufferUsage::Storage;
            desc.initData  = primData.data();
            desc.debugName = "RT_PrimitiveData";
            m_primitiveDataBuf = device.createBuffer(desc);
        }
        else
        {
            void* dst = m_primitiveDataBuf->map();
            if (dst)
            {
                std::memcpy(dst, primData.data(), primBufSize);
                m_primitiveDataBuf->unmap();
            }
        }
    }
}

// ─── getOrCreateBLAS ──────────────────────────────────────────────────────────
// Deduplicates BLAS by (vertexBuffer, indexBuffer) pointer pair.

rhi::IAccelerationStructure* RTSceneManager::getOrCreateBLAS(
    rhi::IRenderDevice& device,
    const MeshDraw& draw)
{
    if (!draw.vertexBuffer) return nullptr;

    // Key by the GPU-resource native handle (e.g. MTLBuffer*) rather than the
    // IBuffer C++ wrapper pointer.  The native handle is stable for the
    // lifetime of the underlying GPU resource — the BLAS holds a strong
    // backend reference, so the address is never reused for a different buffer
    // while the old BLAS survives.  This prevents stale-BLAS aliasing when
    // retessellate() frees and reallocates IBuffer wrappers at the same C++
    // heap addresses.
    BLASKey key{
        draw.vertexBuffer->nativeHandle(),
        draw.indexBuffer  ? draw.indexBuffer->nativeHandle() : nullptr
    };

    auto it = m_blasCache.find(key);
    if (it != m_blasCache.end())
        return it->second.get();

    // Build geometry descriptor.
    rhi::AccelStructGeometryDesc geo{};
    geo.vertexBuffer = draw.vertexBuffer;
    geo.vertexCount  = draw.indexBuffer ? draw.indexCount : 0;
    geo.vertexStride = static_cast<u32>(sizeof(StaticMeshVertex));
    geo.indexBuffer  = draw.indexBuffer;
    geo.indexCount   = draw.indexCount;

    auto blas = device.createPrimitiveAccelStruct(std::span{&geo, 1});
    if (!blas) return nullptr;

    auto* raw = blas.get();
    m_blasCache[key] = std::move(blas);
    return raw;
}

// ─── registerTexture ─────────────────────────────────────────────────────────────
// Maps a texture pointer to a unique index in the texture table.
// nullptr → 0 (reserved for default white).

u32 RTSceneManager::registerTexture(rhi::ITexture* tex)
{
    if (!tex) return 0;

    auto it = m_textureIndexMap.find(tex);
    if (it != m_textureIndexMap.end())
        return it->second;

    u32 idx = static_cast<u32>(m_textureList.size());
    m_textureList.push_back(tex);
    m_textureIndexMap[tex] = idx;
    return idx;
}

// ─── buildPrimitiveData ─────────────────────────────────────────────────────────
// Extracts per-triangle vertex attributes from a mesh's vertex/index buffers.
// Deduplicates by BLAS key — instances sharing the same VB+IB reuse the same
// primitive data range.  Returns the base offset into the output vector.

u32 RTSceneManager::buildPrimitiveData(
    const MeshDraw& draw,
    std::vector<RTPrimitiveDataGPU>& outPrimData)
{
    if (!draw.vertexBuffer || !draw.indexBuffer || draw.indexCount == 0)
        return 0;

    // Check dedup cache.
    // Same native-handle keying as getOrCreateBLAS — dedup by GPU resource,
    // not by C++ wrapper pointer.
    BLASKey key{
        draw.vertexBuffer->nativeHandle(),
        draw.indexBuffer  ? draw.indexBuffer->nativeHandle() : nullptr
    };

    auto it = m_primDataOffsetCache.find(key);
    if (it != m_primDataOffsetCache.end())
        return it->second;

    const u32 baseOffset = static_cast<u32>(outPrimData.size());
    m_primDataOffsetCache[key] = baseOffset;

    // Map vertex and index buffers (CPU-readable on Metal shared memory).
    const auto* vertices = static_cast<const StaticMeshVertex*>(draw.vertexBuffer->map());
    const auto* indices  = static_cast<const u32*>(draw.indexBuffer->map());

    if (!vertices || !indices)
    {
        draw.vertexBuffer->unmap();
        draw.indexBuffer->unmap();
        return baseOffset;
    }

    const u32 triangleCount = draw.indexCount / 3;
    outPrimData.reserve(outPrimData.size() + triangleCount);

    for (u32 tri = 0; tri < triangleCount; ++tri)
    {
        const u32 i0 = indices[tri * 3 + 0];
        const u32 i1 = indices[tri * 3 + 1];
        const u32 i2 = indices[tri * 3 + 2];

        const StaticMeshVertex& v0 = vertices[i0];
        const StaticMeshVertex& v1 = vertices[i1];
        const StaticMeshVertex& v2 = vertices[i2];

        RTPrimitiveDataGPU pd{};
        pd.uv0[0] = v0.uv[0];  pd.uv0[1] = v0.uv[1];
        pd.uv1[0] = v1.uv[0];  pd.uv1[1] = v1.uv[1];
        pd.uv2[0] = v2.uv[0];  pd.uv2[1] = v2.uv[1];

        pd.normal0[0] = v0.normal[0];  pd.normal0[1] = v0.normal[1];  pd.normal0[2] = v0.normal[2];
        pd.normal1[0] = v1.normal[0];  pd.normal1[1] = v1.normal[1];  pd.normal1[2] = v1.normal[2];
        pd.normal2[0] = v2.normal[0];  pd.normal2[1] = v2.normal[1];  pd.normal2[2] = v2.normal[2];

        pd.tangent0[0] = v0.tangent[0];  pd.tangent0[1] = v0.tangent[1];
        pd.tangent0[2] = v0.tangent[2];  pd.tangent0[3] = v0.tangent[3];
        pd.tangent1[0] = v1.tangent[0];  pd.tangent1[1] = v1.tangent[1];
        pd.tangent1[2] = v1.tangent[2];  pd.tangent1[3] = v1.tangent[3];
        pd.tangent2[0] = v2.tangent[0];  pd.tangent2[1] = v2.tangent[1];
        pd.tangent2[2] = v2.tangent[2];  pd.tangent2[3] = v2.tangent[3];

        outPrimData.push_back(pd);
    }

    // Unmap is a no-op on Metal (persistent mapping), but call for RHI contract.
    draw.vertexBuffer->unmap();
    draw.indexBuffer->unmap();

    return baseOffset;
}

} // namespace daedalus::render
