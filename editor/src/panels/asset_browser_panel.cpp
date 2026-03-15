// asset_browser_panel.cpp

#include "asset_browser_panel.h"
#include "catalog/material_catalog.h"
#include "catalog/model_catalog.h"

#include "imgui.h"

#include <set>

namespace daedalus::editor
{

// ─── openPicker / openModelPicker / closePicker ────────────────────────────────────────

void AssetBrowserPanel::openPicker(PickCallback cb, std::string label)
{
    m_pickerCallback = std::move(cb);
    m_pickerLabel    = std::move(label);
    m_pickerOpen     = true;
    m_wantFocus      = true;
    m_mode           = Mode::Textures;
}

void AssetBrowserPanel::openModelPicker(ModelPickCallback cb, std::string label)
{
    m_modelPickerCallback = std::move(cb);
    m_pickerLabel         = std::move(label);
    m_pickerOpen          = true;
    m_wantFocus           = true;
    m_mode                = Mode::Models;
}

void AssetBrowserPanel::closePicker() noexcept
{
    m_pickerOpen          = false;
    m_pickerCallback      = nullptr;
    m_modelPickerCallback = nullptr;
}

// ─── drawThumbnailGrid ────────────────────────────────────────────────────────
// Shared thumbnail grid — used by both the browser panel and picker popup.

void AssetBrowserPanel::drawThumbnailGrid(MaterialCatalog&    catalog,
                                           rhi::IRenderDevice& device,
                                           bool                isPicker)
{
    constexpr float kThumb   = 64.0f;
    constexpr float kLabelH  = 18.0f;
    constexpr float kPadH    = 6.0f;
    constexpr float kItemW   = kThumb + kPadH * 2.0f;
    constexpr float kItemH   = kThumb + kLabelH + kPadH;

    const float availW  = ImGui::GetContentRegionAvail().x;
    const int   numCols = std::max(1, static_cast<int>(availW / kItemW));

    ImGui::Columns(numCols, "##thumbcols", /*border=*/false);

    for (std::size_t i = 0; i < catalog.entries().size(); ++i)
    {
        const MaterialEntry& entry = catalog.entries()[i];

        // Apply folder filter.
        if (!m_selectedFolder.empty() && entry.folderPath != m_selectedFolder)
        {
            continue;
        }

        auto* thumb = catalog.getOrLoadThumbnail(entry.uuid, device);

        ImGui::PushID(static_cast<int>(i));

        bool clicked = false;
        if (thumb)
        {
            clicked = ImGui::ImageButton(
                "##thumb",
                reinterpret_cast<ImTextureID>(thumb->nativeHandle()),
                ImVec2(kThumb, kThumb),
                ImVec2(0, 0), ImVec2(1, 1),
                ImVec4(0, 0, 0, 0),
                ImVec4(1, 1, 1, 1));
        }
        else
        {
            // Placeholder when thumbnail hasn't loaded yet.
            ImGui::Dummy(ImVec2(kThumb, kThumb));
            clicked = ImGui::IsItemClicked();
        }

        // Tooltip with full path.
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", entry.absPath.c_str());

        // Drag source — drag a thumbnail into the 3D viewport to assign the material.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("MATERIAL_UUID", &entry.uuid, sizeof(UUID));
            if (thumb)
                ImGui::Image(reinterpret_cast<ImTextureID>(thumb->nativeHandle()),
                             ImVec2(32.0f, 32.0f));
            ImGui::SameLine();
            ImGui::TextUnformatted(entry.displayName.c_str());
            ImGui::EndDragDropSource();
        }

        // Truncated name label below the thumbnail.
        ImGui::TextUnformatted(entry.displayName.c_str());

        ImGui::PopID();

        if (clicked && isPicker && m_pickerCallback)
        {
            m_pickerCallback(entry.uuid);
            closePicker();
            ImGui::CloseCurrentPopup();
        }

        ImGui::NextColumn();
    }

    ImGui::Columns(1);
    (void)kItemH;
}

// ─── drawModelGrid ───────────────────────────────────────────────────────────────────────────
// Model list — used by both the browser panel and model picker.

