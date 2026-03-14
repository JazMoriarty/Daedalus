// rt_scene_manager.h
// Manages acceleration structures and material table for ray tracing mode.
// Owned by FrameRenderer; allocated lazily on first RT frame.

#pragma once

#include "daedalus/core/types.h"
#include "daedalus/render/rhi/i_acceleration_structure.h"
#include "daedalus/render/rhi/i_buffer.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/scene_data.h"
#include "daedalus/render/scene_view.h"

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace daedalus::render
{

// ─── RTSceneManager ───────────────────────────────────────────────────────────
// Builds and caches bottom-level acceleration structures (BLAS) per unique
// vertex/index buffer pair, rebuilds a top-level acceleration structure (TLAS)
// every frame from the current draw lists, and maintains a flat GPU material
// table indexed by instance ID.

class RTSceneManager
{
public:
    RTSceneManager() = default;

    /// Rebuild BLAS cache, TLAS, and material table from the current draw lists.
    ///
    /// @param device          GPU device for creating acceleration structures and buffers.
    /// @param opaqueDraws     Opaque mesh draws (from SceneView::meshDraws).
    /// @param transparentDraws Transparent draws (from SceneView::transparentDraws).
    void update(rhi::IRenderDevice& device,
                std::span<const MeshDraw> opaqueDraws,
                std::span<const MeshDraw> transparentDraws);

    // ─── Accessors ────────────────────────────────────────────────────────────

    /// Top-level AS for binding to the path trace shader.  nullptr if no geometry.
    [[nodiscard]] rhi::IAccelerationStructure* tlas() const noexcept { return m_tlas.get(); }

    /// Flat GPU buffer of RTMaterialGPU entries, indexed by TLAS instance ID.
    [[nodiscard]] rhi::IBuffer* materialTable() const noexcept { return m_materialTableBuf.get(); }

    /// Number of instances in the TLAS (= number of material table entries).
    [[nodiscard]] u32 instanceCount() const noexcept { return m_instanceCount; }

    /// Flat list of unique textures referenced by the material table.
    /// RTMaterialGPU texture indices index into this array.
    [[nodiscard]] std::span<rhi::ITexture* const> textureTable() const noexcept
    {
        return m_textureList;
    }

    /// Flat GPU buffer of RTPrimitiveDataGPU entries — per-triangle vertex
    /// attributes for barycentric interpolation.  nullptr if no geometry.
    [[nodiscard]] rhi::IBuffer* primitiveDataBuffer() const noexcept
    {
        return m_primitiveDataBuf.get();
    }

    /// Unique vertex/index buffers referenced by the TLAS.  Must be made
    /// resident via useResource() on the compute encoder before dispatching
    /// any ray intersection queries.
    [[nodiscard]] std::span<rhi::IBuffer* const> uniqueBuffers() const noexcept
    {
        return m_uniqueBuffers;
    }

    /// Unique BLAS objects referenced by the TLAS.  Must be made resident
    /// via useResource() on the compute encoder before dispatching any ray
    /// intersection queries (Metal requires explicit residency for all
    /// primitive acceleration structures referenced through an instance AS).
    [[nodiscard]] std::span<rhi::IAccelerationStructure* const> uniqueBLAS() const noexcept
    {
        return m_uniqueBLAS;
    }

private:
    // ─── BLAS cache ───────────────────────────────────────────────────────────
    struct BLASKey
    {
        void* vb = nullptr;
        void* ib = nullptr;
        bool operator==(const BLASKey& o) const noexcept
        {
            return vb == o.vb && ib == o.ib;
        }
    };

    struct BLASKeyHash
    {
        std::size_t operator()(const BLASKey& k) const noexcept
        {
            auto h1 = std::hash<void*>{}(k.vb);
            auto h2 = std::hash<void*>{}(k.ib);
            return h1 ^ (h2 << 1);
        }
    };

    std::unordered_map<BLASKey, std::unique_ptr<rhi::IAccelerationStructure>, BLASKeyHash>
        m_blasCache;

    // ─── TLAS ─────────────────────────────────────────────────────────────────
    std::unique_ptr<rhi::IAccelerationStructure> m_tlas;

    // ─── Material table ───────────────────────────────────────────────────────
    std::unique_ptr<rhi::IBuffer> m_materialTableBuf;
    u32                           m_instanceCount = 0;

    // ─── Texture table ────────────────────────────────────────────────────────
    std::unordered_map<rhi::ITexture*, u32> m_textureIndexMap;
    std::vector<rhi::ITexture*>             m_textureList;

    // ─── Primitive data buffer ──────────────────────────────────────────────────
    // Per-triangle vertex attributes (UV, normal, tangent), deduplicated per BLAS.
    std::unique_ptr<rhi::IBuffer>             m_primitiveDataBuf;
    std::unordered_map<BLASKey, u32, BLASKeyHash> m_primDataOffsetCache;

    // ─── Unique buffers for useResource ───────────────────────────────────────────
    // VB/IB pointers referenced by the BLAS; rebuilt every frame.
    std::vector<rhi::IBuffer*> m_uniqueBuffers;

    // ─── Unique BLAS for useResource ─────────────────────────────────────────────
    // Deduplicated BLAS objects referenced by the TLAS; rebuilt every frame.
    std::vector<rhi::IAccelerationStructure*> m_uniqueBLAS;

    // ─── Helpers ──────────────────────────────────────────────────────────────

    /// Get or create a BLAS for the given vertex/index buffer pair.
    rhi::IAccelerationStructure* getOrCreateBLAS(
        rhi::IRenderDevice& device,
        const MeshDraw& draw);

    /// Register a texture and return its index in the texture table.
    /// nullptr textures get index 0 (reserved for engine default white).
    u32 registerTexture(rhi::ITexture* tex);

    /// Build per-triangle vertex attributes for a mesh and append to the
    /// output vector.  Returns the base offset (number of triangles already
    /// in the vector before appending).
    u32 buildPrimitiveData(
        const MeshDraw& draw,
        std::vector<RTPrimitiveDataGPU>& outPrimData);
};

} // namespace daedalus::render
