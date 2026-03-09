// selection_state.h
// Tracks which map objects are currently selected in the editor.

#pragma once

#include "daedalus/world/world_types.h"  // SectorId, INVALID_SECTOR_ID

#include <cstddef>
#include <vector>

namespace daedalus::editor
{

enum class SelectionType : unsigned
{
    None   = 0,
    Sector = 1,
    Wall   = 2,
    Vertex = 3,
    Light       = 4,
    Entity      = 5,  ///< An editor-placed EntityDef.
    PlayerStart = 6,  ///< The map's player spawn point.
};

struct SelectionState
{
    SelectionType type = SelectionType::None;

    /// Selected sector indices (valid when type == Sector).
    std::vector<world::SectorId> sectors;

    /// For type == Wall: the owning sector and wall index within it.
    world::SectorId wallSectorId = world::INVALID_SECTOR_ID;
    std::size_t     wallIndex    = 0;

    /// For type == Vertex: the owning sector and the wall whose p0 is the vertex.
    world::SectorId vertexSectorId  = world::INVALID_SECTOR_ID;
    std::size_t     vertexWallIndex = 0;

    /// For type == Light: index into EditMapDocument::lights().
    std::size_t lightIndex = 0;

    /// For type == Entity: index into EditMapDocument::entities().
    std::size_t entityIndex = 0;

    void clear() noexcept
    {
        type            = SelectionType::None;
        sectors.clear();
        wallSectorId    = world::INVALID_SECTOR_ID;
        wallIndex       = 0;
        vertexSectorId  = world::INVALID_SECTOR_ID;
        vertexWallIndex = 0;
        lightIndex      = 0;
        entityIndex     = 0;
    }

    [[nodiscard]] bool hasSelection() const noexcept
    {
        return type != SelectionType::None;
    }

    [[nodiscard]] bool isSectorSelected(world::SectorId id) const noexcept
    {
        if (type != SelectionType::Sector) return false;
        for (auto sid : sectors)
            if (sid == id) return true;
        return false;
    }

    /// Select every sector in a map that has `sectorCount` sectors.
    void selectAll(std::size_t sectorCount)
    {
        clear();
        type = SelectionType::Sector;
        sectors.reserve(sectorCount);
        for (std::size_t i = 0; i < sectorCount; ++i)
            sectors.push_back(static_cast<world::SectorId>(i));
    }
};

} // namespace daedalus::editor
