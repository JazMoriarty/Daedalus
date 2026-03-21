// selection_state.h
// Tracks which map objects are currently selected in the editor.

#pragma once

#include "daedalus/world/world_types.h"  // SectorId, INVALID_SECTOR_ID

#include <algorithm>
#include <cstddef>
#include <vector>

namespace daedalus::editor
{

/// The kind of object described by a SelectionItem.
/// To add a new object type, append a new enumerator here — no other
/// SelectionItem or SelectionState fields need to change.
enum class SelectionType : unsigned
{
    None        = 0,
    Sector      = 1,
    Wall        = 2,
    Vertex      = 3,
    Light       = 4,
    Entity      = 5,  ///< An editor-placed EntityDef.
    PlayerStart = 6,  ///< The map's player spawn point.
};

/// Identifies exactly one selected object in the document.
///
/// Field semantics by type:
///  - Sector     : sectorId = sector ID,            index = (unused)
///  - Wall       : sectorId = owning sector ID,     index = wall index
///  - Vertex     : sectorId = owning sector ID,     index = wall index whose p0 is the vertex
///  - Light      : sectorId = (INVALID_SECTOR_ID),  index = lights() index
///  - Entity     : sectorId = (INVALID_SECTOR_ID),  index = entities() index
///  - PlayerStart: sectorId = (INVALID_SECTOR_ID),  index = (unused)
struct SelectionItem
{
    SelectionType   type     = SelectionType::None;
    world::SectorId sectorId = world::INVALID_SECTOR_ID;
    std::size_t     index    = 0;

    [[nodiscard]] bool operator==(const SelectionItem&) const noexcept = default;
};

/// The complete selection state of the editor.
/// Contains zero or more SelectionItems of any mix of types.
struct SelectionState
{
    std::vector<SelectionItem> items;

    /// Remove all selected objects.
    void clear() noexcept { items.clear(); }

    /// True if at least one object is selected.
    [[nodiscard]] bool hasSelection() const noexcept { return !items.empty(); }

    /// Returns the common SelectionType if all items share one type, or
    /// SelectionType::None if the selection is empty or contains mixed types.
    [[nodiscard]] SelectionType uniformType() const noexcept
    {
        if (items.empty()) return SelectionType::None;
        const SelectionType t = items[0].type;
        for (const auto& item : items)
            if (item.type != t) return SelectionType::None;
        return t;
    }

    /// True iff the selection contains exactly one item of the given type.
    [[nodiscard]] bool hasSingleOf(SelectionType t) const noexcept
    {
        return items.size() == 1 && items[0].type == t;
    }

    /// True if the given entity (by its entities() index) is selected.
    [[nodiscard]] bool isEntitySelected(std::size_t idx) const noexcept
    {
        for (const auto& item : items)
            if (item.type == SelectionType::Entity && item.index == idx)
                return true;
        return false;
    }

    /// True if the given light (by its lights() index) is selected.
    [[nodiscard]] bool isLightSelected(std::size_t idx) const noexcept
    {
        for (const auto& item : items)
            if (item.type == SelectionType::Light && item.index == idx)
                return true;
        return false;
    }

    /// True if the given vertex (identified by owning sector + wall index) is selected.
    [[nodiscard]] bool isVertexSelected(world::SectorId sid, std::size_t wi) const noexcept
    {
        for (const auto& item : items)
            if (item.type == SelectionType::Vertex &&
                item.sectorId == sid && item.index == wi)
                return true;
        return false;
    }

    /// True if the given sector is among the selected items.
    [[nodiscard]] bool isSectorSelected(world::SectorId id) const noexcept
    {
        for (const auto& item : items)
            if (item.type == SelectionType::Sector && item.sectorId == id)
                return true;
        return false;
    }

    /// Returns the IDs of all selected sectors (items of type Sector).
    [[nodiscard]] std::vector<world::SectorId> selectedSectors() const
    {
        std::vector<world::SectorId> result;
        for (const auto& item : items)
            if (item.type == SelectionType::Sector)
                result.push_back(item.sectorId);
        return result;
    }

    /// Replace the selection with every sector in a map of `sectorCount` sectors.
    void selectAll(std::size_t sectorCount)
    {
        clear();
        items.reserve(sectorCount);
        for (std::size_t i = 0; i < sectorCount; ++i)
            items.push_back({SelectionType::Sector,
                             static_cast<world::SectorId>(i), 0});
    }
};

} // namespace daedalus::editor
