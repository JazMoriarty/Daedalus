// asset_browser_panel.h
// Dockable panel that shows assets from the active catalogs.
//
// Two categories:
//  • Textures — thumbnail grid of material images from MaterialCatalog.
//  • Models   — list of .gltf / .glb files from ModelCatalog.
//
// Two display states:
//  • Browsing: category buttons let the user switch freely.
//  • Picker (invoked by openPicker / openModelPicker): the panel shows the
//    appropriate category and a single click fires the callback.

#pragma once

#include "daedalus/core/types.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/i_asset_loader.h"

#include <functional>
#include <string>

namespace daedalus::editor
{

class MaterialCatalog;
class ModelCatalog;

class AssetBrowserPanel
{
public:
    /// Callback type for texture/material picks: called with the selected UUID.
    using PickCallback      = std::function<void(const UUID&)>;
    /// Callback type for model picks: called with the relative path string.
    using ModelPickCallback = std::function<void(const std::string&)>;

    AssetBrowserPanel()  = default;
    ~AssetBrowserPanel() = default;

    AssetBrowserPanel(const AssetBrowserPanel&)            = delete;
    AssetBrowserPanel& operator=(const AssetBrowserPanel&) = delete;

    /// Draw the dockable browser panel (both categories, picker banner, etc.).
    /// Pass a non-null @p voxCatalog to enable the Voxels category/tab.
    void draw(MaterialCatalog&      catalog,
              rhi::IRenderDevice&   device,
              render::IAssetLoader& loader,
              ModelCatalog&         modelCatalog,
              ModelCatalog*         voxCatalog = nullptr);

    // ─── Texture picker mode ────────────────────────────────────────────────

    /// Activate picker mode showing the Textures category.  When the user
    /// clicks a thumbnail, cb is invoked with the UUID and picker mode ends.
    void openPicker(PickCallback cb, std::string label = "");

    // ─── Model picker mode ──────────────────────────────────────────────────

    /// Activate picker mode showing the Models category.  When the user
    /// clicks a model entry, cb is invoked with the relative path string and
    /// picker mode ends automatically.
    void openModelPicker(ModelPickCallback cb, std::string label = "");

    // ─── Voxel picker mode ──────────────────────────────────────────────────

    /// Activate picker mode showing the Voxels category.  Callback receives
    /// the absolute path string of the selected .vox file.
    void openVoxPicker(ModelPickCallback cb, std::string label = "");

    [[nodiscard]] bool isPickerOpen() const noexcept { return m_pickerOpen; }

    /// Dismiss the picker without selection (e.g. Escape key).
    void closePicker() noexcept;

    /// Set callback to be invoked when user right-clicks "Generate Normal Map" on a texture.
    void setNormalMapGenCallback(std::function<void(const UUID&)> callback)
    {
        m_normalMapGenCallback = std::move(callback);
    }

private:
    enum class Mode { Textures, Models, Voxels };

    Mode         m_mode         = Mode::Textures;
    bool         m_pickerOpen   = false;
    bool         m_wantFocus    = false;  ///< Bring tab to front next draw.
    std::string  m_pickerLabel;

    // Texture picker state
    std::string  m_selectedFolder;
    PickCallback m_pickerCallback;

    // Model picker state
    std::string       m_modelSelectedFolder;
    ModelPickCallback m_modelPickerCallback;

    // Voxel picker state (reuses m_modelPickerCallback — mutually exclusive with model picker)
    std::string  m_voxSelectedFolder;

    // Normal map generator callback
    std::function<void(const UUID&)> m_normalMapGenCallback;

    // Draw helpers
    void drawThumbnailGrid(MaterialCatalog&    catalog,
                           rhi::IRenderDevice& device,
                           bool                isPicker);
    /// @p selectedFolder is a per-mode folder filter string (passed by reference so the
    /// sidebar selection persists across frames).
    void drawModelGrid(ModelCatalog& modelCatalog, bool isPicker, std::string& selectedFolder);
};

} // namespace daedalus::editor
