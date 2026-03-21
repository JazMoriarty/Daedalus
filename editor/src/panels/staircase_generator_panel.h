#pragma once

#include "daedalus/world/world_types.h"

#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Panel for generating staircases in two modes.
///
/// **Visual Stair Mode** (FloorShape::VisualStairs) —
///   Applies a StairProfile to the currently selected sector so the tessellator
///   renders a stair-step mesh.  The physics collision uses a ramp.
///
/// **Sector Chain Mode** —
///   Generates a chain of new rectangular sectors whose floor heights step up by
///   riserHeight each, connected by wall portals so they form a climbable stair
///   corridor.  The user specifies the width, number of steps, and direction.
///
/// Both modes are fully undoable: they push a single CompoundCommand onto the
/// undo stack.
class StaircaseGeneratorPanel
{
public:
    StaircaseGeneratorPanel()  = default;
    ~StaircaseGeneratorPanel() = default;

    void draw(EditMapDocument& doc);

private:
    // ─── Shared parameters ────────────────────────────────────────────────────
    int   m_stepCount   = 6;
    float m_riserHeight = 0.25f;
    float m_treadDepth  = 0.5f;
    float m_dirDeg      = 0.0f;   ///< Stair run direction (degrees CCW from +X axis).

    // ─── Sector-chain mode parameters ─────────────────────────────────────────
    float m_corridorWidth = 2.0f;  ///< Width of each step sector (perpendicular to run).

    // ─── Mode tabs ────────────────────────────────────────────────────────────
    int m_modeTab = 0;  ///< 0 = Visual Stair, 1 = Sector Chain.

    // ─── Error / info message ─────────────────────────────────────────────────
    std::string m_statusMsg;
    bool        m_statusIsError = false;

    void applyVisualStair(EditMapDocument& doc, world::SectorId sid);
    void applySectorChain(EditMapDocument& doc, world::SectorId sid);
};

} // namespace daedalus::editor
