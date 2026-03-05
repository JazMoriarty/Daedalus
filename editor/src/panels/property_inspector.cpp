#include "property_inspector.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"
#include "document/commands/cmd_set_sector_heights.h"
#include "document/commands/cmd_set_wall_flags.h"
#include "document/commands/cmd_link_portal.h"
#include "document/commands/cmd_unlink_portal.h"
#include "tools/geometry_utils.h"

#include "imgui.h"

#include <cstddef>
#include <memory>

namespace daedalus::editor
{

void PropertyInspector::draw(EditMapDocument& doc)
{
    ImGui::Begin("Properties");

    const SelectionState& sel = doc.selection();

    if (sel.type == SelectionType::Sector && !sel.sectors.empty())
    {
        const world::SectorId sid = sel.sectors.front();
        auto& sectors = doc.mapData().sectors;

        if (sid >= sectors.size())
        {
            ImGui::TextDisabled("(invalid selection)");
            ImGui::End();
            return;
        }

        world::Sector& sector = sectors[sid];

        ImGui::SeparatorText("Sector");
        ImGui::Text("Index: %u", static_cast<unsigned>(sid));
        ImGui::Text("Walls: %u", static_cast<unsigned>(sector.walls.size()));

        ImGui::Spacing();
        ImGui::SeparatorText("Heights");

        // Floor height — commit on deactivate to create a single undo action.
        {
            float floor = sector.floorHeight;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##floor", &floor, 0.1f, -100.0f, 100.0f, "Floor: %.2f");
            if (ImGui::IsItemDeactivatedAfterEdit() && floor != sector.floorHeight)
            {
                doc.pushCommand(std::make_unique<CmdSetSectorHeights>(
                    doc, sid, floor, sector.ceilHeight));
            }
        }
        {
            float ceil = sector.ceilHeight;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##ceil", &ceil, 0.1f, -100.0f, 100.0f, "Ceiling: %.2f");
            if (ImGui::IsItemDeactivatedAfterEdit() && ceil != sector.ceilHeight)
            {
                doc.pushCommand(std::make_unique<CmdSetSectorHeights>(
                    doc, sid, sector.floorHeight, ceil));
            }
        }

        // ── Wall list ─────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Walls");

        for (std::size_t wi = 0; wi < sector.walls.size(); ++wi)
        {
            const world::Wall& wall = sector.walls[wi];
            ImGui::PushID(static_cast<int>(wi));

            if (ImGui::TreeNode("Wall", "Wall %zu  (%.1f, %.1f)",
                                wi, wall.p0.x, wall.p0.y))
            {
                // Blocking flag checkbox.
                bool blocking = hasFlag(wall.flags, world::WallFlags::Blocking);
                if (ImGui::Checkbox("Blocking", &blocking))
                {
                    const world::WallFlags newFlags = blocking
                        ? (wall.flags | world::WallFlags::Blocking)
                        : static_cast<world::WallFlags>(
                              static_cast<unsigned>(wall.flags) &
                              ~static_cast<unsigned>(world::WallFlags::Blocking));
                    doc.pushCommand(std::make_unique<CmdSetWallFlags>(
                        doc, sid, wi, newFlags));
                }

                bool twoSided = hasFlag(wall.flags, world::WallFlags::TwoSided);
                if (ImGui::Checkbox("Two-sided", &twoSided))
                {
                    const world::WallFlags newFlags = twoSided
                        ? (wall.flags | world::WallFlags::TwoSided)
                        : static_cast<world::WallFlags>(
                              static_cast<unsigned>(wall.flags) &
                              ~static_cast<unsigned>(world::WallFlags::TwoSided));
                    doc.pushCommand(std::make_unique<CmdSetWallFlags>(
                        doc, sid, wi, newFlags));
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }
    }
    else if (sel.type == SelectionType::Wall)
    {
        const world::SectorId sid = sel.wallSectorId;
        const std::size_t     wi  = sel.wallIndex;
        auto& sectors = doc.mapData().sectors;

        if (sid >= sectors.size() || wi >= sectors[sid].walls.size())
        {
            ImGui::TextDisabled("(invalid selection)");
            ImGui::End();
            return;
        }

        world::Wall& wall = sectors[sid].walls[wi];
        const std::size_t n = sectors[sid].walls.size();
        const glm::vec2   p0 = wall.p0;
        const glm::vec2   p1 = sectors[sid].walls[(wi + 1) % n].p0;

        ImGui::SeparatorText("Wall");
        ImGui::Text("Sector %u  Wall %zu", static_cast<unsigned>(sid), wi);
        ImGui::Text("p0 (%.2f, %.2f)  p1 (%.2f, %.2f)", p0.x, p0.y, p1.x, p1.y);

        ImGui::Spacing();
        ImGui::SeparatorText("Flags");
        {
            bool blocking = hasFlag(wall.flags, world::WallFlags::Blocking);
            if (ImGui::Checkbox("Blocking", &blocking))
            {
                const world::WallFlags newFlags = blocking
                    ? (wall.flags | world::WallFlags::Blocking)
                    : static_cast<world::WallFlags>(
                          static_cast<unsigned>(wall.flags) &
                          ~static_cast<unsigned>(world::WallFlags::Blocking));
                doc.pushCommand(std::make_unique<CmdSetWallFlags>(doc, sid, wi, newFlags));
            }
            bool twoSided = hasFlag(wall.flags, world::WallFlags::TwoSided);
            if (ImGui::Checkbox("Two-sided", &twoSided))
            {
                const world::WallFlags newFlags = twoSided
                    ? (wall.flags | world::WallFlags::TwoSided)
                    : static_cast<world::WallFlags>(
                          static_cast<unsigned>(wall.flags) &
                          ~static_cast<unsigned>(world::WallFlags::TwoSided));
                doc.pushCommand(std::make_unique<CmdSetWallFlags>(doc, sid, wi, newFlags));
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Portal");

        if (wall.portalSectorId == world::INVALID_SECTOR_ID)
        {
            ImGui::TextDisabled("Not linked.");
            if (ImGui::Button("Link Portal"))
            {
                const auto [matchSid, matchWi] =
                    geometry::findMatchingWall(sid, wi, doc.mapData());
                if (matchSid == world::INVALID_SECTOR_ID)
                {
                    doc.log("Link Portal: no matching wall found in adjacent sectors.");
                }
                else
                {
                    doc.pushCommand(std::make_unique<CmdLinkPortal>(
                        doc, sid, wi, matchSid, matchWi));
                    doc.log(std::string("Linked wall ") +
                            std::to_string(wi) + " (sector " +
                            std::to_string(sid) + ") <-> wall " +
                            std::to_string(matchWi) + " (sector " +
                            std::to_string(matchSid) + ").");
                }
            }
        }
        else
        {
            ImGui::Text("\xe2\x86\x92 Sector %u  Wall %zu",
                        static_cast<unsigned>(wall.portalSectorId),
                        static_cast<unsigned>(wi));

            // Find the partner wall index for the unlink command.
            if (ImGui::Button("Unlink Portal"))
            {
                const world::SectorId partnerSid = wall.portalSectorId;
                // Scan partner sector for the wall that points back to us.
                std::size_t partnerWi = 0;
                bool found = false;
                if (partnerSid < doc.mapData().sectors.size())
                {
                    const auto& pSec = doc.mapData().sectors[partnerSid];
                    for (std::size_t pw = 0; pw < pSec.walls.size(); ++pw)
                    {
                        if (pSec.walls[pw].portalSectorId == sid)
                        {
                            partnerWi = pw;
                            found = true;
                            break;
                        }
                    }
                }
                if (found)
                {
                    doc.pushCommand(std::make_unique<CmdUnlinkPortal>(
                        doc, sid, wi, partnerSid, partnerWi));
                }
                else
                {
                    // Partner not found (orphaned link) — just clear our side.
                    doc.pushCommand(std::make_unique<CmdUnlinkPortal>(
                        doc, sid, wi, world::INVALID_SECTOR_ID, 0));
                }
            }
        }
    }
    else if (sel.type == SelectionType::Vertex)
    {
        const world::SectorId sid = sel.vertexSectorId;
        const std::size_t     wi  = sel.vertexWallIndex;
        auto& sectors = doc.mapData().sectors;

        if (sid < sectors.size() && wi < sectors[sid].walls.size())
        {
            const glm::vec2 p = sectors[sid].walls[wi].p0;
            ImGui::SeparatorText("Vertex");
            ImGui::Text("Sector %u  Wall %zu", static_cast<unsigned>(sid), wi);
            ImGui::Text("Position: (%.3f, %.3f)", p.x, p.y);
            ImGui::Spacing();
            ImGui::TextDisabled("Drag in 2D viewport to move.");
        }
    }
    else if (sel.type == SelectionType::None)
    {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::Spacing();
        ImGui::TextDisabled("Click a sector in the 2D viewport.");
    }

    ImGui::End();
}

} // namespace daedalus::editor
