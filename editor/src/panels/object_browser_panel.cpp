#include "object_browser_panel.h"

#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/editor_layer.h"
#include "daedalus/editor/entity_def.h"
#include "daedalus/editor/prefab_def.h"
#include "daedalus/editor/selection_state.h"
#include "document/commands/cmd_place_entity.h"
#include "document/commands/cmd_place_light.h"
#include "document/commands/cmd_save_prefab.h"
#include "document/commands/cmd_place_prefab.h"
#include "document/commands/cmd_set_player_start.h"
#include "daedalus/editor/light_def.h"

#include "imgui.h"

#include <memory>
#include <cstring>

namespace daedalus::editor
{

void ObjectBrowserPanel::draw(EditMapDocument& doc, glm::vec2 cursorMapPos)
{
    ImGui::Begin("Object Browser");

    // ── Entities ─────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Entities", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("Place at 2D cursor. Set asset path in Properties.");
        ImGui::Spacing();

        // Billboard row
        if (ImGui::Button("Billboard (Cutout)"))
        {
            EntityDef ed;
            ed.visualType = EntityVisualType::BillboardCutout;
            ed.position   = {cursorMapPos.x, 1.0f, cursorMapPos.y};
            doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, ed));
        }
        ImGui::SameLine();
        if (ImGui::Button("Blended"))
        {
            EntityDef ed;
            ed.visualType = EntityVisualType::BillboardBlended;
            ed.position   = {cursorMapPos.x, 1.0f, cursorMapPos.y};
            doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, ed));
        }
        ImGui::SameLine();
        if (ImGui::Button("Animated"))
        {
            EntityDef ed;
            ed.visualType      = EntityVisualType::AnimatedBillboard;
            ed.position        = {cursorMapPos.x, 1.0f, cursorMapPos.y};
            ed.anim.frameCount = 4u;
            ed.anim.cols       = 2u;
            ed.anim.rows       = 2u;
            ed.anim.frameRate  = 8.0f;
            doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, ed));
        }

        // Mesh row
        if (ImGui::Button("Voxel Object"))
        {
            EntityDef ed;
            ed.visualType = EntityVisualType::VoxelObject;
            ed.position   = {cursorMapPos.x, 0.0f, cursorMapPos.y};
            doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, ed));
        }
        ImGui::SameLine();
        if (ImGui::Button("Static Mesh"))
        {
            EntityDef ed;
            ed.visualType = EntityVisualType::StaticMesh;
            ed.position   = {cursorMapPos.x, 0.0f, cursorMapPos.y};
            doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, ed));
        }

        // Rotated billboard row
        if (ImGui::Button("Rotated Sprite"))
        {
            EntityDef ed;
            ed.visualType                  = EntityVisualType::RotatedSpriteSet;
            ed.position                    = {cursorMapPos.x, 1.0f, cursorMapPos.y};
            ed.rotatedSprite.directionCount = 8u;
            ed.rotatedSprite.animRows       = 1u;
            ed.rotatedSprite.animCols       = 8u;
            ed.rotatedSprite.frameRate      = 8.0f;
            doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, ed));
        }

        // Effect row
        if (ImGui::Button("Decal"))
        {
            EntityDef ed;
            ed.visualType = EntityVisualType::Decal;
            ed.position   = {cursorMapPos.x, 0.05f, cursorMapPos.y};
            ed.scale      = {2.0f, 0.1f, 2.0f};  // wide thin OBB for projection
            doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, ed));
        }
        ImGui::SameLine();
        if (ImGui::Button("Particle Emitter"))
        {
            EntityDef ed;
            ed.visualType = EntityVisualType::ParticleEmitter;
            ed.position   = {cursorMapPos.x, 0.5f, cursorMapPos.y};
            doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, ed));
        }

        ImGui::Spacing();
        ImGui::TextDisabled("(%zu entities)", doc.entities().size());
    }

    ImGui::Spacing();

    // ── Lights ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("Place Point Light"))
        {
            LightDef ld;
            ld.position = {cursorMapPos.x, 2.0f, cursorMapPos.y};
            ld.type     = LightType::Point;
            doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
        }
        ImGui::SameLine();
        if (ImGui::Button("Place Spot Light"))
        {
            LightDef ld;
            ld.position = {cursorMapPos.x, 4.0f, cursorMapPos.y};
            ld.type     = LightType::Spot;
            doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu lights)", doc.lights().size());
    }

    ImGui::Spacing();

    // ── Player Start ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Player Start", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& ps = doc.playerStart();

        if (ps.has_value())
        {
            ImGui::TextDisabled("(%.1f, %.1f)  yaw %.0f\xc2\xb0",
                                ps->position.x, ps->position.z,
                                glm::degrees(ps->yaw));
            ImGui::TextDisabled("Edit in Properties panel.");
            if (ImGui::Button("Clear##ps"))
            {
                doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
                    doc, ps, std::nullopt));
            }
        }
        else
        {
            ImGui::TextDisabled("(not set)");
            if (ImGui::Button("Set Here##ps"))
            {
                PlayerStart newPs;
                newPs.position = {cursorMapPos.x, 0.0f, cursorMapPos.y};
                newPs.yaw      = 0.0f;
                doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
                    doc, std::nullopt, newPs));
            }
        }
    }

    ImGui::Spacing();

    // ── Prefabs ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Prefabs"))
    {
        // ─ Save as prefab ───────────────────────────────────────────
        static char s_prefabNameBuf[128] = "new_prefab";
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("##pfname", s_prefabNameBuf, sizeof(s_prefabNameBuf));
        ImGui::SameLine();

        const bool canSave = doc.selection().type == SelectionType::Sector &&
                             !doc.selection().sectors.empty();
        if (!canSave) ImGui::BeginDisabled();
        if (ImGui::Button("Save Selection"))
            doc.pushCommand(std::make_unique<CmdSavePrefab>(doc, std::string(s_prefabNameBuf)));
        if (!canSave) ImGui::EndDisabled();

        if (!canSave)
            ImGui::TextDisabled("(select sectors first)");

        ImGui::Separator();

        // ─ Prefab library ──────────────────────────────────────────
        const auto& prefabs = doc.prefabs();
        if (prefabs.empty())
        {
            ImGui::TextDisabled("(no prefabs saved)");
        }
        else
        {
            ImGui::TextDisabled("(%zu prefabs)", prefabs.size());
            for (std::size_t i = 0; i < prefabs.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%s", prefabs[i].name.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Place"))
                    doc.pushCommand(std::make_unique<CmdPlacePrefab>(
                        doc, prefabs[i], cursorMapPos));
                ImGui::PopID();
            }
        }
    }

    ImGui::End();
}

} // namespace daedalus::editor
