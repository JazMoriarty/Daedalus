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
};

struct SelectionState
{
    SelectionType type = SelectionType::None;

    /// Selected sector indices (valid when type == Sector).
    std::vector<world::SectorId> sectors;

    /// For type == Wall: the owning sector and wall index within it.
    world::SectorId wallSectorId = world::INVALID_SECTOR_ID;
    std::size_t     wallIndex    = 0;

    void clear() noexcept
    {
        type         = SelectionType::None;
        sectors.clear();
        wallSectorId = world::INVALID_SECTOR_ID;
        wallIndex    = 0;
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
};

} // namespace daedalus::editor
