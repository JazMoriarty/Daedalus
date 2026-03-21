#pragma once

#include "daedalus/world/world_types.h"

#include <string>
#include <vector>

namespace daedalus::editor
{

class EditMapDocument;

/// Panel that implements the Multi-Floor 2D View described in the design spec.
///
/// An **edit height slider** controls the current working Y level.  Sectors
/// whose [floorHeight, ceilHeight] range does not include this value are shown
/// at 20 % opacity in the 2D viewport.  This makes multi-floor maps legible:
/// only the floor the designer is currently editing renders at full brightness.
///
/// A floor-group list (sectors partitioned into portal-linked vertical stacks)
/// lets the user jump between floors quickly: clicking a group sets the edit
/// height to the midpoint of that group's Y range.
///
/// **Show All** disables the height filter and renders every floor at full
/// opacity for top-level spatial reasoning and cross-floor portal linking.
class FloorLayerPanel
{
public:
    FloorLayerPanel()  = default;
    ~FloorLayerPanel() = default;

    void draw(EditMapDocument& doc);

    // ─── Viewport2D integration ───────────────────────────────────────────────

    /// True when the height filter is active (not "show all").
    [[nodiscard]] bool heightFilterActive() const noexcept { return m_filterEnabled; }

    /// Returns true if a sector spanning [floorH, ceilH] should be rendered at
    /// full opacity at the current edit height.  Always true when the filter is
    /// disabled.  Sectors whose range contains the edit height return true;
    /// all others return false and should be drawn at 20 % opacity.
    [[nodiscard]] bool isSectorFullOpacity(float floorH, float ceilH) const noexcept;

private:
    bool  m_filterEnabled = false;
    float m_editHeight    = 0.0f;  ///< Current working Y level (world units).

    /// User-saved named height presets.
    struct Preset { std::string name; float y; };
    std::vector<Preset> m_presets;

    /// Transitive portal-linked group starting from `seed`.
    static std::vector<world::SectorId> collectLayer(const EditMapDocument& doc,
                                                     world::SectorId        seed);
};

} // namespace daedalus::editor
