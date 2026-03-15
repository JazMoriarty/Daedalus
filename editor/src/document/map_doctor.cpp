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

        // Build vertex list once; reused by multiple checks below.
        std::vector<glm::vec2> verts;
        verts.reserve(n);
        for (const auto& wall : sector.walls)
            verts.push_back(wall.p0);

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
                    else
                    {
                        // ── 2a. Portal geometry mismatch ──────────────────────
                        // Reverse link exists — verify the wall edges are
                        // geometrically aligned with the target sector boundary.
                        const world::SectorId geomSec =
                            geometry::findMatchingWall(sid, wi, map).first;
                        if (geomSec != pid)
                        {
                            issues.push_back({
                                std::format("Sector {} wall {}: portal references sector {} "
                                            "but wall edge does not geometrically align with "
                                            "that sector's boundary.",
                                            si, wi, pid),
                                makeWallSel(sid, wi)
                            });
                        }
                    }
                }
            }
        }

        // ── 3. Floor/ceiling inversion ────────────────────────────────────────
        if (sector.floorHeight >= sector.ceilHeight)
        {
            issues.push_back({
                std::format("Sector {}: floor height ({:.2f}) must be less than "
                            "ceiling height ({:.2f}).",
                            si, sector.floorHeight, sector.ceilHeight),
                makeSectorSel(sid)
            });
        }

        // ── 4. Self-intersecting polygon ──────────────────────────────────────
        if (geometry::isSelfIntersecting(verts))
        {
            issues.push_back({
                std::format("Sector {}: polygon is self-intersecting.", si),
                makeSectorSel(sid)
            });
        }

        // ── 5. Winding order ──────────────────────────────────────────────────
        if (geometry::signedArea(verts) < 0.0f)
        {
            issues.push_back({
                std::format("Sector {}: polygon is wound clockwise (expected CCW).", si),
                makeSectorSel(sid)
            });
        }

        // ── 6. Duplicate non-adjacent vertices ────────────────────────────────
        for (std::size_t wi = 0; wi < n; ++wi)
        {
            for (std::size_t wj = wi + 2; wj < n; ++wj)
            {
                if (wi == 0 && wj == n - 1) continue;  // adjacent wrap-around pair
                const glm::vec2 delta = sector.walls[wi].p0 - sector.walls[wj].p0;
                if (glm::dot(delta, delta) < 1e-6f)
                {
                    issues.push_back({
                        std::format("Sector {}: walls {} and {} share the same vertex "
                                    "position (duplicate non-adjacent vertex).",
                                    si, wi, wj),
                        makeSectorSel(sid)
                    });
                }
            }
        }
    }

    // ── 7. Sector overlaps ────────────────────────────────────────────────────
    // O(n²) pass over all sector pairs.  Proper edge-edge intersection or full
    // containment both count; shared portal boundaries do not.
    for (std::size_t si = 0; si < map.sectors.size(); ++si)
    {
        const auto& secA = map.sectors[si];
        if (secA.walls.size() < 3) continue;

        std::vector<glm::vec2> vertsA;
        vertsA.reserve(secA.walls.size());
        for (const auto& w : secA.walls) vertsA.push_back(w.p0);

        for (std::size_t sj = si + 1; sj < map.sectors.size(); ++sj)
        {
            const auto& secB = map.sectors[sj];
            if (secB.walls.size() < 3) continue;

            std::vector<glm::vec2> vertsB;
            vertsB.reserve(secB.walls.size());
            for (const auto& w : secB.walls) vertsB.push_back(w.p0);

            if (geometry::polygonsOverlap(vertsA, vertsB))
            {
                issues.push_back({
                    std::format("Sectors {} and {} have overlapping polygons.", si, sj),
                    makeSectorSel(static_cast<world::SectorId>(si))
                });
            }
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
        const bool isCW      = geometry::signedArea(verts) < 0.0f;

        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const auto& wall = sector.walls[wi];
            const glm::vec2 p1 = sector.walls[(wi + 1) % n].p0;

            // Zero-length edge (takes priority over all other kinds).
            const glm::vec2 d = p1 - wall.p0;
            if (glm::dot(d, d) < 1e-6f)
            {
                out.push_back({sid, wi, WallHighlightKind::ZeroLength});
                continue;
            }

            if (isSelfInt)
                out.push_back({sid, wi, WallHighlightKind::SelfIntersecting});

            if (isCW)
                out.push_back({sid, wi, WallHighlightKind::WindingOrder});

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
                    {
                        out.push_back({sid, wi, WallHighlightKind::MissingBackLink});
                    }
                    else
                    {
                        const world::SectorId geomSec =
                            geometry::findMatchingWall(sid, wi, map).first;
                        if (geomSec != pid)
                            out.push_back({sid, wi, WallHighlightKind::PortalGeomMismatch});
                    }
                }
            }
        }
    }

    return out;
}

} // namespace daedalus::editor
