#include "map_doctor.h"
#include "tools/geometry_utils.h"

#include <format>
#include <unordered_set>
#include <vector>

namespace daedalus::editor
{

// ─── Helpers ─────────────────────────────────────────────────────────────────

static SelectionState makeSectorSel(world::SectorId si)
{
    SelectionState s;
    s.type = SelectionType::Sector;
    s.sectors.push_back(si);
    return s;
}

static SelectionState makeWallSel(world::SectorId si, std::size_t wi)
{
    SelectionState s;
    s.type         = SelectionType::Wall;
    s.wallSectorId = si;
    s.wallIndex    = wi;
    return s;
}

// ─── diagnose ────────────────────────────────────────────────────────────────

std::vector<MapIssue> diagnose(const world::WorldMapData& map)
{
    std::vector<MapIssue> issues;

    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const auto&       sector = map.sectors[si];
        const auto        sid    = static_cast<world::SectorId>(si);
        const std::size_t n      = sector.walls.size();

        // ── 1. Fewer than 3 walls ─────────────────────────────────────────────
        if (n < 3)
        {
            issues.push_back({
                std::format("Sector {}: fewer than 3 walls ({} wall{}).",
                            si, n, n == 1 ? "" : "s"),
                makeSectorSel(sid)
            });
            continue; // Per-wall checks are meaningless on a degenerate sector.
        }

        // ── 2. Per-wall checks ────────────────────────────────────────────────
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const auto& wall = sector.walls[wi];
            const glm::vec2 p1 = sector.walls[(wi + 1) % n].p0;

            // Zero-length edge.
            const glm::vec2 d = p1 - wall.p0;
            if (glm::dot(d, d) < 1e-6f)
            {
                issues.push_back({
                    std::format("Sector {} wall {}: zero-length edge.", si, wi),
                    makeWallSel(sid, wi)
                });
            }

            // Portal checks.
            if (wall.portalSectorId != world::INVALID_SECTOR_ID)
            {
                const world::SectorId pid = wall.portalSectorId;

                if (pid >= static_cast<world::SectorId>(map.sectors.size()))
                {
                    // Target sector does not exist.
                    issues.push_back({
                        std::format("Sector {} wall {}: portal targets sector {} which does not exist.",
                                    si, wi, pid),
                        makeWallSel(sid, wi)
                    });
                }
                else
                {
                    // Target sector exists — check for a reverse link.
                    bool hasBack = false;
                    for (const auto& pw : map.sectors[pid].walls)
                    {
                        if (pw.portalSectorId == sid) { hasBack = true; break; }
                    }
                    if (!hasBack)
                    {
                        issues.push_back({
                            std::format("Sector {} wall {}: portal to sector {} has no reverse link.",
                                        si, wi, pid),
                            makeWallSel(sid, wi)
                        });
                    }
                }
            }
        }

        // ── 3. Self-intersecting polygon ──────────────────────────────────────
        std::vector<glm::vec2> verts;
        verts.reserve(n);
        for (const auto& wall : sector.walls)
            verts.push_back(wall.p0);

        if (geometry::isSelfIntersecting(verts))
        {
            issues.push_back({
                std::format("Sector {}: polygon is self-intersecting.", si),
                makeSectorSel(sid)
            });
        }
    }

    return issues;
}

// ─── getWallHighlights ────────────────────────────────────────────────────────────

std::vector<WallHighlight> getWallHighlights(const world::WorldMapData& map)
{
    std::vector<WallHighlight> out;

    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const auto&       sector = map.sectors[si];
        const auto        sid    = static_cast<world::SectorId>(si);
        const std::size_t n      = sector.walls.size();
        if (n < 3) continue;

        // Check for self-intersecting polygon once per sector; mark all walls.
        std::vector<glm::vec2> verts;
        verts.reserve(n);
        for (const auto& wall : sector.walls)
            verts.push_back(wall.p0);

        const bool isSelfInt = geometry::isSelfIntersecting(verts);

        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const auto& wall = sector.walls[wi];
            const glm::vec2 p1 = sector.walls[(wi + 1) % n].p0;

            // Zero-length edge (takes priority over self-intersecting).
            const glm::vec2 d = p1 - wall.p0;
            if (glm::dot(d, d) < 1e-6f)
            {
                out.push_back({sid, wi, WallHighlightKind::ZeroLength});
                continue;
            }

            if (isSelfInt)
                out.push_back({sid, wi, WallHighlightKind::SelfIntersecting});

            if (wall.portalSectorId != world::INVALID_SECTOR_ID)
            {
                const world::SectorId pid = wall.portalSectorId;
                if (pid >= static_cast<world::SectorId>(map.sectors.size()))
                {
                    out.push_back({sid, wi, WallHighlightKind::OrphanedPortal});
                }
                else
                {
                    bool hasBack = false;
                    for (const auto& pw : map.sectors[pid].walls)
                        if (pw.portalSectorId == sid) { hasBack = true; break; }
                    if (!hasBack)
                        out.push_back({sid, wi, WallHighlightKind::MissingBackLink});
                }
            }
        }
    }

    return out;
}

} // namespace daedalus::editor
