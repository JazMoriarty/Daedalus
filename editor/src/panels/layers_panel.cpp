#include "layers_panel.h"

#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/editor_layer.h"
#include "daedalus/editor/selection_state.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>

namespace daedalus::editor
{

void LayersPanel::draw(EditMapDocument& doc)
{
    ImGui::Begin("Layers");

    auto&          layers    = doc.layers();
    const uint32_t activeIdx = doc.activeLayerIdx();

    // ── Add layer button ──────────────────────────────────────────────────────
    if (ImGui::Button("+ Add Layer"))
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Layer %zu", layers.size() + 1u);
        layers.push_back(EditorLayer{buf, true, false});
    }

    ImGui::Separator();

    // ── Layer list ────────────────────────────────────────────────────────────
    const bool canDelete = (layers.size() > 1);

    for (uint32_t i = 0; i < static_cast<uint32_t>(layers.size()); ++i)
    {
        EditorLayer& layer = layers[i];

        ImGui::PushID(static_cast<int>(i));

        // Highlight the active layer row.
        const bool isActive = (i == activeIdx);
        if (isActive)
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.45f, 0.75f, 0.60f));

        // Selectable row — clicking sets the active layer.
        if (ImGui::Selectable("##row", isActive,
                              ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(0.0f, 0.0f)))
        {
            doc.setActiveLayerIdx(i);
        }

        if (isActive)
            ImGui::PopStyleColor();

        ImGui::SameLine();

        // Eye toggle (visibility).
        {
            const char* eyeLabel = layer.visible ? "O##eye" : " ##eye";
            if (ImGui::SmallButton(eyeLabel))
                layer.visible = !layer.visible;
        }
        ImGui::SameLine();

        // Lock toggle.
        {
            const char* lockLabel = layer.locked ? "L##lock" : " ##lock";
            if (ImGui::SmallButton(lockLabel))
                layer.locked = !layer.locked;
        }
        ImGui::SameLine();

        // Editable name.
        char nameBuf[128];
        std::strncpy(nameBuf, layer.name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(-canDelete ? 28.0f : 1.0f);
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
            layer.name = nameBuf;

        // Select-all-in-layer button.
        ImGui::SameLine();
        if (ImGui::SmallButton("Sel##selall"))
        {
            const auto& sectors = doc.mapData().sectors;
            SelectionState& sel = doc.selection();
            sel.clear();
            for (std::size_t si = 0; si < sectors.size(); ++si)
            {
                if (doc.sectorLayerIndex(si) == i)
                    sel.items.push_back({SelectionType::Sector,
                                         static_cast<world::SectorId>(si), 0});
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select all sectors on this layer");

        // Delete button (disabled when only one layer remains).
        if (canDelete)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("X##del"))
            {
                layers.erase(layers.begin() + i);
                // Keep active index in bounds.
                if (doc.activeLayerIdx() >= static_cast<uint32_t>(layers.size()))
                    doc.setActiveLayerIdx(static_cast<uint32_t>(layers.size()) - 1u);
                ImGui::PopID();
                break;  // Vector was modified; restart next frame.
            }
        }

        ImGui::PopID();
    }

    ImGui::End();
}

} // namespace daedalus::editor
