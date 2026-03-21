#include "property_inspector.h"
#include "asset_browser_panel.h"
#include "catalog/material_catalog.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"
#include "document/commands/cmd_set_sector_heights.h"
#include "document/commands/cmd_set_sector_ambient.h"
#include "document/commands/cmd_set_sector_flags.h"
#include "document/commands/cmd_set_wall_flags.h"
#include "document/commands/cmd_set_vertex_height.h"
#include "document/commands/cmd_set_sector_floor_shape.h"
#include "document/commands/cmd_set_floor_portal.h"
#include "document/commands/cmd_set_wall_curve.h"
#include "document/commands/cmd_set_heightfield.h"
#include "document/commands/cmd_set_wall_uv.h"
#include "daedalus/editor/compound_command.h"
#include "uv_utils.h"
#include "document/commands/cmd_link_portal.h"
#include "document/commands/cmd_unlink_portal.h"
#include "document/commands/cmd_move_light.h"
#include "document/commands/cmd_set_light_props.h"
#include "document/commands/cmd_delete_light.h"
#include "document/commands/cmd_move_entity.h"
#include "document/commands/cmd_set_entity_props.h"
#include "document/commands/cmd_delete_entity.h"
#include "document/commands/cmd_set_player_start.h"
#include "document/commands/cmd_set_wall_material.h"
#include "document/commands/cmd_set_sector_material.h"
#include "document/commands/cmd_set_sector_surface_uv.h"
#include "document/commands/cmd_set_map_meta.h"
#include "document/commands/cmd_set_map_defaults.h"
#include "document/commands/cmd_set_global_ambient.h"
#include "tools/geometry_utils.h"

#include "imgui.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

namespace daedalus::editor
{

// Colour-edit widget wrapper that survives per-frame local re-initialisation.
// ImGui's colour picker opens a popup; without this the outer widget's value
// reference is re-created from stored data each frame, so in-progress changes
// are lost when IsItemDeactivatedAfterEdit fires.  A static slot keyed by
// widget ID holds the live value until the user finishes editing.
// Returns true (and writes the committed colour into col[0..channels-1]) when
// the user deactivates the widget after editing.
static bool colorEditUndo(const char* label, float* col, int channels,
                          ImGuiColorEditFlags flags = 0)
{
    static ImGuiID s_id     = 0;
    static float   s_val[4] = {};

    // Compute the widget ID before drawing; must match what ColorEdit3/4 uses.
    const ImGuiID wid = ImGui::GetID(label);

    // Restore the live in-progress value so the re-initialised local is ignored.
    if (s_id == wid)
        for (int i = 0; i < channels; ++i) col[i] = s_val[i];

    if (channels == 3) ImGui::ColorEdit3(label, col, flags);
    else               ImGui::ColorEdit4(label, col, flags);

    if (ImGui::IsItemActive() || ImGui::IsItemEdited())
    {
        s_id = wid;
        for (int i = 0; i < channels; ++i) s_val[i] = col[i];
    }

    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        if (s_id == wid)
        {
            for (int i = 0; i < channels; ++i) col[i] = s_val[i];
            s_id = 0;
        }
        return true;
    }
    return false;
}

