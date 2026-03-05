#include "property_inspector.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"
#include "document/commands/cmd_set_sector_heights.h"
#include "document/commands/cmd_set_wall_flags.h"

#include "imgui.h"

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
    else if (sel.type == SelectionType::None)
    {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::Spacing();
        ImGui::TextDisabled("Click a sector in the 2D viewport.");
    }

    ImGui::End();
}

} // namespace daedalus::editor