void AssetBrowserPanel::drawModelGrid(ModelCatalog& modelCatalog, bool isPicker)
{
    for (std::size_t i = 0; i < modelCatalog.entries().size(); ++i)
    {
        const ModelEntry& entry = modelCatalog.entries()[i];

        // Apply folder filter.
        if (!m_modelSelectedFolder.empty() &&
            entry.folderPath != m_modelSelectedFolder)
            continue;

        ImGui::PushID(static_cast<int>(i));

        const bool clicked = ImGui::Selectable(entry.displayName.c_str());

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", entry.relPath.string().c_str());

        ImGui::PopID();

        if (clicked && isPicker && m_modelPickerCallback)
        {
            m_modelPickerCallback(entry.relPath.string());
            closePicker();
        }
    }
}

// ─── draw ───────────────────────────────────────────────────────────────────────────

void AssetBrowserPanel::draw(MaterialCatalog&      catalog,
                               rhi::IRenderDevice&   device,
                               render::IAssetLoader& loader,
                               ModelCatalog&         modelCatalog)
{
    (void)loader;  // available for future reload-on-change
    // ── Dockable browser window ───────────────────────────────────────────────────
    ImGui::Begin("Asset Browser");

    // Bring this tab to the front the frame picker is first activated.
    if (m_wantFocus)
    {
        ImGui::SetWindowFocus();
        m_wantFocus = false;
    }

    // ── Picker banner ────────────────────────────────────────────────────────────────
    if (m_pickerOpen)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
        const char* lbl = m_pickerLabel.empty() ? "asset" : m_pickerLabel.c_str();
        ImGui::Text("Picking: %s", lbl);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel"))
            closePicker();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            closePicker();
        ImGui::Separator();
    }

    // ── Category toggle buttons (disabled while picker is active) ───────────────
    ImGui::BeginDisabled(m_pickerOpen);
    {
        const bool texActive = (m_mode == Mode::Textures);
        const bool modActive = (m_mode == Mode::Models);

        if (texActive)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("Textures"))
            { m_mode = Mode::Textures; m_selectedFolder.clear(); }
        if (texActive) ImGui::PopStyleColor();

        ImGui::SameLine();

        if (modActive)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("Models"))
            { m_mode = Mode::Models; m_modelSelectedFolder.clear(); }
        if (modActive) ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    // ── Content for active category ────────────────────────────────────────────────
    if (m_mode == Mode::Textures)
    {
        if (catalog.empty())
        {
            ImGui::TextDisabled("No asset root set.");
            ImGui::TextDisabled(
                "Use File > Set Asset Root\xe2\x80\xa6 to choose a textures folder.");
        }
        else
        {
            // ── Left pane: folder tree
            ImGui::BeginChild(
                "##folders",
                ImVec2(std::min(150.0f, ImGui::GetContentRegionAvail().x * 0.25f), 0.0f),
                ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

            if (ImGui::Selectable("All##fold", m_selectedFolder.empty()))
                m_selectedFolder.clear();

            std::set<std::string> folders;
            for (const auto& e : catalog.entries())
                if (!e.folderPath.empty()) folders.insert(e.folderPath);
            for (const auto& folder : folders)
            {
                if (ImGui::Selectable(folder.c_str(), m_selectedFolder == folder))
                    m_selectedFolder = folder;
            }
            ImGui::EndChild();
            ImGui::SameLine();

            // ── Right pane: thumbnails
            ImGui::BeginChild("##thumbs", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
            drawThumbnailGrid(catalog, device, /*isPicker=*/m_pickerOpen);
            ImGui::EndChild();
        }
    }
    else  // Mode::Models
    {
        if (modelCatalog.empty())
        {
            ImGui::TextDisabled("No models found.");
            ImGui::TextDisabled(
                "Place .gltf / .glb files in assets/models/ next to the binary.");
        }
        else
        {
            // ── Left pane: folder tree
            ImGui::BeginChild(
                "##mfolders",
                ImVec2(std::min(150.0f, ImGui::GetContentRegionAvail().x * 0.25f), 0.0f),
                ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

            if (ImGui::Selectable("All##mfold", m_modelSelectedFolder.empty()))
                m_modelSelectedFolder.clear();

            std::set<std::string> folders;
            for (const auto& e : modelCatalog.entries())
                if (!e.folderPath.empty()) folders.insert(e.folderPath);
            for (const auto& folder : folders)
            {
                if (ImGui::Selectable(folder.c_str(),
                                      m_modelSelectedFolder == folder))
                    m_modelSelectedFolder = folder;
            }
            ImGui::EndChild();
            ImGui::SameLine();

            // ── Right pane: model list
            ImGui::BeginChild("##models", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
            drawModelGrid(modelCatalog, /*isPicker=*/m_pickerOpen);
            ImGui::EndChild();
        }
    }

    ImGui::End();
}

} // namespace daedalus::editor
