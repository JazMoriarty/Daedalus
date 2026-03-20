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
};

// ─── Sector ───────────────────────────────────────────────────────────────────
// A convex or concave horizontal region of the world.
//
// walls forms a closed polygon (CCW winding from above). All walls share this
// sector's floor and ceiling heights unless a per-vertex slope is applied
// (slope support is reserved for a future phase; not encoded here).

struct Sector
{
    std::vector<Wall> walls;  ///< Ordered CCW; walls.size() >= 3.

    f32 floorHeight = 0.0f;  ///< World Y of the floor surface.
    f32 ceilHeight  = 4.0f;  ///< World Y of the ceiling surface.

    // ─── Material references ──────────────────────────────────────────────────
    UUID floorMaterialId;    ///< Floor surface material.
    UUID ceilMaterialId;     ///< Ceiling surface material.

    // ─── Lighting ─────────────────────────────────────────────────────────────
    glm::vec3 ambientColor     = {0.05f, 0.05f, 0.08f};
    f32       ambientIntensity = 1.0f;

    SectorFlags flags = SectorFlags::None;

    // ─── Phase 1F: floor shape and stair geometry ─────────────────────────────
    // Controls how the tessellator generates this sector's floor mesh.
    // Defaults to FloorShape::Flat which reproduces the baseline flat floor.
    FloorShape              floorShape   = FloorShape::Flat;
    std::optional<StairProfile> stairProfile;  ///< Used when floorShape == VisualStairs.
};

// ─── WorldMapData
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
