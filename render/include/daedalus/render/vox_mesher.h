// vox_mesher.h
// Pure greedy-mesh function for VoxData → MeshData.
//
// No GPU or platform dependencies — safe to call from any thread and fully
// unit-testable without instantiating a Metal device.
//
// Algorithm (repeated for all 6 face directions):
//   For each slice perpendicular to the current axis:
//     1. Build a 2D visibility mask.  mask[u][v] = colorIndex of the visible
//        face at that cell, or 0 when the adjacent voxel occludes it.
//     2. Scan in raster order; for each unvisited cell with color c:
//          a. Extend du along u while the same color and not yet consumed.
//          b. Extend dv along v while the entire du-strip remains color c.
//          c. Emit one quad (4 vertices, 6 indices) and mark all cells done.
//
// Winding convention: (0,2,1),(0,3,2) — identical to makeBoxMesh() vertical
// faces.  The geometric cross-product of the first triangle points outward,
// which is the front-facing direction after Metal's viewport Y-flip.
//
// Vertex UV: u = (colorIndex + 0.5) / 256,  v = 0.5.
// This samples the 256×1 RGBA8 palette texture returned by loadVox().

#pragma once

#include "daedalus/render/vox_types.h"
#include "daedalus/render/mesh_data.h"
#include "daedalus/render/vertex_types.h"
#include "daedalus/core/types.h"

#include <vector>

