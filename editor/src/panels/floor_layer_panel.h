#pragma once

#include "daedalus/world/world_types.h"

#include <vector>

namespace daedalus::editor
{

class EditMapDocument;

/// Panel that shows vertically-stacked floor layers linked via floor/ceiling
/// portals and lets the user filter the 2D viewport to one floor at a time.
///
/// A "floor layer" is the transitive closure of sectors connected by
/// floorPortalSectorId / ceilPortalSectorId links.  The panel lists all layers
/// and exposes a "show all floors" / "show layer N" toggle so that in a
/// multi-floor map the 2D viewport is not cluttered by overlapping sectors.
///
/// The active floor filter index is stored here; Viewport2D reads it via
/// activeFloorFilter() to dim or hide out-of-layer sectors when
/// floorFilterEnabled() is true.
class FloorLayerPanel
{
public:
    FloorLayerPanel()  = default;
    ~FloorLayerPanel() = default;

    void draw(EditMapDocument& doc);

    // ─── Viewport2D integration ───────────────────────────────────────────────

    /// True when a specific floor layer is selected (not "show all").
    [[nodiscard]] bool floorFilterEnabled() const noexcept { return m_filterEnabled; }

    /// Returns true if the given sector should be displayed at full opacity.
    /// When the filter is disabled every sector returns true.
    [[nodiscard]] bool isSectorVisible(world::SectorId id) const noexcept;

private:
    bool m_filterEnabled = false;

    /// The sectors belonging to the currently selected floor layer.
    std::vector<world::SectorId> m_visibleSectors;

    /// Collect the transitive portal-linked group starting from `seed`.
    static std::vector<world::SectorId> collectLayer(const EditMapDocument& doc,
                                                     world::SectorId        seed);
};

} // namespace daedalus::editor
