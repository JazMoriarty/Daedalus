// asset_browser_panel.h
// Dockable panel that shows material thumbnails from the active MaterialCatalog.
//
// Two modes:
//  • Browsing (always-open dock pane): folder tree on the left, thumbnail grid
//    on the right.  Double-clicking an entry is a no-op in this mode; the
//    browser exists purely for visual inspection and quick reference.
//
//  • Picker (invoked by openPicker): the same panel becomes modal-ish while
//    m_pickerOpen is true.  A single click on any thumbnail fires the stored
//    PickCallback with the selected UUID and closes the picker.

#pragma once

#include "daedalus/core/types.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/i_asset_loader.h"

#include <functional>
#include <string>

namespace daedalus::editor
{

class MaterialCatalog;

class AssetBrowserPanel
{
public:
    /// Callback type: called with the UUID selected by the user.
    using PickCallback = std::function<void(const UUID&)>;

    AssetBrowserPanel()  = default;
    ~AssetBrowserPanel() = default;

    AssetBrowserPanel(const AssetBrowserPanel&)            = delete;
    AssetBrowserPanel& operator=(const AssetBrowserPanel&) = delete;

    /// Draw the dockable browser panel.
    /// Also draws the picker popup if it is currently open.
    void draw(MaterialCatalog&      catalog,
              rhi::IRenderDevice&   device,
              render::IAssetLoader& loader);

    // ─── Picker mode ────────────────────────────────────────────────────────

    /// Activate picker mode on the panel.  A banner is shown with @p label
    /// (e.g. "Wall Front").  When the user clicks a material, cb is invoked
    /// with the UUID and picker mode ends automatically.
    void openPicker(PickCallback cb, std::string label = "");

    [[nodiscard]] bool isPickerOpen() const noexcept { return m_pickerOpen; }

    /// Dismiss the picker without selection (e.g. Escape key).
    void closePicker() noexcept;

private:
    std::string  m_selectedFolder;  ///< Folder filter ("" = show all).

    bool         m_pickerOpen  = false;
    bool         m_wantFocus   = false;  ///< Bring tab to front next draw.
    std::string  m_pickerLabel;
    PickCallback m_pickerCallback;

    // Draw the shared thumbnail grid.
    // If isPicker is true, a single click fires the callback and closes.
    void drawThumbnailGrid(MaterialCatalog&    catalog,
                           rhi::IRenderDevice& device,
                           bool                isPicker);
};

} // namespace daedalus::editor
