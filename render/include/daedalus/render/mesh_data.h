// mesh_data.h
// CPU-side mesh data shared by the asset pipeline and voxel mesher.
//
// Extracted from i_asset_loader.h so that vox_types.h can depend on MeshData
// without creating a circular include with i_asset_loader.h.

#pragma once

#include "daedalus/render/vertex_types.h"
#include "daedalus/core/types.h"

#include <string>
#include <vector>

namespace daedalus::render
{

// ─── MeshData ─────────────────────────────────────────────────────────────────
// CPU-side mesh data; the caller is responsible for uploading to GPU buffers.

struct MeshData
{
    std::vector<StaticMeshVertex> vertices;
    std::vector<u32>              indices;
    /// Relative URI of the first primitive's baseColor texture, as stored in
    /// the glTF file.  Empty when no material or texture is present.
    std::string                   albedoPath;
};

} // namespace daedalus::render
