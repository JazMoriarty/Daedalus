// map_data.h
// Plain data structs that define the in-memory world representation.
//
// These are pure data — no methods, no virtual functions. They are directly
// serialisable to the .dmap binary format and round-trip through .dmap.json.
//
// Coordinate conventions:
//   • Sectors live in the XZ horizontal plane; Y is the vertical axis (up).
//   • Wall vertices are 2D points (x, z) in map space, matching world X and Z.
//   • Floor and ceiling heights are world-space Y values.
//   • Walls are ordered counter-clockwise when viewed from above (Y+).
//   • Each wall's end point is the next wall's start point (last→first closes
//     the polygon).
//
// Material references:
//   • Materials are identified by UUID. The null UUID (hi=0, lo=0) means
//     "use the engine default for this surface".

#pragma once

#include "daedalus/world/world_types.h"
#include "daedalus/core/types.h"

#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

namespace daedalus::world
{

// ─── Wall ─────────────────────────────────────────────────────────────────────
// One edge of a sector polygon.
//
// p0 is the wall's start vertex in map-space (world X, world Z).
// The wall's end vertex is p0 of the next wall in the Sector::walls list
// (wrapping from last back to index 0).

struct Wall
{
    glm::vec2 p0 = {0.0f, 0.0f};  ///< Start vertex (map X, map Z = world X, Z).

    WallFlags flags = WallFlags::None;

    /// If != INVALID_SECTOR_ID, this wall is a portal into that sector.
    SectorId portalSectorId = INVALID_SECTOR_ID;

    // ─── Material references (UUID) ───────────────────────────────────────────────────
    // Null UUID (hi=lo=0) → engine default material for that surface type.
    UUID frontMaterialId;   ///< Full-height wall face (used when solid, or strips on portal).
    UUID upperMaterialId;   ///< Strip above a portal opening (this_ceil > portal_ceil).
    UUID lowerMaterialId;   ///< Strip below a portal opening (this_floor < portal_floor).
    UUID backMaterialId;    ///< Face seen looking back through a portal from the adjacent sector.

    // ─── UV mapping ────────────────────────────────────────────────────────────
    glm::vec2 uvOffset  = {0.0f, 0.0f};  ///< Texture coordinate offset.
    glm::vec2 uvScale   = {1.0f, 1.0f};  ///< Texture coordinate scale.
    f32       uvRotation = 0.0f;          ///< Rotation in radians (clockwise).

    // ─── Phase 1F: per-vertex height overrides ────────────────────────────────
    // Override the parent sector's scalar floor or ceiling height at THIS wall's
    // start vertex (p0).  The end vertex height comes from the NEXT wall's override.
    // std::nullopt means "use the sector's floorHeight / ceilHeight scalar".
    // These drive sloped floors, ramps, and uneven ceilings without changing the
    // sector's default height for other walls that have no override set.
    std::optional<f32> floorHeightOverride;  ///< Per-vertex floor Y override at p0.
    std::optional<f32> ceilHeightOverride;   ///< Per-vertex ceiling Y override at p0.

    // ─── Phase 1F-C: Bezier curve handles ─────────────────────────────────
    // When curveControlA is set, the wall is tessellated as a Bezier curve
    // rather than a straight segment.  curveSubdivisions controls the number of
    // straight-segment quads used to approximate the curve (default 12).
    //
    // One control point → quadratic Bezier (P0, Ca, P1).
    // Two control points → cubic Bezier (P0, Ca, Cb, P1).
    std::optional<glm::vec2> curveControlA;         ///< Bezier control point in XZ map space.
    std::optional<glm::vec2> curveControlB;         ///< Second control point (cubic only).
    u32                      curveSubdivisions = 12u; ///< Segment count, clamped to [4, 64].
};

// ─── DetailBrushGeomParams ────────────────────────────────────────────────────
// Flat parameter struct for all DetailBrush types.  Only fields relevant to
// the active DetailBrushType are meaningful; unused fields retain defaults.
// Must be declared before Sector so std::vector<DetailBrush> is well-formed.

struct DetailBrushGeomParams
{
    // ── Box / Wedge ───────────────────────────────────────────────────────
    glm::vec3   halfExtents = {0.5f, 0.5f, 0.5f};  ///< Half-extent on each axis.
    u32         slopeAxis   = 1u;  ///< Wedge slope axis: 1 = slope along Z (default).
                                   ///< Phase 1F-C: only slopeAxis=1 is implemented.
                                   ///< slopeAxis=0 (X slope) is planned for Phase 1F-D;
                                   ///< until then it silently produces slopeAxis=1 geometry.
                                   ///< Workaround: rotate the brush 90° around Y.

    // ── Cylinder ─────────────────────────────────────────────────────────
    f32         radius         = 0.5f;  ///< Cylinder radius.
    f32         height         = 1.0f;  ///< Cylinder height.
    u32         segmentCount   = 12u;   ///< Side face count, clamped to [4, 64].

