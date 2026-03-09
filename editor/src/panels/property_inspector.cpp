#include "property_inspector.h"
#include "asset_browser_panel.h"
#include "catalog/material_catalog.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/map_data.h"
#include "document/commands/cmd_set_sector_heights.h"
#include "document/commands/cmd_set_sector_ambient.h"
#include "document/commands/cmd_set_sector_flags.h"
#include "document/commands/cmd_set_wall_flags.h"
#include "document/commands/cmd_set_wall_uv.h"
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
#include "document/commands/cmd_set_map_meta.h"
#include "document/commands/cmd_set_map_defaults.h"
#include "document/commands/cmd_set_global_ambient.h"
#include "tools/geometry_utils.h"

#include "imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
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

void PropertyInspector::draw(EditMapDocument&      doc,
                              MaterialCatalog&      catalog,
                              rhi::IRenderDevice&   device,
                              render::IAssetLoader& loader,
                              AssetBrowserPanel&    assetBrowser)
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
        ImGui::PushID(static_cast<int>(sid));

        ImGui::SeparatorText("Sector");
        ImGui::Text("Index: %u", static_cast<unsigned>(sid));
        ImGui::Text("Walls: %u", static_cast<unsigned>(sector.walls.size()));

        ImGui::Spacing();
        ImGui::SeparatorText("Heights");
        {
            float floor = sector.floorHeight;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##floor", &floor, 0.1f, -100.0f, 100.0f, "Floor: %.2f");
            if (ImGui::IsItemDeactivatedAfterEdit() && floor != sector.floorHeight)
                doc.pushCommand(std::make_unique<CmdSetSectorHeights>(
                    doc, sid, floor, sector.ceilHeight));
        }
        {
            float ceil = sector.ceilHeight;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##ceil", &ceil, 0.1f, -100.0f, 100.0f, "Ceiling: %.2f");
            if (ImGui::IsItemDeactivatedAfterEdit() && ceil != sector.ceilHeight)
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
            ImGui::DragFloat("##ambint", &intensity, 0.01f, 0.0f, 10.0f,
                             "Intensity: %.2f");
            if (ImGui::IsItemDeactivatedAfterEdit() &&
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
                    assetBrowser.openPicker([&doc, sid, surface, capId](const UUID& uuid) {
                        (void)capId;
                        doc.pushCommand(std::make_unique<CmdSetSectorMaterial>(doc, sid, surface, uuid));
                    });
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

                ImGui::TreePop();
            }

            ImGui::PopID();
        }
        ImGui::PopID();  // end sector PushID
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
        }

        ImGui::Spacing();
        ImGui::SeparatorText("UV Mapping");
        {
            glm::vec2 uvOff = wall.uvOffset;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat2("##uvoff", &uvOff.x, 0.01f, -100.0f, 100.0f,
                              "Offset (%.3f, %.3f)");
            if (ImGui::IsItemDeactivatedAfterEdit())
                doc.pushCommand(std::make_unique<CmdSetWallUV>(
                    doc, sid, wi, uvOff, wall.uvScale, wall.uvRotation));

            glm::vec2 uvSc = wall.uvScale;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat2("##uvsc", &uvSc.x, 0.01f, 0.001f, 100.0f,
                              "Scale (%.3f, %.3f)");
            if (ImGui::IsItemDeactivatedAfterEdit())
                doc.pushCommand(std::make_unique<CmdSetWallUV>(
                    doc, sid, wi, wall.uvOffset, uvSc, wall.uvRotation));

            float uvRot = wall.uvRotation;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##uvrot", &uvRot, 0.01f, -6.283f, 6.283f,
                             "Rotation: %.3f rad");
            if (ImGui::IsItemDeactivatedAfterEdit())
                doc.pushCommand(std::make_unique<CmdSetWallUV>(
                    doc, sid, wi, wall.uvOffset, wall.uvScale, uvRot));
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
                    assetBrowser.openPicker([&doc, sid, wi, surface](const UUID& uuid) {
                        doc.pushCommand(std::make_unique<CmdSetWallMaterial>(doc, sid, wi, surface, uuid));
                    });
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
    else if (sel.type == SelectionType::Light)
    {
        const std::size_t li = sel.lightIndex;
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

        // ── Type ────────────────────────────────────────────────────
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

        // ── Position ─────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Position");
        {
            glm::vec3 pos = ld.position;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat3("##lpos", &pos.x, 0.1f, -10000.0f, 10000.0f,
                              "(%.2f, %.2f, %.2f)");
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                (pos.x != ld.position.x || pos.y != ld.position.y ||
                 pos.z != ld.position.z))
            {
                doc.pushCommand(std::make_unique<CmdMoveLight>(
                    doc, li, ld.position, pos));
            }
        }

        // ── Appearance ───────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Appearance");
        {
            const LightDef oldDef  = ld;
            glm::vec3      col     = ld.color;
            float          radius  = ld.radius;
            float          intens  = ld.intensity;

            bool colChanged = colorEditUndo("Color##light", &col.x, 3);

            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##lrad", &radius, 0.1f, 0.1f, 1000.0f, "Radius: %.1f m");
            bool radChanged = ImGui::IsItemDeactivatedAfterEdit();

            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##lint", &intens, 0.05f, 0.0f, 100.0f, "Intensity: %.2f");
            bool intChanged = ImGui::IsItemDeactivatedAfterEdit();

            if (colChanged || radChanged || intChanged)
            {
                LightDef newDef    = oldDef;
                newDef.color     = col;
                newDef.radius    = radius;
                newDef.intensity = intens;
                doc.pushCommand(std::make_unique<CmdSetLightProps>(
                    doc, li, oldDef, newDef));
            }
        }

        // ── Spot cone (visible when type == Spot) ────────────────────────
        if (ld.type == LightType::Spot)
        {
            ImGui::Spacing();
            ImGui::SeparatorText("Spot Cone");
            {
                const LightDef oldDef = ld;
                glm::vec3 dir      = ld.direction;
                float     innerDeg = glm::degrees(ld.innerConeAngle);
                float     outerDeg = glm::degrees(ld.outerConeAngle);
                float     range    = ld.range;

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat3("##ldir", &dir.x, 0.01f, -1.0f, 1.0f,
                                  "Dir (%.2f, %.2f, %.2f)");
                bool dirChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##linnercone", &innerDeg, 0.5f, 1.0f, 89.0f,
                                 "Inner: %.1f\xc2\xb0");
                bool innerChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##loutercone", &outerDeg, 0.5f, 1.0f, 90.0f,
                                 "Outer: %.1f\xc2\xb0");
                bool outerChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##lrange", &range, 0.1f, 0.1f, 1000.0f,
                                 "Range: %.1f m");
                bool rangeChanged = ImGui::IsItemDeactivatedAfterEdit();

                if (dirChanged || innerChanged || outerChanged || rangeChanged)
                {
                    const float len = glm::length(dir);
                    LightDef newDef       = oldDef;
                    newDef.direction      = (len > 1e-5f)
                        ? (dir / len)
                        : glm::vec3(0.0f, -1.0f, 0.0f);
                    newDef.innerConeAngle = glm::radians(
                        std::min(innerDeg, outerDeg));
                    newDef.outerConeAngle = glm::radians(
                        std::max(outerDeg, innerDeg));
                    newDef.range          = range;
                    doc.pushCommand(std::make_unique<CmdSetLightProps>(
                        doc, li, oldDef, newDef));
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
    else if (sel.type == SelectionType::Entity)
    {
        const std::size_t ei = sel.entityIndex;
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
            glm::vec3 pos = ed.position;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat3("##epos", &pos.x, 0.1f, -10000.0f, 10000.0f,
                              "(%.2f, %.2f, %.2f)");
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                (pos.x != ed.position.x || pos.y != ed.position.y ||
                 pos.z != ed.position.z))
            {
                doc.pushCommand(std::make_unique<CmdMoveEntity>(
                    doc, ei, ed.position, pos));
            }

            float yawDeg = glm::degrees(ed.yaw);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##eyaw", &yawDeg, 0.5f, -360.0f, 360.0f,
                             "Yaw: %.1f\xc2\xb0");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                const float newYaw = glm::radians(yawDeg);
                if (newYaw != ed.yaw)
                {
                    EntityDef oldDef = ed;
                    EntityDef newDef = ed;
                    newDef.yaw = newYaw;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, oldDef, newDef));
                }
            }

            float pitchDeg = glm::degrees(ed.pitch);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##epitch", &pitchDeg, 0.5f, -90.0f, 90.0f,
                             "Pitch: %.1f\xc2\xb0");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                const float newPitch = glm::radians(pitchDeg);
                if (newPitch != ed.pitch)
                {
                    EntityDef oldDef = ed;
                    EntityDef newDef = ed;
                    newDef.pitch = newPitch;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, oldDef, newDef));
                }
            }

            float rollDeg = glm::degrees(ed.roll);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##eroll", &rollDeg, 0.5f, -180.0f, 180.0f,
                             "Roll: %.1f\xc2\xb0");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                const float newRoll = glm::radians(rollDeg);
                if (newRoll != ed.roll)
                {
                    EntityDef oldDef = ed;
                    EntityDef newDef = ed;
                    newDef.roll = newRoll;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, oldDef, newDef));
                }
            }

            glm::vec3 scale = ed.scale;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat3("##escale", &scale.x, 0.01f, 0.001f, 1000.0f,
                              "Scale (%.3f, %.3f, %.3f)");
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                (scale.x != ed.scale.x || scale.y != ed.scale.y ||
                 scale.z != ed.scale.z))
            {
                EntityDef oldDef = ed;
                EntityDef newDef = ed;
                newDef.scale = scale;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                    doc, ei, oldDef, newDef));
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

        // ── Appearance ────────────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Appearance");
        {
            char pathBuf[512] = {};
            std::strncpy(pathBuf, ed.assetPath.c_str(), 511);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##easset", pathBuf, sizeof(pathBuf));
            ImGui::SameLine();
            ImGui::TextDisabled("Asset");
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                std::string(pathBuf) != ed.assetPath)
            {
                EntityDef oldDef = ed;
                EntityDef newDef = ed;
                newDef.assetPath = pathBuf;
                doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                    doc, ei, oldDef, newDef));
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
                ImGui::DragInt("##efc", &frameCount, 1.0f, 1, 4096, "Frames: %d");
                bool fcChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragInt("##ecols", &cols, 1.0f, 1, 256, "Cols: %d");
                bool colsChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragInt("##erows", &rows, 1.0f, 1, 256, "Rows: %d");
                bool rowsChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##efps", &frameRate, 0.1f, 0.1f, 120.0f, "FPS: %.1f");
                bool fpsChanged = ImGui::IsItemDeactivatedAfterEdit();

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
                char  normalBuf[512]   = {};
                std::strncpy(normalBuf, ed.decalMat.normalPath.c_str(), 511);
                float roughness = ed.decalMat.roughness;
                float metalness = ed.decalMat.metalness;
                float opacity   = ed.decalMat.opacity;

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##enorm", normalBuf, sizeof(normalBuf));
                ImGui::SameLine();
                ImGui::TextDisabled("Normal Map");
                bool normChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##erough", &roughness, 0.01f, 0.0f, 1.0f,
                                 "Roughness: %.2f");
                bool roughChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##emetal", &metalness, 0.01f, 0.0f, 1.0f,
                                 "Metalness: %.2f");
                bool metalChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##eopac", &opacity, 0.01f, 0.0f, 1.0f,
                                 "Opacity: %.2f");
                bool opacChanged = ImGui::IsItemDeactivatedAfterEdit();

                if (normChanged || roughChanged || metalChanged || opacChanged)
                {
                    EntityDef newDef           = oldDef;
                    newDef.decalMat.normalPath = normalBuf;
                    newDef.decalMat.roughness  = roughness;
                    newDef.decalMat.metalness  = metalness;
                    newDef.decalMat.opacity    = opacity;
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
                ImGui::DragInt("##rsrows", &animRows, 1.0f, 1, 256, "Anim Rows: %d");
                bool rowsChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragInt("##rscols", &animCols, 1.0f, 1, 256, "Anim Cols: %d");
                bool colsChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##rsfps", &frameRate, 0.1f, 0.1f, 120.0f, "FPS: %.1f");
                bool fpsChanged = ImGui::IsItemDeactivatedAfterEdit();

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
                const EntityDef oldDef     = ed;
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

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##perate", &emissionRate, 0.1f, 0.0f, 10000.0f,
                                 "Emission Rate: %.1f/s");
                bool erChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat3("##pedir", &emitDir.x, 0.01f, -1.0f, 1.0f,
                                  "Dir (%.2f, %.2f, %.2f)");
                bool edirChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##pecone", &coneHalfAngleDeg, 0.5f, 0.0f, 90.0f,
                                 "Cone: %.1f\xc2\xb0");
                bool coneChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##pespmin", &speedMin, 0.1f, 0.0f, 1000.0f,
                                 "Speed Min: %.1f");
                bool sminChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##pespmax", &speedMax, 0.1f, 0.0f, 1000.0f,
                                 "Speed Max: %.1f");
                bool smaxChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##peltmin", &lifetimeMin, 0.1f, 0.0f, 300.0f,
                                 "Life Min: %.1f s");
                bool ltminChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##peltmax", &lifetimeMax, 0.1f, 0.0f, 300.0f,
                                 "Life Max: %.1f s");
                bool ltmaxChanged = ImGui::IsItemDeactivatedAfterEdit();

                bool csChanged = colorEditUndo("Color Start##particle", &colorStart.x, 4);

                bool ceChanged = colorEditUndo("Color End##particle",   &colorEnd.x, 4);

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##peszstart", &sizeStart, 0.01f, 0.001f, 100.0f,
                                 "Size Start: %.3f m");
                bool ssChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##peszend", &sizeEnd, 0.01f, 0.001f, 100.0f,
                                 "Size End: %.3f m");
                bool seChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##pedrag", &drag, 0.01f, 0.0f, 100.0f,
                                 "Drag: %.2f");
                bool dragChanged = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat3("##pegrav", &gravity.x, 0.1f, -100.0f, 100.0f,
                                  "Gravity (%.1f, %.1f, %.1f)");
                bool gravChanged = ImGui::IsItemDeactivatedAfterEdit();

                if (erChanged || edirChanged || coneChanged ||
                    sminChanged || smaxChanged || ltminChanged || ltmaxChanged ||
                    csChanged || ceChanged || ssChanged || seChanged ||
                    dragChanged || gravChanged)
                {
                    EntityDef newDef = oldDef;
                    newDef.particle.emissionRate  = emissionRate;
                    const float len = glm::length(emitDir);
                    newDef.particle.emitDir       = (len > 1e-5f)
                        ? emitDir / len
                        : glm::vec3(0.0f, 1.0f, 0.0f);
                    newDef.particle.coneHalfAngle = glm::radians(coneHalfAngleDeg);
                    newDef.particle.speedMin      = speedMin;
                    newDef.particle.speedMax      = speedMax;
                    newDef.particle.lifetimeMin   = lifetimeMin;
                    newDef.particle.lifetimeMax   = lifetimeMax;
                    newDef.particle.colorStart    = colorStart;
                    newDef.particle.colorEnd      = colorEnd;
                    newDef.particle.sizeStart     = sizeStart;
                    newDef.particle.sizeEnd       = sizeEnd;
                    newDef.particle.drag          = drag;
                    newDef.particle.gravity       = gravity;
                    doc.pushCommand(std::make_unique<CmdSetEntityProps>(
                        doc, ei, oldDef, newDef));
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
                ImGui::DragFloat("##pmass", &mass, 0.1f, 0.001f, 10000.0f, "Mass: %.2f kg");
                if (ImGui::IsItemDeactivatedAfterEdit() && mass != ed.physics.mass)
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
            ImGui::DragFloat("##sfalloff", &falloff, 0.5f, 0.1f, 1000.0f,
                             "Falloff: %.1f m");
            if (ImGui::IsItemDeactivatedAfterEdit() && falloff != ed.audio.falloffRadius)
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
        }

        // ── Actions ───────────────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Delete Entity"))
            doc.pushCommand(std::make_unique<CmdDeleteEntity>(doc, ei));
        ImGui::PopID();  // end entity PushID
    }
    else if (sel.type == SelectionType::PlayerStart)
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
            ImGui::DragFloat3("##pspos", &pos.x, 0.1f, -10000.0f, 10000.0f,
                              "(%.2f, %.2f, %.2f)");
            if (ImGui::IsItemDeactivatedAfterEdit() &&
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
            ImGui::DragFloat("##psyaw", &yawDeg, 1.0f, -360.0f, 360.0f,
                             "Yaw: %.1f\xc2\xb0");
            if (ImGui::IsItemDeactivatedAfterEdit())
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
    else if (sel.type == SelectionType::None)
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
            ImGui::DragFloat("##gloint", &intensity, 0.01f, 0.0f, 10.0f,
                             "Intensity: %.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
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
                ImGui::DragFloat("##gravity", &grav, 0.1f, 0.0f, 100.0f,
                                 "Gravity: %.2f m/s\xc2\xb2");
                if (ImGui::IsItemDeactivatedAfterEdit())
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
                ImGui::DragFloat("##deffloor", &floorH, 0.1f, -100.0f, 100.0f,
                                 "Default Floor: %.2f");
                if (ImGui::IsItemDeactivatedAfterEdit())
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
                ImGui::DragFloat("##defceil", &ceilH, 0.1f, -100.0f, 100.0f,
                                 "Default Ceil: %.2f");
                if (ImGui::IsItemDeactivatedAfterEdit())
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
