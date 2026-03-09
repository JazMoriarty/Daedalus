// asset_browser_panel.cpp

#include "asset_browser_panel.h"
#include "catalog/material_catalog.h"

#include "imgui.h"

#include <set>

namespace daedalus::editor
{

// ─── openPicker / closePicker ─────────────────────────────────────────────────

void AssetBrowserPanel::openPicker(PickCallback cb)
{
    m_pickerCallback = std::move(cb);
    m_pickerOpen     = true;
    ImGui::OpenPopup("##AssetPicker");
}

void AssetBrowserPanel::closePicker() noexcept
{
    m_pickerOpen     = false;
    m_pickerCallback = nullptr;
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

// ─── draw ─────────────────────────────────────────────────────────────────────

void AssetBrowserPanel::draw(MaterialCatalog&      catalog,
                               rhi::IRenderDevice&   device,
                               render::IAssetLoader& loader)
{
    (void)loader;  // available for future reload-on-change

    // ── Dockable browser window ───────────────────────────────────────────────
    ImGui::Begin("Asset Browser");

    if (catalog.empty())
    {
        ImGui::TextDisabled("No asset root set.");
        ImGui::TextDisabled("Use File > Set Asset Root\xe2\x80\xa6 to choose a textures folder.");
    }
    else
    {
        // ── Left pane: folder tree ────────────────────────────────────────────
        ImGui::BeginChild("##folders",
                          ImVec2(std::min(150.0f, ImGui::GetContentRegionAvail().x * 0.25f), 0.0f),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

        // "All" entry.
        if (ImGui::Selectable("All##fold", m_selectedFolder.empty()))
            m_selectedFolder.clear();

        // Collect unique folder paths.
        std::set<std::string> folders;
        for (const auto& e : catalog.entries())
            if (!e.folderPath.empty()) folders.insert(e.folderPath);

        for (const auto& folder : folders)
        {
            if (ImGui::Selectable(folder.c_str(),
                                  m_selectedFolder == folder))
                m_selectedFolder = folder;
        }

        ImGui::EndChild();
        ImGui::SameLine();

        // ── Right pane: thumbnails ────────────────────────────────────────────
        ImGui::BeginChild("##thumbs", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        drawThumbnailGrid(catalog, device, /*isPicker=*/false);
        ImGui::EndChild();
    }

    ImGui::End();

    // ── Picker popup (floating, opened by openPicker()) ───────────────────────
    ImGui::SetNextWindowSize(ImVec2(400.0f, 360.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##AssetPicker",
                          ImGuiWindowFlags_NoNav))
    {
        ImGui::Text("Pick Material");
        ImGui::Separator();

        if (catalog.empty())
        {
            ImGui::TextDisabled("(catalog empty \xe2\x80\x94 set asset root first)");
        }
        else
        {
            ImGui::BeginChild("##pickscroll", ImVec2(0.0f, 0.0f),
                              ImGuiChildFlags_None);
            drawThumbnailGrid(catalog, device, /*isPicker=*/true);
            ImGui::EndChild();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            closePicker();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    else if (m_pickerOpen)
    {
        // Popup was closed externally (e.g. click-away).
        closePicker();
    }
}

} // namespace daedalus::editor
