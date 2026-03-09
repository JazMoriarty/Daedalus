// sector_tessellator.h
// Converts WorldMapData into renderable CPU-side mesh geometry.
//
// Each sector produces one MeshData entry containing the interleaved
// StaticMeshVertex geometry for that sector's floor, ceiling, and walls.
// The caller is responsible for uploading the MeshData to GPU buffers.
//
// Tessellation rules:
//   • Floor and ceiling: triangle fan from vertex 0 — O(N) triangles for an
//     N-wall convex polygon. Concave polygons are not yet supported; the
//     tessellator will produce visually incorrect but non-crashing output
//     for concave sectors (documented limitation, fixed in a future phase
//     with ear-clipping tessellation).
//   • Solid walls: one quad (two triangles) per wall, floor-to-ceiling.
//   • Portal walls: upper strip if this_ceil > adj_ceil; lower strip if
//     this_floor < adj_floor; no middle quad (the opening is empty).

#pragma once

#include "daedalus/world/map_data.h"
#include "daedalus/render/i_asset_loader.h"  // for render::MeshData
#include "daedalus/core/types.h"               // for UUID

#include <vector>

namespace daedalus::world
{

/// Tessellate all sectors in a WorldMapData into renderable mesh data.
///
/// @param map  The world map to tessellate.
///
/// @return  A vector of MeshData, one entry per sector, indexed by SectorId.
///          The vector size equals map.sectors.size().
[[nodiscard]] std::vector<render::MeshData> tessellateMap(const WorldMapData& map);

// ─── TaggedMeshBatch ──────────────────────────────────────────────────────────
// One CPU-side mesh batch for a single (sector, materialId) pair.
// Produced by tessellateMapTagged() so the renderer can issue one draw call per
// material per sector and bind the correct texture.

struct TaggedMeshBatch
{
    render::MeshData mesh;        ///< Interleaved vertex + index data ready for GPU upload.
    daedalus::UUID   materialId;  ///< Null UUID → engine-default / no texture.
};

/// Tessellate all sectors into per-material batches.
///
/// Surfaces sharing the same materialId within a sector are merged into one
/// TaggedMeshBatch so the caller can issue one draw call per material.
///
/// @param map  The world map to tessellate.
///
/// @return  Outer vector indexed by SectorId (size == map.sectors.size()).
///          Inner vector has one TaggedMeshBatch per unique materialId in that sector.
[[nodiscard]] std::vector<std::vector<TaggedMeshBatch>>
tessellateMapTagged(const WorldMapData& map);

} // namespace daedalus::world