namespace daedalus::render
{

// ─── greedyMeshVoxels ─────────────────────────────────────────────────────────
// Build a face-culled, greedy-merged mesh from a VoxData volume.
//
// @param vox  Parsed MagicaVoxel data (coordinates use MagicaVoxel's convention:
//             X = right, Y = forward, Z = up, each voxel occupies a 1×1×1 unit
//             cube whose corner is at the integer coordinate).
// @return     CPU-side MeshData in StaticMeshVertex / u32 index format.

[[nodiscard]] inline MeshData greedyMeshVoxels(const VoxData& vox)
{
    MeshData result;
    if (vox.sizeX == 0 || vox.sizeY == 0 || vox.sizeZ == 0) { return result; }

    const u32 sX = vox.sizeX;
    const u32 sY = vox.sizeY;
    const u32 sZ = vox.sizeZ;

    // ─── Emit one quad into result ────────────────────────────────────────────
    // Vertices v0..v3 in order; indices (0,2,1),(0,3,2).
    // nx/ny/nz = face normal; tx/ty/tz = face tangent (handedness always +1).
    // All 4 vertices carry the same UV pointing into the 256×1 palette texture.
    auto emitQuad = [&](
        float nx, float ny, float nz,
        float tx, float ty, float tz,
        u8    colorIndex,
        float x0, float y0, float z0,
        float x1, float y1, float z1,
        float x2, float y2, float z2,
        float x3, float y3, float z3)
    {
        const float pu = (static_cast<float>(colorIndex) + 0.5f) / 256.0f;
        constexpr float pv = 0.5f;

        const u32 base = static_cast<u32>(result.vertices.size());

        auto mkv = [&](float px, float py, float pz) -> StaticMeshVertex
        {
            StaticMeshVertex sv{};
            sv.pos[0] = px;    sv.pos[1] = py;    sv.pos[2] = pz;
            sv.normal[0] = nx; sv.normal[1] = ny; sv.normal[2] = nz;
            sv.uv[0] = pu;     sv.uv[1] = pv;
            sv.tangent[0] = tx; sv.tangent[1] = ty; sv.tangent[2] = tz;
            sv.tangent[3] = 1.0f;
            return sv;
        };

        result.vertices.push_back(mkv(x0, y0, z0));
        result.vertices.push_back(mkv(x1, y1, z1));
        result.vertices.push_back(mkv(x2, y2, z2));
        result.vertices.push_back(mkv(x3, y3, z3));

        // (0,2,1),(0,3,2): matches makeBoxMesh() / makeUnitQuadMesh() convention.
        result.indices.push_back(base + 0); result.indices.push_back(base + 2); result.indices.push_back(base + 1);
        result.indices.push_back(base + 0); result.indices.push_back(base + 3); result.indices.push_back(base + 2);
    };

    // ─── Per-axis, per-sign greedy sweep ─────────────────────────────────────
    // axis: 0=X, 1=Y, 2=Z.   sign: +1 = positive face, −1 = negative face.
    //
    // In-plane axes for each sweep axis:
    //   axis 0 (X slices): u = Y (0..sizeY-1),  v = Z (0..sizeZ-1)
    //   axis 1 (Y slices): u = X (0..sizeX-1),  v = Z (0..sizeZ-1)
    //   axis 2 (Z slices): u = X (0..sizeX-1),  v = Y (0..sizeY-1)
    //
    // Vertex positions for each face direction (derived to satisfy the winding
    // rule: first-triangle cross-product points outward):
    //
    //   +X face at x=slice+1:  v0=(fp,fu0,fv0)  v1=(fp,fu0,fv1)  v2=(fp,fu1,fv1)  v3=(fp,fu1,fv0)
    //   -X face at x=slice:    v0=(fp,fu0,fv1)  v1=(fp,fu0,fv0)  v2=(fp,fu1,fv0)  v3=(fp,fu1,fv1)
    //   +Y face at y=slice+1:  v0=(fu0,fp,fv0)  v1=(fu1,fp,fv0)  v2=(fu1,fp,fv1)  v3=(fu0,fp,fv1)
    //   -Y face at y=slice:    v0=(fu0,fp,fv1)  v1=(fu1,fp,fv1)  v2=(fu1,fp,fv0)  v3=(fu0,fp,fv0)
    //   +Z face at z=slice+1:  v0=(fu1,fv0,fp)  v1=(fu0,fv0,fp)  v2=(fu0,fv1,fp)  v3=(fu1,fv1,fp)
    //   -Z face at z=slice:    v0=(fu0,fv0,fp)  v1=(fu1,fv0,fp)  v2=(fu1,fv1,fp)  v3=(fu0,fv1,fp)
    //
    // where fp = face plane coord, fu0/fu1 = u_min/u_max, fv0/fv1 = v_min/v_max.

    for (int axis = 0; axis < 3; ++axis)
    {
        const u32 sliceDim = (axis == 0) ? sX : (axis == 1) ? sY : sZ;
        const u32 dimU     = (axis == 0) ? sY : sX;
        const u32 dimV     = (axis == 0) ? sZ : (axis == 1) ? sZ : sY;

        for (int signIdx = 0; signIdx < 2; ++signIdx)
        {
            const int sign = (signIdx == 0) ? +1 : -1;

            // Normal and tangent for this face direction.
            float fnx = 0, fny = 0, fnz = 0;
            float ftx = 0, fty = 0, ftz = 0;
            if      (axis == 0 && sign > 0) { fnx =  1; fty = 1; }  // +X, tangent = +Y
            else if (axis == 0)             { fnx = -1; fty = 1; }  // -X, tangent = +Y
            else if (axis == 1 && sign > 0) { fny =  1; ftx = 1; }  // +Y, tangent = +X
            else if (axis == 1)             { fny = -1; ftx = 1; }  // -Y, tangent = +X
            else if (axis == 2 && sign > 0) { fnz =  1; ftx = 1; }  // +Z, tangent = +X
            else                            { fnz = -1; ftx = -1; } // -Z, tangent = -X

            for (u32 slice = 0; slice < sliceDim; ++slice)
            {
                // ── Build visibility mask for this slice ──────────────────────
                std::vector<u8> mask(dimU * dimV, 0u);

                for (u32 u = 0; u < dimU; ++u)
                {
                    for (u32 v = 0; v < dimV; ++v)
                    {
                        // Map (axis, slice, u, v) → (x, y, z).
                        u32 x, y, z;
                        if      (axis == 0) { x = slice; y = u; z = v; }
                        else if (axis == 1) { x = u; y = slice; z = v; }
                        else                { x = u; y = v;     z = slice; }

                        const u8 c = vox.at(x, y, z);
                        if (c == 0) { continue; }

                        // Neighbour in the face direction.
                        const int nx_ = static_cast<int>(x) + (axis == 0 ? sign : 0);
                        const int ny_ = static_cast<int>(y) + (axis == 1 ? sign : 0);
                        const int nz_ = static_cast<int>(z) + (axis == 2 ? sign : 0);

                        const bool oob =
                            nx_ < 0 || ny_ < 0 || nz_ < 0 ||
                            static_cast<u32>(nx_) >= sX ||
                            static_cast<u32>(ny_) >= sY ||
                            static_cast<u32>(nz_) >= sZ;

                        const u8 neighbor = oob ? 0u
                            : vox.at(static_cast<u32>(nx_),
                                     static_cast<u32>(ny_),
                                     static_cast<u32>(nz_));

                        if (neighbor == 0)
                            mask[u + dimU * v] = c;
                    }
                }

                // ── Greedy rectangle merge ────────────────────────────────────
                std::vector<bool> done(dimU * dimV, false);

                for (u32 v = 0; v < dimV; ++v)
                {
                    for (u32 u = 0; u < dimU; ++u)
                    {
                        if (done[u + dimU * v]) { continue; }
                        const u8 c = mask[u + dimU * v];
                        if (c == 0) { continue; }

                        // Extend along u.
                        u32 du = 1;
                        while (u + du < dimU &&
                               mask[(u + du) + dimU * v] == c &&
                               !done[(u + du) + dimU * v])
                            ++du;

                        // Extend along v.
                        u32 dv = 1;
                        while (v + dv < dimV)
                        {
                            bool ok = true;
                            for (u32 i = 0; i < du && ok; ++i)
                                if (mask[(u + i) + dimU * (v + dv)] != c ||
                                    done[(u + i) + dimU * (v + dv)])
                                    ok = false;
                            if (!ok) { break; }
                            ++dv;
                        }

                        // Mark consumed.
                        for (u32 vv = v; vv < v + dv; ++vv)
                            for (u32 uu = u; uu < u + du; ++uu)
                                done[uu + dimU * vv] = true;

                        // Face plane position and rectangle corners.
                        const float fp  = static_cast<float>(sign > 0 ? slice + 1 : slice);
                        const float fu0 = static_cast<float>(u);
                        const float fu1 = static_cast<float>(u + du);
                        const float fv0 = static_cast<float>(v);
                        const float fv1 = static_cast<float>(v + dv);

                        // Emit quad — vertex order matches per-face winding table in header.
                        if (axis == 0 && sign > 0)
                        {   // +X: fp=x, u=Y, v=Z
                            emitQuad(fnx,fny,fnz, ftx,fty,ftz, c,
                                fp,fu0,fv0,  fp,fu0,fv1,  fp,fu1,fv1,  fp,fu1,fv0);
                        }
                        else if (axis == 0)
                        {   // -X: fp=x, u=Y, v=Z
                            emitQuad(fnx,fny,fnz, ftx,fty,ftz, c,
                                fp,fu0,fv1,  fp,fu0,fv0,  fp,fu1,fv0,  fp,fu1,fv1);
                        }
                        else if (axis == 1 && sign > 0)
                        {   // +Y: fp=y, u=X, v=Z
                            emitQuad(fnx,fny,fnz, ftx,fty,ftz, c,
                                fu0,fp,fv0,  fu1,fp,fv0,  fu1,fp,fv1,  fu0,fp,fv1);
                        }
                        else if (axis == 1)
                        {   // -Y: fp=y, u=X, v=Z
                            emitQuad(fnx,fny,fnz, ftx,fty,ftz, c,
                                fu0,fp,fv1,  fu1,fp,fv1,  fu1,fp,fv0,  fu0,fp,fv0);
                        }
                        else if (axis == 2 && sign > 0)
                        {   // +Z: fp=z, u=X, v=Y
                            emitQuad(fnx,fny,fnz, ftx,fty,ftz, c,
                                fu1,fv0,fp,  fu0,fv0,fp,  fu0,fv1,fp,  fu1,fv1,fp);
                        }
                        else
                        {   // -Z: fp=z, u=X, v=Y
                            emitQuad(fnx,fny,fnz, ftx,fty,ftz, c,
                                fu0,fv0,fp,  fu1,fv0,fp,  fu1,fv1,fp,  fu0,fv1,fp);
                        }
                    }
                }
            }
        }
    }

    return result;
}

} // namespace daedalus::render