// DragFloat/DragInt wrappers that survive per-frame local re-initialisation.
// Without these helpers, the caller re-initialises the local variable from
// stored data every frame, so ImGui's drag-accumulator is always applied to
// the STORED value instead of the previously-dragged value — dragged values
// never accumulate beyond one frame's delta.  Each helper stores the
// in-progress value in a static slot keyed by widget ID, restores it before
// calling the ImGui widget, and returns true on IsItemDeactivatedAfterEdit.
// NOTE: only one widget of each type can be active at once, which is
// guaranteed by ImGui's single-active-item model.
static bool dragFloatUndo(const char* label, float* v, float speed,
                           float v_min, float v_max, const char* fmt,
                           ImGuiSliderFlags flags = 0, float snapStep = 0.0f)
{
    static ImGuiID s_id  = 0;
    static float   s_val = 0.0f;
    const ImGuiID wid = ImGui::GetID(label);
    if (s_id == wid) *v = s_val;
    ImGui::DragFloat(label, v, speed, v_min, v_max, fmt, flags);
    if (ImGui::IsItemActive() || ImGui::IsItemEdited())
    {
        if (snapStep > 0.0f && !ImGui::GetIO().KeyShift)
            *v = std::round(*v / snapStep) * snapStep;
        s_id = wid; s_val = *v;
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        { if (s_id == wid) { *v = s_val; s_id = 0; } return true; }
    return false;
}

static bool dragFloat2Undo(const char* label, float* v, float speed,
                            float v_min, float v_max, const char* fmt,
                            ImGuiSliderFlags flags = 0)
{
    static ImGuiID s_id     = 0;
    static float   s_val[2] = {};
    const ImGuiID wid = ImGui::GetID(label);
    if (s_id == wid) { v[0] = s_val[0]; v[1] = s_val[1]; }
    ImGui::DragFloat2(label, v, speed, v_min, v_max, fmt, flags);
    if (ImGui::IsItemActive() || ImGui::IsItemEdited())
        { s_id = wid; s_val[0] = v[0]; s_val[1] = v[1]; }
    if (ImGui::IsItemDeactivatedAfterEdit())
        { if (s_id == wid) { v[0] = s_val[0]; v[1] = s_val[1]; s_id = 0; } return true; }
    return false;
}

static bool dragFloat3Undo(const char* label, float* v, float speed,
                            float v_min, float v_max, const char* fmt,
                            ImGuiSliderFlags flags = 0, float snapStep = 0.0f)
{
    static ImGuiID s_id     = 0;
    static float   s_val[3] = {};
    const ImGuiID wid = ImGui::GetID(label);
    if (s_id == wid) { v[0] = s_val[0]; v[1] = s_val[1]; v[2] = s_val[2]; }
    ImGui::DragFloat3(label, v, speed, v_min, v_max, fmt, flags);
    if (ImGui::IsItemActive() || ImGui::IsItemEdited())
    {
        if (snapStep > 0.0f && !ImGui::GetIO().KeyShift)
        {
            v[0] = std::round(v[0] / snapStep) * snapStep;
            v[1] = std::round(v[1] / snapStep) * snapStep;
            v[2] = std::round(v[2] / snapStep) * snapStep;
        }
        s_id = wid; s_val[0] = v[0]; s_val[1] = v[1]; s_val[2] = v[2];
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        { if (s_id == wid) { v[0]=s_val[0]; v[1]=s_val[1]; v[2]=s_val[2]; s_id=0; } return true; }
    return false;
}

static bool dragIntUndo(const char* label, int* v, float speed,
                         int v_min, int v_max, const char* fmt,
                         ImGuiSliderFlags flags = 0)
{
    static ImGuiID s_id  = 0;
    static int     s_val = 0;
    const ImGuiID wid = ImGui::GetID(label);
    if (s_id == wid) *v = s_val;
    ImGui::DragInt(label, v, speed, v_min, v_max, fmt, flags);
    if (ImGui::IsItemActive() || ImGui::IsItemEdited())
        { s_id = wid; s_val = *v; }
    if (ImGui::IsItemDeactivatedAfterEdit())
        { if (s_id == wid) { *v = s_val; s_id = 0; } return true; }
    return false;
}

void PropertyInspector::draw(EditMapDocument&      doc,
                              MaterialCatalog&      catalog,
                              rhi::IRenderDevice&   device,
                              render::IAssetLoader& loader,
                              AssetBrowserPanel&    assetBrowser,
                              ModelCatalog*         voxCatalog,
                              float                 gridStep)
{
    ImGui::Begin("Properties");

    const SelectionState& sel = doc.selection();

    if (sel.uniformType() == SelectionType::Sector && !sel.items.empty())
    {
        const world::SectorId sid = sel.items[0].sectorId;
        auto& sectors = doc.mapData().sectors;

        if (sid >= sectors.size())
        {
            ImGui::TextDisabled("(invalid selection)");
            ImGui::End();
            return;
        }

        world::Sector& sector = sectors[sid];
        ImGui::PushID(static_cast<int>(sid));

        ImGui::SeparatorText("Sector");
        ImGui::Text("Index: %u", static_cast<unsigned>(sid));
        ImGui::Text("Walls: %u", static_cast<unsigned>(sector.walls.size()));

        ImGui::Spacing();
        ImGui::SeparatorText("Heights");
        {
            float floor = sector.floorHeight;
            ImGui::SetNextItemWidth(-1.0f);
            if (dragFloatUndo("##floor", &floor, 0.1f, 0.0f, 0.0f, "Floor: %.2f") &&
                floor != sector.floorHeight)
                doc.pushCommand(std::make_unique<CmdSetSectorHeights>(
                    doc, sid, floor, sector.ceilHeight));
        }
        {
            float ceil = sector.ceilHeight;
            ImGui::SetNextItemWidth(-1.0f);
            if (dragFloatUndo("##ceil", &ceil, 0.1f, 0.0f, 0.0f, "Ceiling: %.2f") &&
                ceil != sector.ceilHeight)
                doc.pushCommand(std::make_unique<CmdSetSectorHeights>(
                    doc, sid, sector.floorHeight, ceil));
        }

        // ── Flags ────────────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Flags");
        {
            // Helper: toggle a single SectorFlags bit and push a command.
            auto toggleSectorFlag = [&](const char* label, world::SectorFlags bit)
            {
                bool v = hasFlag(sector.flags, bit);
                if (ImGui::Checkbox(label, &v))
                {
                    const world::SectorFlags nf = v
                        ? (sector.flags | bit)
                        : static_cast<world::SectorFlags>(
                              static_cast<unsigned>(sector.flags) &
                              ~static_cast<unsigned>(bit));
                    doc.pushCommand(std::make_unique<CmdSetSectorFlags>(doc, sid, nf));
                }
            };
            toggleSectorFlag("Outdoors",    world::SectorFlags::Outdoors);
            toggleSectorFlag("Underwater",  world::SectorFlags::Underwater);
            toggleSectorFlag("Damage Zone", world::SectorFlags::DamageZone);
            toggleSectorFlag("Trigger Zone",world::SectorFlags::TriggerZone);
        }

        // ── Lighting ──────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Lighting");
        {
            glm::vec3 col = sector.ambientColor;
            if (colorEditUndo("Ambient##sec", &col.x, 3))
                doc.pushCommand(std::make_unique<CmdSetSectorAmbient>(
                    doc, sid, col, sector.ambientIntensity));
        }
        {
            float intensity = sector.ambientIntensity;
            ImGui::SetNextItemWidth(-1.0f);
            if (dragFloatUndo("##ambint", &intensity, 0.01f, 0.0f, 0.0f, "Intensity: %.2f") &&
                intensity != sector.ambientIntensity)
                doc.pushCommand(std::make_unique<CmdSetSectorAmbient>(
                    doc, sid, sector.ambientColor, intensity));
        }

        // ── Materials ────────────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Materials");
        {
            // Shared helper: thumbnail preview + Browse + clear row for a sector material.
            auto editSectorMat = [&](const char* label, const daedalus::UUID& current,
                                     SectorSurface surface)
            {
                ImGui::PushID(label);
                // Thumbnail (32x32).
                rhi::ITexture* thumb = current.isValid()
                    ? catalog.getOrLoadThumbnail(current, device) : nullptr;
                if (thumb)
                    ImGui::Image(reinterpret_cast<ImTextureID>(thumb->nativeHandle()), ImVec2(32, 32));
                else
                    ImGui::Dummy(ImVec2(32, 32));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(label);
                if (current.isValid())
                {
                    const MaterialEntry* entry = catalog.find(current);
                    ImGui::TextDisabled("%s", entry ? entry->displayName.c_str() : "(unknown)");
                }
                else
                    ImGui::TextDisabled("(none)");
                if (ImGui::SmallButton("Browse##sm"))
                {
                    const daedalus::UUID capId = current;
                    const char* surfLbl = (surface == SectorSurface::Floor) ? "Floor" : "Ceiling";
                    assetBrowser.openPicker([&doc, sid, surface, capId](const UUID& uuid) {
                        (void)capId;
                        doc.pushCommand(std::make_unique<CmdSetSectorMaterial>(doc, sid, surface, uuid));
                    }, surfLbl);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X##sm") && current.isValid())
                    doc.pushCommand(std::make_unique<CmdSetSectorMaterial>(doc, sid, surface, daedalus::UUID{}));
                ImGui::EndGroup();
                ImGui::PopID();
            };
            editSectorMat("Floor##sm",   sector.floorMaterialId, SectorSurface::Floor);
            ImGui::Spacing();
            editSectorMat("Ceiling##sm", sector.ceilMaterialId,  SectorSurface::Ceil);
        }

        // ── Floor Shape
        ImGui::Spacing();
        ImGui::SeparatorText("Floor Shape");
        {
            static const char* k_shapeLabels[] = {"Flat", "Visual Stairs", "Heightfield"};
            int shapeIdx = static_cast<int>(sector.floorShape);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##floorshape", &shapeIdx, k_shapeLabels, 3))
            {
                const auto newShape = static_cast<world::FloorShape>(shapeIdx);
                if (newShape != sector.floorShape)
                {
                    std::optional<world::StairProfile> newProfile;
                    if (newShape == world::FloorShape::VisualStairs)
                        newProfile = world::StairProfile{};
                    doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
                        doc, sid, newShape, newProfile));
                }
            }

            if (sector.floorShape == world::FloorShape::VisualStairs &&
                sector.stairProfile.has_value())
            {
                world::StairProfile profile = *sector.stairProfile;
                ImGui::Spacing();
                ImGui::TextDisabled("Stair Profile");

                int steps = static_cast<int>(profile.stepCount);
                ImGui::SetNextItemWidth(-1.0f);
                if (dragIntUndo("##stairsteps", &steps, 0.5f, 1, 64, "Steps: %d") &&
                    static_cast<unsigned>(steps) != profile.stepCount)
                {
                    world::StairProfile np = profile;
                    np.stepCount = static_cast<unsigned>(steps);
                    doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
                        doc, sid, world::FloorShape::VisualStairs, np));
                }

                float riser = profile.riserHeight;
                ImGui::SetNextItemWidth(-1.0f);
                if (dragFloatUndo("##stairriser", &riser, 0.01f, 0.01f, 4.0f, "Riser: %.2f m") &&
                    riser != profile.riserHeight)
                {
                    world::StairProfile np = profile;
                    np.riserHeight = riser;
                    doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
                        doc, sid, world::FloorShape::VisualStairs, np));
                }

                float tread = profile.treadDepth;
                ImGui::SetNextItemWidth(-1.0f);
                if (dragFloatUndo("##stairtread", &tread, 0.01f, 0.01f, 8.0f, "Tread: %.2f m") &&
                    tread != profile.treadDepth)
                {
                    world::StairProfile np = profile;
                    np.treadDepth = tread;
                    doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
                        doc, sid, world::FloorShape::VisualStairs, np));
                }

                constexpr float kRad2Deg = 180.0f / glm::pi<float>();
                constexpr float kDeg2Rad = glm::pi<float>() / 180.0f;
                float angleDeg = profile.directionAngle * kRad2Deg;
                ImGui::SetNextItemWidth(-1.0f);
                if (dragFloatUndo("##stairangle", &angleDeg, 1.0f, 0.0f, 0.0f,
                                  "Direction: %.1f\xc2\xb0"))
                {
                    world::StairProfile np = profile;
                    np.directionAngle = angleDeg * kDeg2Rad;
                    doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
                        doc, sid, world::FloorShape::VisualStairs, np));
                }
            }

            if (sector.floorShape == world::FloorShape::Heightfield)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Heightfield Grid");

                if (!sector.heightfield.has_value())
                {
                    ImGui::TextDisabled("(no heightfield data)");
                    if (ImGui::Button("Create 8\xc3\x97" "8 Heightfield"))
                    {
                        world::HeightfieldFloor hf;
                        hf.gridWidth = 8u;
                        hf.gridDepth = 8u;
                        // Bound the grid to the sector polygon's axis-aligned bounding box.
                        glm::vec2 mn{1e9f, 1e9f}, mx{-1e9f, -1e9f};
                        for (const auto& w : sector.walls)
                        {
                            mn.x = std::min(mn.x, w.p0.x); mn.y = std::min(mn.y, w.p0.y);
                            mx.x = std::max(mx.x, w.p0.x); mx.y = std::max(mx.y, w.p0.y);
                        }
                        hf.worldMin = mn;
                        hf.worldMax = mx;
                        hf.samples.assign(hf.gridWidth * hf.gridDepth, sector.floorHeight);
                        doc.pushCommand(std::make_unique<CmdSetHeightfield>(doc, sid, hf));
                    }
                }
                else
                {
                    const world::HeightfieldFloor& hf = *sector.heightfield;
                    ImGui::Text("Grid: %u \xc3\x97 %u  (%zu samples)",
                                hf.gridWidth, hf.gridDepth, hf.samples.size());

                    int gw = static_cast<int>(hf.gridWidth);
                    int gd = static_cast<int>(hf.gridDepth);
                    bool gwChg = false, gdChg = false;
                    ImGui::SetNextItemWidth(-1.0f);
                    gwChg = dragIntUndo("##hfgw", &gw, 0.5f, 2, 256, "Width: %d");
                    ImGui::SetNextItemWidth(-1.0f);
                    gdChg = dragIntUndo("##hfgd", &gd, 0.5f, 2, 256, "Depth: %d");
                    if ((gwChg || gdChg) &&
                        (static_cast<uint32_t>(gw) != hf.gridWidth ||
                         static_cast<uint32_t>(gd) != hf.gridDepth))
                    {
                        world::HeightfieldFloor nhf = hf;
                        nhf.gridWidth = static_cast<uint32_t>(gw);
                        nhf.gridDepth = static_cast<uint32_t>(gd);
                        nhf.samples.assign(nhf.gridWidth * nhf.gridDepth, sector.floorHeight);
                        doc.pushCommand(std::make_unique<CmdSetHeightfield>(doc, sid, nhf));
                    }

                    if (ImGui::Button("Clear Heightfield"))
                        doc.pushCommand(std::make_unique<CmdSetHeightfield>(
                            doc, sid, std::nullopt));
                }
            }
        }

        // ── Floor / Ceiling Portals ─────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Floor / Ceiling Portals");
        {
            // Helper: draw linked/unlinked state and an ID drag field for one portal.
            const auto editHPortal = [&](const char* label,
                                         HPortalSurface  surf,
                                         world::SectorId curTarget)
            {
                ImGui::PushID(label);
                const bool linked = (curTarget != world::INVALID_SECTOR_ID);
                if (linked)
                    ImGui::Text("%s \xe2\x86\x92 Sector %u", label,
                                static_cast<unsigned>(curTarget));
                else
                    ImGui::TextDisabled("%s: (none)", label);

                // Integer drag: -1 means no link, >=0 is a sector index.
                const int maxSec = static_cast<int>(doc.mapData().sectors.size()) - 1;
                int tgtInt = linked ? static_cast<int>(curTarget) : -1;
                ImGui::SetNextItemWidth(-1.0f);
                // Format: -1 displays as "ID: -1" meaning "no portal"; >=0 is the target sector index.
                if (dragIntUndo("##hptgt", &tgtInt, 0.5f, -1, std::max(maxSec, 0), "ID: %d"))
                {
                    const world::SectorId newTgt = (tgtInt < 0)
                        ? world::INVALID_SECTOR_ID
                        : static_cast<world::SectorId>(tgtInt);
                    if (newTgt != curTarget)
                        doc.pushCommand(std::make_unique<CmdSetFloorPortal>(
                            doc, sid, surf, newTgt, UUID{}));
                }
                ImGui::PopID();
            };

            editHPortal("Floor portal",   HPortalSurface::Floor,
                        sector.floorPortalSectorId);
            ImGui::Spacing();
            editHPortal("Ceiling portal", HPortalSurface::Ceiling,
                        sector.ceilPortalSectorId);
        }

        // ── Wall list ──────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Walls");

        for (std::size_t wi = 0; wi < sector.walls.size(); ++wi)
        {
            const world::Wall& wall = sector.walls[wi];
            ImGui::PushID(static_cast<int>(wi));

            if (ImGui::TreeNode("Wall", "Wall %zu  (%.1f, %.1f)",
                                wi, wall.p0.x, wall.p0.y))
            {
                auto toggleWF = [&](const char* lbl, world::WallFlags bit)
                {
                    bool v = hasFlag(wall.flags, bit);
                    if (ImGui::Checkbox(lbl, &v))
                    {
                        const world::WallFlags nf = v
                            ? (wall.flags | bit)
                            : static_cast<world::WallFlags>(
                                  static_cast<unsigned>(wall.flags) &
                                  ~static_cast<unsigned>(bit));
                        doc.pushCommand(std::make_unique<CmdSetWallFlags>(
                            doc, sid, wi, nf));
                    }
                };
                toggleWF("Blocking",    world::WallFlags::Blocking);
                toggleWF("Two-sided",   world::WallFlags::TwoSided);
                toggleWF("Climbable",   world::WallFlags::Climbable);
                toggleWF("Trigger Zone",world::WallFlags::TriggerZone);
                toggleWF("Invisible",   world::WallFlags::Invisible);
                toggleWF("Mirror",      world::WallFlags::Mirror);
                toggleWF("No Physics",  world::WallFlags::NoPhysics);

                ImGui::TreePop();
            }

            ImGui::PopID();
        }
        ImGui::PopID();  // end sector PushID
    }
    else if (sel.hasSingleOf(SelectionType::Wall))
    {
        const world::SectorId sid = sel.items[0].sectorId;
        const std::size_t     wi  = sel.items[0].index;
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
            auto toggleWF = [&](const char* lbl, world::WallFlags bit)
            {
                bool v = hasFlag(wall.flags, bit);
                if (ImGui::Checkbox(lbl, &v))
                {
                    const world::WallFlags nf = v
                        ? (wall.flags | bit)
                        : static_cast<world::WallFlags>(
                              static_cast<unsigned>(wall.flags) &
                              ~static_cast<unsigned>(bit));
                    doc.pushCommand(std::make_unique<CmdSetWallFlags>(doc, sid, wi, nf));
                }
            };
            toggleWF("Blocking",    world::WallFlags::Blocking);
            toggleWF("Two-sided",   world::WallFlags::TwoSided);
            toggleWF("Climbable",   world::WallFlags::Climbable);
            toggleWF("Trigger Zone",world::WallFlags::TriggerZone);
            toggleWF("Invisible",   world::WallFlags::Invisible);
            toggleWF("Mirror",      world::WallFlags::Mirror);
            toggleWF("No Physics",  world::WallFlags::NoPhysics);
        }

        ImGui::Spacing();
        ImGui::SeparatorText("UV Mapping");
        {
            // Statics to capture pre-drag UV values so the undo command
            // constructor reads the correct original state even after live
            // mutations have already been written to the wall.
            // markDirty() triggers retessellation so the 3D viewport
            // reflects UV changes in real time during drag.
            static glm::vec2 s_origUvOff = {};
            static glm::vec2 s_origUvSc  = {};
            static float     s_origUvRot = 0.0f;  // radians

            // Offset — DragFloat2 uses the format string per-component, so a
            // single-label format like "Offset (%.3f, %.3f)" produces UB
            // (second %f reads garbage).  Use TextDisabled for the row header
            // and a plain "%.3f" format so both X and Y display correctly.
            ImGui::TextDisabled("Offset");
            glm::vec2 uvOff = wall.uvOffset;
            ImGui::SetNextItemWidth(-1.0f);
            bool uvOffCommitted = dragFloat2Undo("##uvoff", &uvOff.x, 0.01f, 0.0f, 0.0f, "%.3f");
            if (ImGui::IsItemActivated())
                s_origUvOff = wall.uvOffset;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                { wall.uvOffset = uvOff; doc.markDirty(); }
            if (uvOffCommitted)
            {
                // Reset to original so CmdSetWallUV captures the correct
                // old value in its constructor; execute() re-applies the final.
                const glm::vec2 finalOff = wall.uvOffset;
                wall.uvOffset = s_origUvOff;
                doc.pushCommand(std::make_unique<CmdSetWallUV>(
                    doc, sid, wi, finalOff, wall.uvScale, wall.uvRotation));
            }

            ImGui::TextDisabled("Scale");
            glm::vec2 uvSc = wall.uvScale;
            ImGui::SetNextItemWidth(-1.0f);
            bool uvScCommitted = dragFloat2Undo("##uvsc", &uvSc.x, 0.01f, 0.0f, 0.0f, "%.3f");
            if (ImGui::IsItemActivated())
                s_origUvSc = wall.uvScale;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                { wall.uvScale = uvSc; doc.markDirty(); }
            if (uvScCommitted)
            {
                const glm::vec2 finalSc = wall.uvScale;
                wall.uvScale = s_origUvSc;
                doc.pushCommand(std::make_unique<CmdSetWallUV>(
                    doc, sid, wi, wall.uvOffset, finalSc, wall.uvRotation));
            }

            // Display rotation in degrees; store internally as radians.
            constexpr float kRad2Deg = 180.0f / glm::pi<float>();
            constexpr float kDeg2Rad = glm::pi<float>() / 180.0f;
            float uvRotDeg = wall.uvRotation * kRad2Deg;
            ImGui::SetNextItemWidth(-1.0f);
            bool uvRotCommitted = dragFloatUndo("##uvrot", &uvRotDeg, 0.5f, 0.0f, 0.0f,
                                               "Rotation: %.1f\xc2\xb0");
            if (ImGui::IsItemActivated())
                s_origUvRot = wall.uvRotation;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                { wall.uvRotation = uvRotDeg * kDeg2Rad; doc.markDirty(); }
            if (uvRotCommitted)
            {
                const float finalRot = wall.uvRotation;
                wall.uvRotation = s_origUvRot;
                doc.pushCommand(std::make_unique<CmdSetWallUV>(
                    doc, sid, wi, wall.uvOffset, wall.uvScale, finalRot));
            }

            // ── UV action buttons ──────────────────────────────────────────────────────
            ImGui::Spacing();

            // Pre-compute wall geometry (needed by several buttons below).
            const glm::vec2 uvP0     = wall.p0;
            const glm::vec2 uvP1     = sectors[sid].walls[(wi + 1) % n].p0;
            const float     uvWallLen = glm::length(uvP1 - uvP0);
            const float     uvWallHt  = sectors[sid].ceilHeight - sectors[sid].floorHeight;

            // Pixel Perfect (N in 3D viewport): scale to 1:1 pixel density using front material dims.
            {
                const MaterialEntry* ppEntry = catalog.find(wall.frontMaterialId);
                const bool hasDims = ppEntry && ppEntry->texWidth > 0 && ppEntry->texHeight > 0;
                if (!hasDims) ImGui::BeginDisabled();
                if (ImGui::SmallButton("Pixel Perfect##uv") && hasDims)
                    doc.pushCommand(std::make_unique<CmdSetWallUV>(
                        doc, sid, wi, wall.uvOffset,
                        computePixelPerfectUVScale(ppEntry->texWidth, ppEntry->texHeight),
                        wall.uvRotation));
                if (!hasDims) ImGui::EndDisabled();
            }

            ImGui::SameLine();

            // Fit Wall: stretch texture to fill wall exactly once.
            if (ImGui::SmallButton("Fit Wall##uv"))
                doc.pushCommand(std::make_unique<CmdSetWallUV>(
                    doc, sid, wi,
                    glm::vec2{0.0f, 0.0f},
                    glm::vec2{uvWallLen, uvWallHt},
                    0.0f));

            ImGui::SameLine();

            // Square: set V scale equal to U scale.
            if (ImGui::SmallButton("Square##uv"))
                doc.pushCommand(std::make_unique<CmdSetWallUV>(
                    doc, sid, wi, wall.uvOffset,
                    glm::vec2{wall.uvScale.x, wall.uvScale.x},
                    wall.uvRotation));

            // Align to left (previous) wall.
            if (ImGui::SmallButton("\xe2\x86\x90 Align##uv"))
            {
                const std::size_t prevWi = (wi + n - 1) % n;
                const world::Wall& prevW  = sectors[sid].walls[prevWi];
                const float prevLen =
                    glm::length(sectors[sid].walls[wi].p0 - prevW.p0);
                const float prevScaleX =
                    (prevW.uvScale.x > 1e-6f) ? prevW.uvScale.x : 1.0f;
                doc.pushCommand(std::make_unique<CmdSetWallUV>(
                    doc, sid, wi,
                    glm::vec2{prevW.uvOffset.x + prevLen / prevScaleX, prevW.uvOffset.y},
                    wall.uvScale, wall.uvRotation));
            }

            ImGui::SameLine();

            // Align to right (next) wall.
            if (ImGui::SmallButton("Align \xe2\x86\x92##uv"))
            {
                const std::size_t nextWi = (wi + 1) % n;
                const world::Wall& nextW  = sectors[sid].walls[nextWi];
                const float curScaleX =
                    (wall.uvScale.x > 1e-6f) ? wall.uvScale.x : 1.0f;
                doc.pushCommand(std::make_unique<CmdSetWallUV>(
                    doc, sid, wi,
                    glm::vec2{nextW.uvOffset.x - uvWallLen / curScaleX, nextW.uvOffset.y},
                    wall.uvScale, wall.uvRotation));
            }

            // Texel density readout.
            {
                const MaterialEntry* densEntry = catalog.find(wall.frontMaterialId);
                if (densEntry && densEntry->texWidth > 0 && densEntry->texHeight > 0)
                {
                    const float scX = (wall.uvScale.x > 1e-6f) ? wall.uvScale.x : 1e-6f;
                    const float scY = (wall.uvScale.y > 1e-6f) ? wall.uvScale.y : 1e-6f;
                    const float densU = static_cast<float>(densEntry->texWidth)  / scX;
                    const float densV = static_cast<float>(densEntry->texHeight) / scY;
                    ImGui::TextDisabled("%.0f px/u  %.0f px/v", densU, densV);
                }
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Materials");
        {
            // Shared helper: thumbnail preview + Browse + clear row for a wall material.
            auto editWallMat = [&](const char* label, const daedalus::UUID& current,
                                   WallSurface surface)
            {
                ImGui::PushID(label);
                // Thumbnail (32x32).
                rhi::ITexture* thumb = current.isValid()
                    ? catalog.getOrLoadThumbnail(current, device) : nullptr;
                if (thumb)
                    ImGui::Image(reinterpret_cast<ImTextureID>(thumb->nativeHandle()), ImVec2(32, 32));
                else
                    ImGui::Dummy(ImVec2(32, 32));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(label);
                if (current.isValid())
                {
                    const MaterialEntry* entry = catalog.find(current);
                    ImGui::TextDisabled("%s", entry ? entry->displayName.c_str() : "(unknown)");
                }
                else
                    ImGui::TextDisabled("(none)");
                if (ImGui::SmallButton("Browse##wm"))
                {
                    const char* surfLbl =
                        (surface == WallSurface::Front) ? "Wall Front" :
                        (surface == WallSurface::Upper) ? "Wall Upper" :
                        (surface == WallSurface::Lower) ? "Wall Lower" : "Wall Back";
                    // Auto-fit UV on assignment: compute pixel-perfect scale from the
                    // texture's native dimensions, falling back to stretch-to-fit.
                    assetBrowser.openPicker([&doc, &catalog, sid, wi, surface](const UUID& uuid)
                    {
                        const auto& sectors = doc.mapData().sectors;
                        if (sid >= sectors.size()) return;
                        const auto& sec = sectors[sid];
                        if (wi >= sec.walls.size()) return;
                        const glm::vec2 p0  = sec.walls[wi].p0;
                        const glm::vec2 p1  = sec.walls[(wi + 1) % sec.walls.size()].p0;
                        const float wallLen = glm::length(p1 - p0);
                        const float wallHt  = sec.ceilHeight - sec.floorHeight;

                        glm::vec2 newScale{wallLen, wallHt};
                        const MaterialEntry* entry = catalog.find(uuid);
                        if (entry && entry->texWidth > 0 && entry->texHeight > 0)
                            newScale = computePixelPerfectUVScale(entry->texWidth, entry->texHeight);

                        std::vector<std::unique_ptr<ICommand>> steps;
                        steps.push_back(std::make_unique<CmdSetWallMaterial>(doc, sid, wi, surface, uuid));
                        steps.push_back(std::make_unique<CmdSetWallUV>(
                            doc, sid, wi, glm::vec2{0.0f, 0.0f}, newScale, 0.0f));
                        doc.pushCommand(std::make_unique<CompoundCommand>(
                            "Apply Wall Material", std::move(steps)));
                    }, surfLbl);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X##wm") && current.isValid())
                    doc.pushCommand(std::make_unique<CmdSetWallMaterial>(doc, sid, wi, surface, daedalus::UUID{}));
                ImGui::EndGroup();
                ImGui::PopID();
            };
            editWallMat("Front##wm", wall.frontMaterialId, WallSurface::Front);
            ImGui::Spacing();
            editWallMat("Upper##wm", wall.upperMaterialId, WallSurface::Upper);
            ImGui::Spacing();
            editWallMat("Lower##wm", wall.lowerMaterialId, WallSurface::Lower);
            ImGui::Spacing();
            editWallMat("Back##wm",  wall.backMaterialId,  WallSurface::Back);
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
                        wi);

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

        // ── Bezier Curve ──────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Bezier Curve");
        {
            const bool hasCurve = wall.curveControlA.has_value();
            bool curveEnabled = hasCurve;
            if (ImGui::Checkbox("Enable Curve##cv", &curveEnabled))
            {
                if (curveEnabled)
                {
                    // Default control point at the wall midpoint.
                    const glm::vec2 mid =
                        (wall.p0 + sectors[sid].walls[(wi + 1) % n].p0) * 0.5f;
                    doc.pushCommand(std::make_unique<CmdSetWallCurve>(
                        doc, sid, wi, mid, std::nullopt, wall.curveSubdivisions));
                }
                else
                {
                    doc.pushCommand(std::make_unique<CmdSetWallCurve>(
                        doc, sid, wi, std::nullopt, std::nullopt, wall.curveSubdivisions));
                }
            }

            if (hasCurve)
            {
                // ── Control point A ───────────────────────────────────────────────
                glm::vec2 cpA = *wall.curveControlA;
                static glm::vec2 s_origCpA = {};
                ImGui::TextDisabled("Control A (X, Z)");
                ImGui::SetNextItemWidth(-1.0f);
                const bool cpACommitted = dragFloat2Undo("##cpa", &cpA.x, 0.05f, 0.0f, 0.0f, "%.3f");
                if (ImGui::IsItemActivated()) s_origCpA = *wall.curveControlA;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                    { wall.curveControlA = cpA; doc.markDirty(); }
                if (cpACommitted && cpA != s_origCpA)
                {
                    const glm::vec2 finalCpA = *wall.curveControlA;
                    wall.curveControlA = s_origCpA;
                    doc.pushCommand(std::make_unique<CmdSetWallCurve>(
                        doc, sid, wi, finalCpA, wall.curveControlB, wall.curveSubdivisions));
                }

                // ── Optional control point B (cubic Bezier) ───────────────────────
                const bool hasCpB = wall.curveControlB.has_value();
                bool cpBEnabled = hasCpB;
                if (ImGui::Checkbox("Cubic (add B)##cpb", &cpBEnabled))
                {
                    if (cpBEnabled)
                    {
                        // Place B perpendicular to the wall from A's position so it is
                        // immediately visible and distinct from A — not overlapping it.
                        const glm::vec2 p0    = wall.p0;
                        const glm::vec2 p1    = sectors[sid].walls[(wi + 1) % n].p0;
                        const glm::vec2 wallV = p1 - p0;
                        const float     wLen  = glm::length(wallV);
                        const glm::vec2 wallDir = wLen > 1e-6f ? wallV / wLen : glm::vec2{1.0f, 0.0f};
                        // Perpendicular direction (rotate 90° CCW).
                        const glm::vec2 perp  = {-wallDir.y, wallDir.x};
                        // Offset B to the opposite side from A relative to the midpoint.
                        const glm::vec2 mid   = (p0 + p1) * 0.5f;
                        const glm::vec2 aPos  = wall.curveControlA.value_or(mid);
                        const float     aSign = glm::dot(aPos - mid, perp);
                        // Place B on the opposite perpendicular side from A.
                        const float     bDist = std::max(wLen * 0.25f, 0.5f);
                        const glm::vec2 bPos  = mid + perp * (aSign >= 0.0f ? -bDist : bDist);
                        doc.pushCommand(std::make_unique<CmdSetWallCurve>(
                            doc, sid, wi, wall.curveControlA, bPos, wall.curveSubdivisions));
                    }
                    else
                    {
                        doc.pushCommand(std::make_unique<CmdSetWallCurve>(
                            doc, sid, wi, wall.curveControlA, std::nullopt,
                            wall.curveSubdivisions));
                    }
                }

                if (hasCpB)
                {
                    glm::vec2 cpB = *wall.curveControlB;
                    static glm::vec2 s_origCpB = {};
                    ImGui::TextDisabled("Control B (X, Z)");
                    ImGui::SetNextItemWidth(-1.0f);
                    const bool cpBCommitted = dragFloat2Undo("##cpb2", &cpB.x, 0.05f, 0.0f, 0.0f, "%.3f");
                    if (ImGui::IsItemActivated()) s_origCpB = *wall.curveControlB;
                    if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                        { wall.curveControlB = cpB; doc.markDirty(); }
                    if (cpBCommitted && cpB != s_origCpB)
                    {
                        const glm::vec2 finalCpB = *wall.curveControlB;
                        wall.curveControlB = s_origCpB;
                        doc.pushCommand(std::make_unique<CmdSetWallCurve>(
                            doc, sid, wi, wall.curveControlA, finalCpB, wall.curveSubdivisions));
                    }
                }

                // ── Subdivisions (live-update for real-time 3D feedback) ──────────────
                int subdivs = static_cast<int>(wall.curveSubdivisions);
                static int s_origSubdivs = 12;
                ImGui::SetNextItemWidth(-1.0f);
                const bool subdivCommitted = dragIntUndo("##curvesub", &subdivs, 0.5f, 4, 64, "Subdivisions: %d");
                if (ImGui::IsItemActivated())
                    s_origSubdivs = static_cast<int>(wall.curveSubdivisions);
                if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                {
                    wall.curveSubdivisions = static_cast<uint32_t>(std::clamp(subdivs, 4, 64));
                    doc.markDirty();
                }
                if (subdivCommitted && subdivs != s_origSubdivs)
                {
                    const uint32_t finalSubdivs = wall.curveSubdivisions;
                    // Restore original so CmdSetWallCurve captures the correct old value.
                    wall.curveSubdivisions = static_cast<uint32_t>(s_origSubdivs);
                    doc.pushCommand(std::make_unique<CmdSetWallCurve>(
                        doc, sid, wi, wall.curveControlA, wall.curveControlB, finalSubdivs));
                }
            }
        }
    }
    else if (sel.hasSingleOf(SelectionType::Floor) || sel.hasSingleOf(SelectionType::Ceil))
    {
        // ── Floor / Ceiling surface selected in the 3D viewport ────────────────────
        // Shows the material and full UV controls for the specific clicked surface,
        // matching the wall UV workflow (Offset, Scale, Rotation + action buttons).
        const bool         isFloor = sel.hasSingleOf(SelectionType::Floor);
        const world::SectorId sid  = sel.items[0].sectorId;
        const SectorSurface   surf  = isFloor ? SectorSurface::Floor : SectorSurface::Ceil;
        auto& sectors = doc.mapData().sectors;

        if (sid >= sectors.size())
        {
            ImGui::TextDisabled("(invalid selection)");
            ImGui::End();
            return;
        }

        world::Sector& sector = sectors[sid];
        ImGui::PushID(static_cast<int>(sid));

        ImGui::SeparatorText(isFloor ? "Floor" : "Ceiling");
        ImGui::Text("Sector %u", static_cast<unsigned>(sid));
        {
            const float h = isFloor ? sector.floorHeight : sector.ceilHeight;
            ImGui::TextDisabled("%s height: %.2f m", isFloor ? "Floor" : "Ceiling", h);
        }

        // ── Material ────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Material");
        {
            const daedalus::UUID& matId = isFloor ? sector.floorMaterialId : sector.ceilMaterialId;
            rhi::ITexture* thumb = matId.isValid()
                ? catalog.getOrLoadThumbnail(matId, device) : nullptr;
            if (thumb) ImGui::Image(reinterpret_cast<ImTextureID>(thumb->nativeHandle()), ImVec2(32, 32));
            else       ImGui::Dummy(ImVec2(32, 32));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(isFloor ? "Floor" : "Ceiling");
            if (matId.isValid())
            {
                const MaterialEntry* entry = catalog.find(matId);
                ImGui::TextDisabled("%s", entry ? entry->displayName.c_str() : "(unknown)");
            }
            else
                ImGui::TextDisabled("(none)");
            if (ImGui::SmallButton("Browse##sm"))
            {
                const SectorSurface capSurf = surf;
                const char* surfLbl = isFloor ? "Floor" : "Ceiling";
                assetBrowser.openPicker([&doc, sid, capSurf](const UUID& uuid) {
                    // Auto pixel-perfect UV when assigning material.
                    doc.pushCommand(std::make_unique<CmdSetSectorMaterial>(
                        doc, sid, capSurf, uuid));
                }, surfLbl);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X##sm") && matId.isValid())
                doc.pushCommand(std::make_unique<CmdSetSectorMaterial>(
                    doc, sid, surf, daedalus::UUID{}));
            ImGui::EndGroup();
        }

        // ── UV Mapping ────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("UV Mapping");
        {
            glm::vec2& uvOff = isFloor ? sector.floorUvOffset : sector.ceilUvOffset;
            glm::vec2& uvSc  = isFloor ? sector.floorUvScale  : sector.ceilUvScale;
            float&     uvRot = isFloor ? sector.floorUvRotation : sector.ceilUvRotation;

            static glm::vec2 s_surfOrigOff = {};
            static glm::vec2 s_surfOrigSc  = {};
            static float     s_surfOrigRot = 0.0f;

            constexpr float kRad2Deg = 180.0f / glm::pi<float>();
            constexpr float kDeg2Rad = glm::pi<float>() / 180.0f;

            ImGui::TextDisabled("Offset");
            glm::vec2 offEdit = uvOff;
            ImGui::SetNextItemWidth(-1.0f);
            bool offCommitted = dragFloat2Undo("##sfuvoff", &offEdit.x, 0.01f, 0.0f, 0.0f, "%.3f");
            if (ImGui::IsItemActivated())              s_surfOrigOff = uvOff;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                { uvOff = offEdit; doc.markDirty(); }
            if (offCommitted)
            {
                const glm::vec2 finalOff = uvOff;
                uvOff = s_surfOrigOff;
                doc.pushCommand(std::make_unique<CmdSetSectorSurfaceUV>(
                    doc, sid, surf, finalOff, uvSc, uvRot));
            }

            ImGui::TextDisabled("Scale");
            glm::vec2 scEdit = uvSc;
            ImGui::SetNextItemWidth(-1.0f);
            bool scCommitted = dragFloat2Undo("##sfuvsc", &scEdit.x, 0.01f, 0.0f, 0.0f, "%.3f");
            if (ImGui::IsItemActivated())              s_surfOrigSc = uvSc;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                { uvSc = scEdit; doc.markDirty(); }
            if (scCommitted)
            {
                const glm::vec2 finalSc = uvSc;
                uvSc = s_surfOrigSc;
                doc.pushCommand(std::make_unique<CmdSetSectorSurfaceUV>(
                    doc, sid, surf, uvOff, finalSc, uvRot));
            }

            float uvRotDeg = uvRot * kRad2Deg;
            ImGui::SetNextItemWidth(-1.0f);
            bool rotCommitted = dragFloatUndo("##sfuvrot", &uvRotDeg, 0.5f, 0.0f, 0.0f,
                                             "Rotation: %.1f\xc2\xb0");
            if (ImGui::IsItemActivated())              s_surfOrigRot = uvRot;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                { uvRot = uvRotDeg * kDeg2Rad; doc.markDirty(); }
            if (rotCommitted)
            {
                const float finalRot = uvRot;
                uvRot = s_surfOrigRot;
                doc.pushCommand(std::make_unique<CmdSetSectorSurfaceUV>(
                    doc, sid, surf, uvOff, uvSc, finalRot));
            }

            // ── UV action buttons ────────────────────────────────────────────
            ImGui::Spacing();
            const daedalus::UUID& matIdForUV =
                isFloor ? sector.floorMaterialId : sector.ceilMaterialId;

            // Pixel Perfect: scale to 1:1 pixel density from the material texture.
            {
                const MaterialEntry* ppEntry = catalog.find(matIdForUV);
                const bool hasDims = ppEntry && ppEntry->texWidth > 0 && ppEntry->texHeight > 0;
                if (!hasDims) ImGui::BeginDisabled();
                if (ImGui::SmallButton("Pixel Perfect##sfuv") && hasDims)
                    doc.pushCommand(std::make_unique<CmdSetSectorSurfaceUV>(
                        doc, sid, surf, uvOff,
                        computePixelPerfectUVScale(ppEntry->texWidth, ppEntry->texHeight),
                        uvRot));
                if (!hasDims) ImGui::EndDisabled();
            }

            ImGui::SameLine();

            // Fit Surface: stretch to cover the sector's XZ bounding box once.
            if (ImGui::SmallButton("Fit Surface##sfuv"))
            {
                float minX =  1e9f, maxX = -1e9f, minZ =  1e9f, maxZ = -1e9f;
                for (const auto& w : sector.walls)
                {
                    minX = std::min(minX, w.p0.x); maxX = std::max(maxX, w.p0.x);
                    minZ = std::min(minZ, w.p0.y); maxZ = std::max(maxZ, w.p0.y);
                }
                const float fitX = (maxX > minX) ? (maxX - minX) : 1.0f;
                const float fitZ = (maxZ > minZ) ? (maxZ - minZ) : 1.0f;
                doc.pushCommand(std::make_unique<CmdSetSectorSurfaceUV>(
                    doc, sid, surf, glm::vec2{0.0f, 0.0f},
                    glm::vec2{fitX, fitZ}, 0.0f));
            }

            ImGui::SameLine();

            // Square: set V scale equal to U scale.
            if (ImGui::SmallButton("Square##sfuv"))
                doc.pushCommand(std::make_unique<CmdSetSectorSurfaceUV>(
                    doc, sid, surf, uvOff,
                    glm::vec2{uvSc.x, uvSc.x}, uvRot));

            // Texel density readout.
            {
                const MaterialEntry* densEntry = catalog.find(matIdForUV);
                if (densEntry && densEntry->texWidth > 0 && densEntry->texHeight > 0)
                {
                    const float scX = (uvSc.x > 1e-6f) ? uvSc.x : 1e-6f;
                    const float scY = (uvSc.y > 1e-6f) ? uvSc.y : 1e-6f;
                    ImGui::TextDisabled("%.0f px/u  %.0f px/v",
                        static_cast<float>(densEntry->texWidth)  / scX,
                        static_cast<float>(densEntry->texHeight) / scY);
                }
            }
        }

        ImGui::PopID();
    }
    else if (sel.uniformType() == SelectionType::Vertex && !sel.items.empty())
    {
        auto& sectors = doc.mapData().sectors;

        // Multi-vertex header: show count when more than one vertex is selected.
        if (sel.items.size() > 1)
        {
            ImGui::SeparatorText("Vertices");
            ImGui::Text("%zu vertices selected", sel.items.size());
            ImGui::Spacing();
            ImGui::TextDisabled("Shift+click to add/remove. Drag any selected vertex to move all.");
            ImGui::End();
            return;
        }

        const world::SectorId sid = sel.items[0].sectorId;
        const std::size_t     wi  = sel.items[0].index;

        if (sid < sectors.size() && wi < sectors[sid].walls.size())
        {
            const glm::vec2 p = sectors[sid].walls[wi].p0;
            ImGui::SeparatorText("Vertex");
            ImGui::Text("Sector %u  Wall %zu", static_cast<unsigned>(sid), wi);
            ImGui::Text("Position: (%.3f, %.3f)", p.x, p.y);
            ImGui::Spacing();
            ImGui::TextDisabled("Drag in 2D viewport to move.");

            // ── Height overrides ──────────────────────────────────────────────────
            ImGui::Spacing();
            ImGui::SeparatorText("Height Overrides");
            {
                world::Wall& vwall = sectors[sid].walls[wi];

                // Floor override.
                bool hasFloorOvr = vwall.floorHeightOverride.has_value();
                if (ImGui::Checkbox("Floor##vhof", &hasFloorOvr))
                {
                    const std::optional<float> nf = hasFloorOvr
                        ? std::optional<float>(sectors[sid].floorHeight)
                        : std::nullopt;
                    doc.pushCommand(std::make_unique<CmdSetVertexHeight>(
                        doc, sid, wi, nf, vwall.ceilHeightOverride));
                }
                if (vwall.floorHeightOverride.has_value())
                {
                    float fh = *vwall.floorHeightOverride;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (dragFloatUndo("##vfloor", &fh, 0.05f, 0.0f, 0.0f, "Floor: %.2f") &&
                        fh != *vwall.floorHeightOverride)
                        doc.pushCommand(std::make_unique<CmdSetVertexHeight>(
                            doc, sid, wi, fh, vwall.ceilHeightOverride));
                }

                // Ceiling override.
                bool hasCeilOvr = vwall.ceilHeightOverride.has_value();
                if (ImGui::Checkbox("Ceiling##vhoc", &hasCeilOvr))
                {
                    const std::optional<float> nc = hasCeilOvr
                        ? std::optional<float>(sectors[sid].ceilHeight)
                        : std::nullopt;
                    doc.pushCommand(std::make_unique<CmdSetVertexHeight>(
                        doc, sid, wi, vwall.floorHeightOverride, nc));
                }
                if (vwall.ceilHeightOverride.has_value())
                {
                    float ch = *vwall.ceilHeightOverride;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (dragFloatUndo("##vceil", &ch, 0.05f, 0.0f, 0.0f, "Ceiling: %.2f") &&
                        ch != *vwall.ceilHeightOverride)
                        doc.pushCommand(std::make_unique<CmdSetVertexHeight>(
                            doc, sid, wi, vwall.floorHeightOverride, ch));
                }
            }
        }
    }
    else if (sel.hasSingleOf(SelectionType::Light))
    {
        const std::size_t li = sel.items[0].index;
        auto& lights = doc.lights();

        if (li >= lights.size())
        {
            ImGui::TextDisabled("(invalid light selection)");
            ImGui::End();
            return;
        }

        LightDef& ld = lights[li];
        ImGui::PushID(static_cast<int>(li));

        ImGui::SeparatorText("Light");
        ImGui::Text("Index: %zu", li);

        // Snapshot captured at the start of each drag for undo construction.
        // Viewport3D reads doc.lights() fresh every frame so live mutations
        // to ld.* appear in the viewport without any explicit dirty-flag call.
        static LightDef s_preDragLight = {};

        // ── Type ──────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Type");
        {
            const char* typeItems[] = {"Point", "Spot"};
            int typeIdx = static_cast<int>(ld.type);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##ltype", &typeIdx, typeItems, 2))
            {
                const LightType newType = static_cast<LightType>(typeIdx);
                if (newType != ld.type)
                {
                    LightDef oldDef = ld;
                    LightDef newDef = ld;
                    newDef.type = newType;
                    doc.pushCommand(std::make_unique<CmdSetLightProps>(
                        doc, li, oldDef, newDef));
                }
            }
        }

        // ── Position ─────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Position");
        {
            glm::vec3 pos = ld.position;
            ImGui::SetNextItemWidth(-1.0f);
            bool posCommitted = dragFloat3Undo("##lpos", &pos.x, 0.1f, 0.0f, 0.0f, "%.2f");
            if (ImGui::IsItemActivated())
                s_preDragLight = ld;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                ld.position = pos;
            if (posCommitted &&
                (ld.position.x != s_preDragLight.position.x ||
                 ld.position.y != s_preDragLight.position.y ||
                 ld.position.z != s_preDragLight.position.z))
            {
                doc.pushCommand(std::make_unique<CmdMoveLight>(
                    doc, li, s_preDragLight.position, ld.position));
            }
        }

        // ── Appearance ───────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Appearance");
        {
            glm::vec3 col    = ld.color;
            float     radius = ld.radius;
            float     intens = ld.intensity;

            bool colChanged = colorEditUndo("Color##light", &col.x, 3);
            if (ImGui::IsItemActivated()) s_preDragLight = ld;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ld.color = col;

            ImGui::SetNextItemWidth(-1.0f);
            bool radChanged = dragFloatUndo("##lrad", &radius, 0.1f, 0.0f, 0.0f, "Radius: %.1f m");
            if (ImGui::IsItemActivated()) s_preDragLight = ld;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ld.radius = radius;

            ImGui::SetNextItemWidth(-1.0f);
            bool intChanged = dragFloatUndo("##lint", &intens, 0.05f, 0.0f, 0.0f, "Intensity: %.2f");
            if (ImGui::IsItemActivated()) s_preDragLight = ld;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ld.intensity = intens;

            if (colChanged || radChanged || intChanged)
            {
                doc.pushCommand(std::make_unique<CmdSetLightProps>(
                    doc, li, s_preDragLight, ld));
            }
        }

        // ── Spot cone (visible when type == Spot) ──────────────────
        if (ld.type == LightType::Spot)
        {
            ImGui::Spacing();
            ImGui::SeparatorText("Spot Cone");
            {
                glm::vec3 dir      = ld.direction;
                float     innerDeg = glm::degrees(ld.innerConeAngle);
                float     outerDeg = glm::degrees(ld.outerConeAngle);
                float     range    = ld.range;

                ImGui::SetNextItemWidth(-1.0f);
                bool dirChanged = dragFloat3Undo("##ldir", &dir.x, 0.01f, 0.0f, 0.0f, "%.2f");
                if (ImGui::IsItemActivated()) s_preDragLight = ld;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                {
                    const float len = glm::length(dir);
                    ld.direction = (len > 1e-5f) ? (dir / len) : glm::vec3(0.0f, -1.0f, 0.0f);
                }

                ImGui::SetNextItemWidth(-1.0f);
                bool innerChanged = dragFloatUndo("##linnercone", &innerDeg, 0.5f, 0.0f, 0.0f,
                                                  "Inner: %.1f\xc2\xb0");
                if (ImGui::IsItemActivated()) s_preDragLight = ld;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                    ld.innerConeAngle = glm::radians(std::min(innerDeg, outerDeg));

                ImGui::SetNextItemWidth(-1.0f);
                bool outerChanged = dragFloatUndo("##loutercone", &outerDeg, 0.5f, 0.0f, 0.0f,
                                                  "Outer: %.1f\xc2\xb0");
                if (ImGui::IsItemActivated()) s_preDragLight = ld;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                    ld.outerConeAngle = glm::radians(std::max(outerDeg, innerDeg));

                ImGui::SetNextItemWidth(-1.0f);
                bool rangeChanged = dragFloatUndo("##lrange", &range, 0.1f, 0.0f, 0.0f,
                                                  "Range: %.1f m");
                if (ImGui::IsItemActivated()) s_preDragLight = ld;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ld.range = range;

                if (dirChanged || innerChanged || outerChanged || rangeChanged)
                {
                    doc.pushCommand(std::make_unique<CmdSetLightProps>(
                        doc, li, s_preDragLight, ld));
                }
            }
        }

        // ── Actions ───────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Delete Light"))
        {
            doc.pushCommand(std::make_unique<CmdDeleteLight>(doc, li));
        }
        ImGui::PopID();  // end light PushID
    }
    else if (sel.hasSingleOf(SelectionType::Entity))
    {
        const std::size_t ei = sel.items[0].index;
        auto& entities = doc.entities();

        if (ei >= entities.size())
        {
            ImGui::TextDisabled("(invalid entity selection)");
            ImGui::End();
            return;
        }

        EntityDef& ed = entities[ei];
        ImGui::PushID(static_cast<int>(ei));

        ImGui::SeparatorText("Entity");
        ImGui::Text("Index: %zu", ei);

        // ── Visual Type ───────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Visual Type");
        {
            static const char* k_typeLabels[] = {
                "Billboard (Cutout)",
                "Billboard (Blended)",
                "Billboard (Animated)",
                "Voxel Object",
                "Static Mesh",
                "Decal",
                "Particle Emitter",
                "Rotated Sprite"
            };
            int typeIdx = static_cast<int>(ed.visualType);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##etype", &typeIdx, k_typeLabels, 8))
            {
                const auto newType = static_cast<EntityVisualType>(typeIdx);
                if (newType != ed.visualType)
                {
                    EntityDef oldDef = ed;
                    EntityDef newDef = ed;
                    newDef.visualType = newType;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, oldDef, newDef));
                }
            }
        }

        // ── Transform ─────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Transform");
        {
            // Snapshot of entity state captured at the start of each drag for
            // undo-command construction.  populateSceneView reads doc.entities()
            // fresh every frame so live mutations to ed.* are reflected in the
            // 3D viewport without any explicit dirty-flag call during drag.
            static EntityDef s_preDragEntity = {};

            glm::vec3 pos = ed.position;
            ImGui::SetNextItemWidth(-1.0f);
            bool posCommitted = dragFloat3Undo("##epos", &pos.x, 0.1f, 0.0f, 0.0f, "%.2f",
                                               0, gridStep);
            if (ImGui::IsItemActivated())
                s_preDragEntity = ed;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                ed.position = pos;
            if (posCommitted &&
                (ed.position.x != s_preDragEntity.position.x ||
                 ed.position.y != s_preDragEntity.position.y ||
                 ed.position.z != s_preDragEntity.position.z))
            {
                doc.pushCommand(std::make_unique<CmdMoveEntity>(
                    doc, ei, s_preDragEntity.position, ed.position));
            }

            float yawDeg = glm::degrees(ed.yaw);
            ImGui::SetNextItemWidth(-1.0f);
            bool yawCommitted = dragFloatUndo("##eyaw", &yawDeg, 0.5f, 0.0f, 0.0f, "Yaw: %.1f\xc2\xb0",
                                              0, 5.0f);
            if (ImGui::IsItemActivated())
                s_preDragEntity = ed;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                ed.yaw = glm::radians(yawDeg);
            if (yawCommitted && ed.yaw != s_preDragEntity.yaw)
            {
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                    doc, ei, s_preDragEntity, ed));
            }

            float pitchDeg = glm::degrees(ed.pitch);
            ImGui::SetNextItemWidth(-1.0f);
            bool pitchCommitted = dragFloatUndo("##epitch", &pitchDeg, 0.5f, 0.0f, 0.0f, "Pitch: %.1f\xc2\xb0",
                                                0, 5.0f);
            if (ImGui::IsItemActivated())
                s_preDragEntity = ed;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                ed.pitch = glm::radians(pitchDeg);
            if (pitchCommitted && ed.pitch != s_preDragEntity.pitch)
            {
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                    doc, ei, s_preDragEntity, ed));
            }

            float rollDeg = glm::degrees(ed.roll);
            ImGui::SetNextItemWidth(-1.0f);
            bool rollCommitted = dragFloatUndo("##eroll", &rollDeg, 0.5f, 0.0f, 0.0f, "Roll: %.1f\xc2\xb0",
                                               0, 5.0f);
            if (ImGui::IsItemActivated())
                s_preDragEntity = ed;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                ed.roll = glm::radians(rollDeg);
            if (rollCommitted && ed.roll != s_preDragEntity.roll)
            {
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                    doc, ei, s_preDragEntity, ed));
            }

            glm::vec3 scale = ed.scale;
            ImGui::TextDisabled("Scale");
            ImGui::SetNextItemWidth(-1.0f);
            bool scaleCommitted = dragFloat3Undo("##escale", &scale.x, 0.01f, 0.0f, 0.0f, "%.3f");
            if (ImGui::IsItemActivated())
                s_preDragEntity = ed;
            if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                ed.scale = scale;
            if (scaleCommitted &&
                (ed.scale.x != s_preDragEntity.scale.x ||
                 ed.scale.y != s_preDragEntity.scale.y ||
                 ed.scale.z != s_preDragEntity.scale.z))
            {
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                    doc, ei, s_preDragEntity, ed));
            }
        }

        // ── Identity ────────────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Identity");
        {
            char nameBuf[256] = {};
            std::strncpy(nameBuf, ed.entityName.c_str(), 255);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##ename", nameBuf, sizeof(nameBuf));
            ImGui::SameLine();
            ImGui::TextDisabled("Name");
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                std::string(nameBuf) != ed.entityName)
            {
                EntityDef oldDef = ed;
                EntityDef newDef = ed;
                newDef.entityName = nameBuf;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                    doc, ei, oldDef, newDef));
            }

            static const char* k_alignLabels[] = {
                "Floor", "Ceiling", "Wall", "Free"
            };
            int alignIdx = static_cast<int>(ed.alignmentMode);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##ealign", &alignIdx, k_alignLabels, 4))
            {
                const auto newAlign = static_cast<EntityAlignment>(alignIdx);
                if (newAlign != ed.alignmentMode)
                {
                    EntityDef oldDef = ed;
                    EntityDef newDef = ed;
                    newDef.alignmentMode = newAlign;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, oldDef, newDef));
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Alignment");
        }

        // ── Appearance ──────────────────────────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Appearance");
        {
            if (ed.visualType == EntityVisualType::StaticMesh)
            {
                // Browse button + current filename display (mirroring the
                // texture picker pattern used for sector / wall materials).
                ImGui::Dummy(ImVec2(32, 32));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextDisabled("Model");
                {
                    const std::string fname =
                        std::filesystem::path(ed.assetPath).filename().string();
                    ImGui::TextDisabled("%s",
                        fname.empty() ? "(none)" : fname.c_str());
                }
                if (ImGui::SmallButton("Browse##mesh"))
                {
                    const std::size_t capEi = ei;
                    assetBrowser.openModelPicker(
                        [&doc, capEi](const std::string& path)
                        {
                            auto& ents = doc.entities();
                            if (capEi >= ents.size()) return;
                            EntityDef oldDef = ents[capEi];
                            EntityDef newDef = oldDef;
                            newDef.assetPath = path;
                            doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                                doc, capEi, oldDef, newDef));
                        }, "Static Mesh");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X##mesh") && !ed.assetPath.empty())
                {
                    EntityDef oldDef = ed;
                    EntityDef newDef = ed;
                    newDef.assetPath.clear();
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, oldDef, newDef));
                }
                ImGui::EndGroup();
            }
            else
            {
                // Browse-button UI for all non-StaticMesh entity types.
                const bool isVoxel = (ed.visualType == EntityVisualType::VoxelObject);

                ImGui::Dummy(ImVec2(32, 32));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextDisabled("%s", isVoxel ? "Voxel Model" : "Texture");
                {
                    const std::string fname =
                        std::filesystem::path(ed.assetPath).filename().string();
                    ImGui::TextDisabled("%s", fname.empty() ? "(none)" : fname.c_str());
                }

                bool showedBrowse = false;
                if (isVoxel)
                {
                    if (voxCatalog != nullptr)
                    {
                        if (ImGui::SmallButton("Browse##easset"))
                        {
                            const std::size_t capEi = ei;
                            assetBrowser.openVoxPicker(
                                [&doc, capEi](const std::string& path)
                                {
                                    auto& ents = doc.entities();
                                    if (capEi >= ents.size()) return;
                                    EntityDef oldDef = ents[capEi];
                                    EntityDef newDef = oldDef;
                                    newDef.assetPath = path;
                                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                                        doc, capEi, oldDef, newDef));
                                }, "Voxel Model");
                        }
                        showedBrowse = true;
                    }
                }
                else
                {
                    if (ImGui::SmallButton("Browse##easset"))
                    {
                        const std::size_t capEi = ei;
                        assetBrowser.openPicker(
                            [&doc, &catalog, capEi](const UUID& uuid)
                            {
                                const MaterialEntry* entry = catalog.find(uuid);
                                if (!entry) return;
                                auto& ents = doc.entities();
                                if (capEi >= ents.size()) return;
                                EntityDef oldDef = ents[capEi];
                                EntityDef newDef = oldDef;
                                newDef.assetPath = entry->absPath.string();
                                doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                                    doc, capEi, oldDef, newDef));
                            }, "Texture");
                    }
                    showedBrowse = true;
                }
                if (showedBrowse) ImGui::SameLine();
                if (ImGui::SmallButton("X##easset") && !ed.assetPath.empty())
                {
                    EntityDef oldDef = ed;
                    EntityDef newDef = ed;
                    newDef.assetPath.clear();
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, oldDef, newDef));
                }
                ImGui::EndGroup();
            }

            glm::vec4 tint = ed.tint;
            if (colorEditUndo("Tint##entity", &tint.x, 4) && tint != ed.tint)
            {
                EntityDef oldDef = ed;
                EntityDef newDef = ed;
                newDef.tint = tint;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                    doc, ei, oldDef, newDef));
            }

            const auto& layers = doc.layers();
            if (!layers.empty())
            {
                std::vector<const char*> layerNames;
                layerNames.reserve(layers.size());
                for (const auto& layer : layers)
                    layerNames.push_back(layer.name.c_str());
                int layerIdx = std::clamp(static_cast<int>(ed.layerIndex),
                                          0, static_cast<int>(layers.size()) - 1);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##elayer", &layerIdx,
                                 layerNames.data(),
                                 static_cast<int>(layerNames.size())))
                {
                    const auto newLayer = static_cast<uint32_t>(layerIdx);
                    if (newLayer != ed.layerIndex)
                    {
                        EntityDef oldDef = ed;
                        EntityDef newDef = ed;
                        newDef.layerIndex = newLayer;
                        doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                            doc, ei, oldDef, newDef));
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Layer");
            }
        }

        // ── Type-specific parameters ──────────────────────────────────────────
        if (ed.visualType == EntityVisualType::AnimatedBillboard)
        {
            ImGui::Spacing();
            ImGui::SeparatorText("Animation");
            {
                const EntityDef oldDef = ed;
                int   frameCount = static_cast<int>(ed.anim.frameCount);
                int   cols       = static_cast<int>(ed.anim.cols);
                int   rows       = static_cast<int>(ed.anim.rows);
                float frameRate  = ed.anim.frameRate;

                ImGui::SetNextItemWidth(-1.0f);
                bool fcChanged   = dragIntUndo("##efc",   &frameCount, 1.0f, 0, 0, "Frames: %d");

                ImGui::SetNextItemWidth(-1.0f);
                bool colsChanged = dragIntUndo("##ecols", &cols,       1.0f, 0, 0, "Cols: %d");

                ImGui::SetNextItemWidth(-1.0f);
                bool rowsChanged = dragIntUndo("##erows", &rows,       1.0f, 0, 0, "Rows: %d");

                ImGui::SetNextItemWidth(-1.0f);
                bool fpsChanged  = dragFloatUndo("##efps", &frameRate, 0.1f, 0.0f, 0.0f, "FPS: %.1f");

                if (fcChanged || colsChanged || rowsChanged || fpsChanged)
                {
                    EntityDef newDef       = oldDef;
                    newDef.anim.frameCount = static_cast<uint32_t>(std::max(1, frameCount));
                    newDef.anim.cols       = static_cast<uint32_t>(std::max(1, cols));
                    newDef.anim.rows       = static_cast<uint32_t>(std::max(1, rows));
                    newDef.anim.frameRate  = frameRate;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, oldDef, newDef));
                }
            }
        }
        else if (ed.visualType == EntityVisualType::Decal)
        {
            ImGui::Spacing();
            ImGui::SeparatorText("Decal Material");
            {
        const EntityDef oldDef = ed;
                float roughness = ed.decalMat.roughness;
                float metalness = ed.decalMat.metalness;
                float opacity   = ed.decalMat.opacity;
                int   zIndex    = ed.decalMat.zIndex;

                // Normal map — Browse / X buttons.
                {
                    const std::string fname =
                        std::filesystem::path(ed.decalMat.normalPath).filename().string();
                    ImGui::TextDisabled("Normal: %s",
                        fname.empty() ? "(none)" : fname.c_str());
                }
                if (ImGui::SmallButton("Browse##enorm"))
                {
                    const std::size_t capEi = ei;
                    assetBrowser.openPicker(
                        [&doc, &catalog, capEi](const UUID& uuid)
                        {
                            const MaterialEntry* entry = catalog.find(uuid);
                            if (!entry) return;
                            auto& ents = doc.entities();
                            if (capEi >= ents.size()) return;
                            EntityDef oldD = ents[capEi];
                            EntityDef newD = oldD;
                            newD.decalMat.normalPath = entry->absPath.string();
                            doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                                doc, capEi, oldD, newD));
                        }, "Normal Map");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X##enorm") && !ed.decalMat.normalPath.empty())
                {
                    EntityDef oldD = ed; EntityDef newD = ed;
                    newD.decalMat.normalPath.clear();
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldD, newD));
                }

                ImGui::Spacing();

                ImGui::SetNextItemWidth(-1.0f);
                bool roughChanged = dragFloatUndo("##erough", &roughness, 0.01f,
                                                  0.0f, 1.0f, "Roughness: %.2f");

                ImGui::SetNextItemWidth(-1.0f);
                bool metalChanged = dragFloatUndo("##emetal", &metalness, 0.01f,
                                                  0.0f, 1.0f, "Metalness: %.2f");

                ImGui::SetNextItemWidth(-1.0f);
                bool opacChanged  = dragFloatUndo("##eopac",  &opacity,   0.01f,
                                                  0.0f, 1.0f, "Opacity: %.2f");

                ImGui::SetNextItemWidth(-1.0f);
                bool zIdxChanged  = dragIntUndo("##ezidx",  &zIndex,   1.0f,
                                                0, 0, "Z-Index: %d");

                if (roughChanged || metalChanged || opacChanged || zIdxChanged)
                {
                    EntityDef newDef          = oldDef;
                    newDef.decalMat.roughness = roughness;
                    newDef.decalMat.metalness = metalness;
                    newDef.decalMat.opacity   = opacity;
                    newDef.decalMat.zIndex    = zIndex;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, oldDef, newDef));
                }
            }
        }
        else if (ed.visualType == EntityVisualType::RotatedSpriteSet)
        {
            ImGui::Spacing();
            ImGui::SeparatorText("Rotated Sprite");
            {
                const EntityDef oldDef = ed;
                int   dirCount  = static_cast<int>(ed.rotatedSprite.directionCount);
                int   animRows  = static_cast<int>(ed.rotatedSprite.animRows);
                int   animCols  = static_cast<int>(ed.rotatedSprite.animCols);
                float frameRate = ed.rotatedSprite.frameRate;

                static const char* k_dirItems[] = {"8 directions", "16 directions"};
                int dirIdx = (dirCount == 16) ? 1 : 0;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##rsdircount", &dirIdx, k_dirItems, 2))
                {
                    EntityDef nd = oldDef;
                    nd.rotatedSprite.directionCount = (dirIdx == 1) ? 16u : 8u;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, nd));
                }
                ImGui::SameLine(); ImGui::TextDisabled("Directions");

                ImGui::SetNextItemWidth(-1.0f);
                bool rowsChanged = dragIntUndo("##rsrows", &animRows, 1.0f, 0, 0, "Anim Rows: %d");

                ImGui::SetNextItemWidth(-1.0f);
                bool colsChanged = dragIntUndo("##rscols", &animCols, 1.0f, 0, 0, "Anim Cols: %d");

                ImGui::SetNextItemWidth(-1.0f);
                bool fpsChanged  = dragFloatUndo("##rsfps", &frameRate, 0.1f, 0.0f, 0.0f, "FPS: %.1f");

                if (rowsChanged || colsChanged || fpsChanged)
                {
                    EntityDef nd = oldDef;
                    nd.rotatedSprite.animRows  = static_cast<uint32_t>(std::max(1, animRows));
                    nd.rotatedSprite.animCols  = static_cast<uint32_t>(std::max(1, animCols));
                    nd.rotatedSprite.frameRate = frameRate;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, nd));
                }
            }
        }
        else if (ed.visualType == EntityVisualType::ParticleEmitter)
        {
            ImGui::Spacing();
            ImGui::SeparatorText("Particle Emitter");
            {
                // s_preDragParticle captures the entity state at the moment a
                // drag begins (IsItemActivated).  Live mutations write directly
                // to ed.particle.* so the 3D viewport updates every frame.
                // The undo command is pushed on commit using this snapshot.
                static EntityDef s_preDragParticle = {};

                float     emissionRate     = ed.particle.emissionRate;
                glm::vec3 emitDir          = ed.particle.emitDir;
                float     coneHalfAngleDeg = glm::degrees(ed.particle.coneHalfAngle);
                float     speedMin         = ed.particle.speedMin;
                float     speedMax         = ed.particle.speedMax;
                float     lifetimeMin      = ed.particle.lifetimeMin;
                float     lifetimeMax      = ed.particle.lifetimeMax;
                glm::vec4 colorStart       = ed.particle.colorStart;
                glm::vec4 colorEnd         = ed.particle.colorEnd;
                float     sizeStart        = ed.particle.sizeStart;
                float     sizeEnd          = ed.particle.sizeEnd;
                float     drag             = ed.particle.drag;
                glm::vec3 gravity          = ed.particle.gravity;
                float     softRange        = ed.particle.softRange;
                float     emissiveStart    = ed.particle.emissiveStart;
                float     emissiveEnd      = ed.particle.emissiveEnd;
                float     shadowDensity    = ed.particle.shadowDensity;

                ImGui::SetNextItemWidth(-1.0f);
                bool erChanged = dragFloatUndo("##perate", &emissionRate, 0.1f,
                                               0.0f, 0.0f, "Emission Rate: %.1f/s");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.emissionRate = emissionRate;

                ImGui::SetNextItemWidth(-1.0f);
                bool edirChanged = dragFloat3Undo("##pedir", &emitDir.x, 0.01f,
                                                  0.0f, 0.0f, "%.2f");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                {
                    const float dlen = glm::length(emitDir);
                    ed.particle.emitDir = (dlen > 1e-5f) ? emitDir / dlen : glm::vec3(0.0f, 1.0f, 0.0f);
                }

                ImGui::SetNextItemWidth(-1.0f);
                bool coneChanged = dragFloatUndo("##pecone", &coneHalfAngleDeg, 0.5f,
                                                 0.0f, 0.0f, "Cone: %.1f\xc2\xb0");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.coneHalfAngle = glm::radians(coneHalfAngleDeg);

                ImGui::SetNextItemWidth(-1.0f);
                bool sminChanged = dragFloatUndo("##pespmin", &speedMin, 0.1f,
                                                 0.0f, 0.0f, "Speed Min: %.1f");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.speedMin = speedMin;

                ImGui::SetNextItemWidth(-1.0f);
                bool smaxChanged = dragFloatUndo("##pespmax", &speedMax, 0.1f,
                                                 0.0f, 0.0f, "Speed Max: %.1f");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.speedMax = speedMax;

                ImGui::SetNextItemWidth(-1.0f);
                bool ltminChanged = dragFloatUndo("##peltmin", &lifetimeMin, 0.1f,
                                                  0.0f, 0.0f, "Life Min: %.1f s");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.lifetimeMin = lifetimeMin;

                ImGui::SetNextItemWidth(-1.0f);
                bool ltmaxChanged = dragFloatUndo("##peltmax", &lifetimeMax, 0.1f,
                                                  0.0f, 0.0f, "Life Max: %.1f s");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.lifetimeMax = lifetimeMax;

                // Colors: IsItemActivated captures pre-state when the picker opens;
                // always push the current picker value live so the viewport tracks it.
                bool csChanged = colorEditUndo("Color Start##particle", &colorStart.x, 4);
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                ed.particle.colorStart = colorStart;

                bool ceChanged = colorEditUndo("Color End##particle", &colorEnd.x, 4);
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                ed.particle.colorEnd = colorEnd;

                ImGui::SetNextItemWidth(-1.0f);
                bool ssChanged = dragFloatUndo("##peszstart", &sizeStart, 0.01f,
                                               0.0f, 0.0f, "Size Start: %.3f m");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.sizeStart = sizeStart;

                ImGui::SetNextItemWidth(-1.0f);
                bool seChanged = dragFloatUndo("##peszend", &sizeEnd, 0.01f,
                                              0.0f, 0.0f, "Size End: %.3f m");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.sizeEnd = sizeEnd;

                ImGui::SetNextItemWidth(-1.0f);
                bool dragChanged = dragFloatUndo("##pedrag", &drag, 0.01f,
                                                 0.0f, 0.0f, "Drag: %.2f");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.drag = drag;

                ImGui::SetNextItemWidth(-1.0f);
                bool gravChanged = dragFloat3Undo("##pegrav", &gravity.x, 0.1f,
                                                  0.0f, 0.0f, "%.1f");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.gravity = gravity;

                ImGui::SetNextItemWidth(-1.0f);
                bool softChanged = dragFloatUndo("##pesoftrange", &softRange, 0.05f,
                                                 0.0f, 0.0f, "Soft Range: %.2f m");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.softRange = softRange;

                ImGui::Spacing();
                ImGui::SeparatorText("Emissive / Lighting");

                ImGui::SetNextItemWidth(-1.0f);
                bool emStartChanged = dragFloatUndo("##peemstart", &emissiveStart, 0.05f,
                                                    0.0f, 0.0f, "Emissive Start: %.2f");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.emissiveStart = emissiveStart;

                ImGui::SetNextItemWidth(-1.0f);
                bool emEndChanged = dragFloatUndo("##peemend", &emissiveEnd, 0.05f,
                                                  0.0f, 0.0f, "Emissive End: %.2f");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.emissiveEnd = emissiveEnd;

                // Checkbox: instantaneous — capture pre-state locally and push
                // its own command immediately so undo is always correct.
                {
                    bool emitsLight = ed.particle.emitsLight;
                    const EntityDef preCheck = ed;
                    if (ImGui::Checkbox("Emits Light##pe", &emitsLight) &&
                        emitsLight != preCheck.particle.emitsLight)
                    {
                        ed.particle.emitsLight = emitsLight;
                        EntityDef newDef = ed;
                        doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                            doc, ei, preCheck, newDef));
                    }
                }

                // Light radius — only shown when emitsLight is enabled.
                if (ed.particle.emitsLight)
                {
                    float lightRadius = ed.particle.emitLightRadius;
                    ImGui::SetNextItemWidth(-1.0f);
                    bool lrChanged = dragFloatUndo("##pelightr", &lightRadius, 0.5f,
                                                   1.0f, 100.0f, "Light Radius: %.1f m");
                    if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                    if (ImGui::IsItemActive() || ImGui::IsItemEdited())
                        ed.particle.emitLightRadius = lightRadius;
                    if (lrChanged)
                    {
                        EntityDef newDef = ed;
                        doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                            doc, ei, s_preDragParticle, newDef));
                    }
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Shadow Volume (RT)");

                ImGui::SetNextItemWidth(-1.0f);
                bool shadowDensityChanged = dragFloatUndo(
                    "##peshadowdensity", &shadowDensity, 0.1f,
                    0.0f, 0.0f, "Shadow Density: %.2f");
                if (ImGui::IsItemActivated()) s_preDragParticle = ed;
                if (ImGui::IsItemActive() || ImGui::IsItemEdited()) ed.particle.shadowDensity = shadowDensity;

                if (erChanged || edirChanged || coneChanged ||
                    sminChanged || smaxChanged || ltminChanged || ltmaxChanged ||
                    csChanged || ceChanged || ssChanged || seChanged ||
                    dragChanged || gravChanged || softChanged ||
                    emStartChanged || emEndChanged ||
                    shadowDensityChanged)
                {
                    // ed.particle.* already holds the final live-mutated values;
                    // re-apply normalization-dependent fields to be safe.
                    EntityDef newDef = ed;
                    const float dlen = glm::length(newDef.particle.emitDir);
                    newDef.particle.emitDir = (dlen > 1e-5f)
                        ? newDef.particle.emitDir / dlen
                        : glm::vec3(0.0f, 1.0f, 0.0f);
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, s_preDragParticle, newDef));
                }
            }
        }

        // ── Physics ───────────────────────────────────────────────────────────────────
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Physics"))
        {
            static const char* k_shapeLabels[] = {"Box", "Sphere", "Capsule"};
            int shapeIdx = static_cast<int>(ed.physics.shape);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##pshape", &shapeIdx, k_shapeLabels, 3))
            {
                EntityDef oldDef = ed;
                EntityDef newDef = ed;
                newDef.physics.shape = static_cast<CollisionShape>(shapeIdx);
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
            }
            ImGui::SameLine(); ImGui::TextDisabled("Shape");

            bool isStatic = ed.physics.isStatic;
            if (ImGui::Checkbox("Static##phys", &isStatic))
            {
                EntityDef oldDef = ed; EntityDef newDef = ed;
                newDef.physics.isStatic = isStatic;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
            }

            if (!ed.physics.isStatic)
            {
                float mass = ed.physics.mass;
                ImGui::SetNextItemWidth(-1.0f);
                if (dragFloatUndo("##pmass", &mass, 0.1f, 0.0f, 0.0f, "Mass: %.2f kg") &&
                    mass != ed.physics.mass)
                {
                    EntityDef oldDef = ed; EntityDef newDef = ed;
                    newDef.physics.mass = mass;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
                }
            }
        }

        // ── Script ───────────────────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Script"))
        {
            char scriptBuf[512] = {};
            std::strncpy(scriptBuf, ed.script.scriptPath.c_str(), 511);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##scriptpath", scriptBuf, sizeof(scriptBuf));
            ImGui::SameLine(); ImGui::TextDisabled("Script");
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                std::string(scriptBuf) != ed.script.scriptPath)
            {
                EntityDef oldDef = ed; EntityDef newDef = ed;
                newDef.script.scriptPath = scriptBuf;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
            }

            // ── Exposed variables ────────────────────────────────────────────
            ImGui::Spacing();
            ImGui::SeparatorText("Exposed Variables");

            // Sort for stable display order
            std::vector<std::pair<std::string, std::string>> vars(
                ed.script.exposedVars.begin(), ed.script.exposedVars.end());
            std::sort(vars.begin(), vars.end(),
                      [](const auto& a, const auto& b){ return a.first < b.first; });

            int toDelete = -1;
            for (int vi = 0; vi < static_cast<int>(vars.size()); ++vi)
            {
                ImGui::PushID(vi);
                char keyBuf[256] = {};
                char valBuf[256] = {};
                std::strncpy(keyBuf, vars[vi].first.c_str(),  255);
                std::strncpy(valBuf, vars[vi].second.c_str(), 255);

                const float avail = ImGui::GetContentRegionAvail().x;
                const float btnW  = ImGui::GetFrameHeight();
                const float half  = (avail - btnW - ImGui::GetStyle().ItemSpacing.x * 2.0f) * 0.5f;

                ImGui::SetNextItemWidth(half);
                ImGui::InputText("##vkey", keyBuf, sizeof(keyBuf));
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    std::string newKey(keyBuf);
                    if (newKey != vars[vi].first)
                    {
                        EntityDef oldDef = ed; EntityDef newDef = ed;
                        newDef.script.exposedVars.erase(vars[vi].first);
                        newDef.script.exposedVars.emplace(std::move(newKey), vars[vi].second);
                        doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
                    }
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(half);
                ImGui::InputText("##vval", valBuf, sizeof(valBuf));
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    std::string newVal(valBuf);
                    if (newVal != vars[vi].second)
                    {
                        EntityDef oldDef = ed; EntityDef newDef = ed;
                        newDef.script.exposedVars[vars[vi].first] = std::move(newVal);
                        doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("X"))
                    toDelete = vi;
                ImGui::PopID();
            }

            if (toDelete >= 0)
            {
                EntityDef oldDef = ed; EntityDef newDef = ed;
                newDef.script.exposedVars.erase(vars[toDelete].first);
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
            }

            if (ImGui::Button("Add Variable"))
            {
                EntityDef oldDef = ed; EntityDef newDef = ed;
                std::string newKey = "var";
                int suffix = 0;
                while (newDef.script.exposedVars.count(newKey))
                    newKey = "var" + std::to_string(++suffix);
                newDef.script.exposedVars.emplace(newKey, "");
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
            }
        }

        // ── Audio ───────────────────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Audio"))
        {
            char soundBuf[512] = {};
            std::strncpy(soundBuf, ed.audio.soundPath.c_str(), 511);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##soundpath", soundBuf, sizeof(soundBuf));
            ImGui::SameLine(); ImGui::TextDisabled("Sound");
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                std::string(soundBuf) != ed.audio.soundPath)
            {
                EntityDef oldDef = ed; EntityDef newDef = ed;
                newDef.audio.soundPath = soundBuf;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
            }

            float falloff = ed.audio.falloffRadius;
            ImGui::SetNextItemWidth(-1.0f);
            if (dragFloatUndo("##sfalloff", &falloff, 0.5f, 0.0f, 0.0f, "Falloff: %.1f m") &&
                falloff != ed.audio.falloffRadius)
            {
                EntityDef oldDef = ed; EntityDef newDef = ed;
                newDef.audio.falloffRadius = falloff;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
            }

            bool loop = ed.audio.loop;
            if (ImGui::Checkbox("Loop##audio", &loop))
            {
                EntityDef oldDef = ed; EntityDef newDef = ed;
                newDef.audio.loop = loop;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
            }

            float volume = ed.audio.volume;
            ImGui::SetNextItemWidth(-1.0f);
            if (dragFloatUndo("##svol", &volume, 0.01f, 0.0f, 1.0f, "Volume: %.2f") &&
                volume != ed.audio.volume)
            {
                EntityDef oldDef = ed; EntityDef newDef = ed;
                newDef.audio.volume = volume;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
            }

            bool autoPlay = ed.audio.autoPlay;
            if (ImGui::Checkbox("Auto-Play##audio", &autoPlay))
            {
                EntityDef oldDef = ed; EntityDef newDef = ed;
                newDef.audio.autoPlay = autoPlay;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, ei, oldDef, newDef));
            }
        }

        // ── Actions ───────────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Delete Entity"))
            doc.pushCommand(std::make_unique<CmdDeleteEntity>(doc, ei));
        ImGui::PopID();  // end entity PushID
    }
    else if (sel.hasSingleOf(SelectionType::PlayerStart))
    {
        const auto& ps = doc.playerStart();

        if (!ps.has_value())
        {
            ImGui::TextDisabled("(player start cleared)");
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Player Start");

        // ── Position ─────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Position");
        {
            glm::vec3 pos = ps->position;
            ImGui::SetNextItemWidth(-1.0f);
            if (dragFloat3Undo("##pspos", &pos.x, 0.1f, 0.0f, 0.0f, "%.2f") &&
                (pos.x != ps->position.x ||
                 pos.y != ps->position.y ||
                 pos.z != ps->position.z))
            {
                PlayerStart newPs = *ps;
                newPs.position    = pos;
                doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
                    doc, ps, newPs));
            }
        }

        // ── Yaw ──────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Orientation");
        {
            float yawDeg = glm::degrees(ps->yaw);
            ImGui::SetNextItemWidth(-1.0f);
            if (dragFloatUndo("##psyaw", &yawDeg, 1.0f, 0.0f, 0.0f, "Yaw: %.1f\xc2\xb0"))
            {
                const float newYaw = glm::radians(yawDeg);
                if (newYaw != ps->yaw)
                {
                    PlayerStart newPs = *ps;
                    newPs.yaw         = newYaw;
                    doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
                        doc, ps, newPs));
                }
            }
        }

        // ── Actions ───────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Clear Player Start"))
        {
            doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
                doc, ps, std::nullopt));
        }
    }
    else if (!sel.hasSelection())
    {
        auto& map = doc.mapData();

        ImGui::SeparatorText("Map Properties");
        {
            char nameBuf[256] = {};
            std::strncpy(nameBuf, map.name.c_str(), 255);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##mapname", nameBuf, sizeof(nameBuf));
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                const std::string newName = nameBuf;
                if (newName != map.name)
                    doc.pushCommand(std::make_unique<CmdSetMapMeta>(
                        doc, map.name, newName, map.author, map.author));
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Name");

            char authorBuf[256] = {};
            std::strncpy(authorBuf, map.author.c_str(), 255);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##mapauthor", authorBuf, sizeof(authorBuf));
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                const std::string newAuthor = authorBuf;
                if (newAuthor != map.author)
                    doc.pushCommand(std::make_unique<CmdSetMapMeta>(
                        doc, map.name, map.name, map.author, newAuthor));
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Author");
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Global Ambient");
        {
            glm::vec3 col = map.globalAmbientColor;
            if (colorEditUndo("Colour##glo", &col.x, 3))
                doc.pushCommand(std::make_unique<CmdSetGlobalAmbient>(
                    doc, map.globalAmbientColor, col,
                    map.globalAmbientIntensity, map.globalAmbientIntensity));

            float intensity = map.globalAmbientIntensity;
            ImGui::SetNextItemWidth(-1.0f);
            if (dragFloatUndo("##gloint", &intensity, 0.01f, 0.0f, 0.0f, "Intensity: %.2f"))
                doc.pushCommand(std::make_unique<CmdSetGlobalAmbient>(
                    doc, map.globalAmbientColor, map.globalAmbientColor,
                    map.globalAmbientIntensity, intensity));
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Defaults");
        {
            // Sky path
            char skyBuf[512] = {};
            std::strncpy(skyBuf, doc.skyPath().c_str(), sizeof(skyBuf) - 1);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##skypath", skyBuf, sizeof(skyBuf));
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                const std::string newSky = skyBuf;
                if (newSky != doc.skyPath())
                    doc.pushCommand(std::make_unique<CmdSetMapDefaults>(
                        doc,
                        doc.skyPath(), newSky,
                        doc.gravity(), doc.gravity(),
                        doc.defaultFloorHeight(), doc.defaultFloorHeight(),
                        doc.defaultCeilHeight(),  doc.defaultCeilHeight()));
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Sky Path");

            // Gravity
            {
                float grav = doc.gravity();
                ImGui::SetNextItemWidth(-1.0f);
                if (dragFloatUndo("##gravity", &grav, 0.1f, 0.0f, 0.0f, "Gravity: %.2f m/s\xc2\xb2"))
                    doc.pushCommand(std::make_unique<CmdSetMapDefaults>(
                        doc,
                        doc.skyPath(), doc.skyPath(),
                        doc.gravity(), grav,
                        doc.defaultFloorHeight(), doc.defaultFloorHeight(),
                        doc.defaultCeilHeight(),  doc.defaultCeilHeight()));
            }

            // Default floor height
            {
                float floorH = doc.defaultFloorHeight();
                ImGui::SetNextItemWidth(-1.0f);
                if (dragFloatUndo("##deffloor", &floorH, 0.1f, 0.0f, 0.0f, "Default Floor: %.2f"))
                    doc.pushCommand(std::make_unique<CmdSetMapDefaults>(
                        doc,
                        doc.skyPath(), doc.skyPath(),
                        doc.gravity(), doc.gravity(),
                        doc.defaultFloorHeight(), floorH,
                        doc.defaultCeilHeight(),  doc.defaultCeilHeight()));
            }

            // Default ceil height
            {
                float ceilH = doc.defaultCeilHeight();
                ImGui::SetNextItemWidth(-1.0f);
                if (dragFloatUndo("##defceil", &ceilH, 0.1f, 0.0f, 0.0f, "Default Ceil: %.2f"))
                    doc.pushCommand(std::make_unique<CmdSetMapDefaults>(
                        doc,
                        doc.skyPath(), doc.skyPath(),
                        doc.gravity(), doc.gravity(),
                        doc.defaultFloorHeight(), doc.defaultFloorHeight(),
                        doc.defaultCeilHeight(),  ceilH));
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%zu sector(s)",
                            doc.mapData().sectors.size());
    }

    ImGui::End();
}

} // namespace daedalus::editor
