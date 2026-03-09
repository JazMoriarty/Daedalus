// prefab_def.h
// A named, reusable group of sectors and entities stored with positions
// relative to an anchor (pivot) point.  Placed via CmdPlacePrefab.

#pragma once

#include "daedalus/editor/entity_def.h"
#include "daedalus/world/map_data.h"

#include <string>
#include <vector>

namespace daedalus::editor
{

/// A reusable prefab: a named snapshot of sectors and entities whose positions
/// are stored relative to a pivot point (centroid of the captured AABB).
/// When placed, every position is offset by the chosen world XZ coordinates.
///
/// Sector wall p0 values use (world_X, world_Z) stored as glm::vec2(x, y),
/// shifted so the pivot maps to (0, 0).  Portal links are stripped — they
/// reference absolute sector indices that become meaningless after re-placement.
struct PrefabDef
{
    std::string name;

    /// Sector geometry with wall XZ positions relative to the pivot.
    std::vector<world::Sector> sectors;

    /// Entities whose XZ position has been made relative to the pivot.
    std::vector<EntityDef> entities;
};

} // namespace daedalus::editor
