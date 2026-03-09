// map_doctor.h
// Validates a WorldMapData instance and emits structured log entries for any
// detected issues.
//
// diagnose()      — pure check; returns a list of issues.  Testable without
//                   an EditMapDocument.
// runMapDoctor()  — calls diagnose() and logs results to doc.

#pragma once

#include "daedalus/editor/selection_state.h"
#include "daedalus/world/map_data.h"

#include <optional>
#include <string>
#include <vector>

namespace daedalus::editor
{

class EditMapDocument;

// ─── MapIssue ─────────────────────────────────────────────────────────────────

struct MapIssue
{
    std::string                   description;
    std::optional<SelectionState> jumpTo; ///< If set, OutputLog renders a navigate button.
};

// ─── diagnose ─────────────────────────────────────────────────────────────────
// Runs all validation checks and returns every detected issue.
// Does NOT modify any state; safe to call at any time.

[[nodiscard]] std::vector<MapIssue> diagnose(const world::WorldMapData& map);

// ─── WallHighlight ───────────────────────────────────────────────────────────────
// Per-wall colour data used by the 2D viewport continuous validation overlay.

enum class WallHighlightKind : uint8_t
{
    SelfIntersecting, ///< Wall belongs to a self-intersecting sector.    (red)
    ZeroLength,       ///< Wall has zero length.                          (red)
    OrphanedPortal,   ///< Portal target sector does not exist.           (orange)
    MissingBackLink,  ///< Portal has no matching reverse link.           (yellow)
};

struct WallHighlight
{
    world::SectorId      sectorId;
    std::size_t          wallIndex;
    WallHighlightKind    kind;
};

/// Returns per-wall highlight descriptors for all detected geometry issues.
/// Safe to call every frame on any thread; does not modify state.
[[nodiscard]] std::vector<WallHighlight> getWallHighlights(const world::WorldMapData& map);

// ─── runMapDoctor ───────────────────────────────────────────────────────────────
// Calls diagnose() and appends each issue to doc's output log as a navigable
// LogEntry.  Reports a one-line summary first.

void runMapDoctor(EditMapDocument& doc);

} // namespace daedalus::editor