    // ── ArchSpan ─────────────────────────────────────────────────────────
    f32         spanWidth    = 2.0f;  ///< Arch span along local X (world units).
    f32         archHeight   = 1.0f;  ///< Arch height above its base along local Y.
    f32         thickness    = 0.2f;  ///< Arch band depth along local Z.
    ArchProfile archProfile  = ArchProfile::Semicircular;
    u32         archSegments = 12u;   ///< Number of curve-approximation segments.

    // ── ImportedMesh ───────────────────────────────────────────────────────
    UUID        meshAssetId;  ///< Pre-compiled GLTF asset UUID.
};

// ─── DetailBrush ──────────────────────────────────────────────────────────────
// A Layer 2 static geometry element placed within a sector.
//
// Detail brushes are compiled into the sector's tagged GPU mesh batch at
// level-compile time.  They do not create portals or affect portal visibility.
// Physics shapes for brushes with collidable = true are registered as part of
// the sector's static compound body by the engine's physics world builder.
//
// ImportedMesh brushes are handled by the asset pipeline compile step;
// the tessellator skips them (no geometry is emitted by tessellateMapTagged).

struct DetailBrush
{
    glm::mat4             transform   = glm::mat4(1.0f);  ///< World-space transform.
    DetailBrushType       type        = DetailBrushType::Box;
    DetailBrushGeomParams geom;                            ///< Type-specific parameters.
    UUID                  materialId;                      ///< Material for rendering.
    bool                  collidable  = false;  ///< Whether a physics shape is registered.
    bool                  castsShadow = true;   ///< Whether the brush casts shadows.
};

// ─── Sector ─────────────────────────────────────────────────────────────────
// A convex or concave horizontal region of the world.
//
// walls forms a closed polygon (CCW winding from above). The sector's
// floorHeight and ceilHeight are the scalar defaults for all surfaces.
// Individual walls may override these with floorHeightOverride / ceilHeightOverride
// to produce sloped floors, ramps, and uneven ceilings.

struct Sector
{
    std::vector<Wall> walls;  ///< Ordered CCW; walls.size() >= 3.

    f32 floorHeight = 0.0f;  ///< World Y of the floor surface.
    f32 ceilHeight  = 4.0f;  ///< World Y of the ceiling surface.

    // ─── Material references ───────────────────────────────────────────────────
    UUID floorMaterialId;    ///< Floor surface material.
    UUID ceilMaterialId;     ///< Ceiling surface material.

    // ─── Lighting ─────────────────────────────────────────────────────────
    glm::vec3 ambientColor     = {0.05f, 0.05f, 0.08f};
    f32       ambientIntensity = 1.0f;

    SectorFlags flags = SectorFlags::None;

    // ─── Phase 1F: floor shape and stair geometry ─────────────────────────────
    // Controls how the tessellator generates this sector's floor mesh.
    // Defaults to FloorShape::Flat which reproduces the baseline flat floor.
    FloorShape              floorShape   = FloorShape::Flat;
    std::optional<StairProfile> stairProfile;  ///< Used when floorShape == VisualStairs.

    // ─── Phase 1F-B: floor and ceiling portals (Sector-Over-Sector) ──────────────
    // When valid, the floor or ceiling surface becomes a portal opening into the
    // specified sector.  INVALID_SECTOR_ID means no portal — the surface is solid.
    SectorId floorPortalSectorId = INVALID_SECTOR_ID;  ///< Target sector below floor.
    SectorId ceilPortalSectorId  = INVALID_SECTOR_ID;  ///< Target sector above ceiling.
    UUID     floorPortalMaterialId;  ///< Material for the floor portal surface.
    UUID     ceilPortalMaterialId;   ///< Material for the ceiling portal surface.

    // ─── Phase 1F-C: detail geometry (Layer 2) ──────────────────────────────
    // Static mesh shapes compiled into this sector's tagged GPU mesh batch at
    // level-compile time.  Portal visibility culls at sector granularity,
    // automatically excluding all detail brushes in culled sectors.
    std::vector<DetailBrush> details;
};

// ─── WorldMapData ─────────────────────────────────────────────────────────────
// The complete in-memory representation of a .dmap file.
//
// Sector IDs are implicit indices into the sectors vector (sector 0 is
// sectors[0], etc.). INVALID_SECTOR_ID is never a valid index.

struct WorldMapData
{
    std::string name;    ///< Human-readable map name.
    std::string author;  ///< Author name(s).

    std::vector<Sector> sectors;

    // ─── Global ambient override ──────────────────────────────────────────────
    // Used for sectors whose ambientColor is not explicitly set.
    glm::vec3 globalAmbientColor     = {0.05f, 0.05f, 0.08f};
    f32       globalAmbientIntensity = 1.0f;
};

} // namespace daedalus::world
