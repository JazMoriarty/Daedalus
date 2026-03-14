#pragma once

#include <filesystem>
#include <functional>

namespace daedalus::editor
{

class EditMapDocument;

/// Panel that exposes all SceneView post-processing parameters.
/// Changes apply immediately to the next Viewport3D frame — no undo.
class RenderSettingsPanel
{
public:
    /// @param onBrowseRequested  Zero-argument callback invoked when the user
    ///                           clicks "Browse…" for the LUT path field.  The
    ///                           caller is responsible for opening the file
    ///                           dialog asynchronously and delivering the chosen
    ///                           path via deliverPath().  May be null; the
    ///                           Browse button is hidden when null.
    explicit RenderSettingsPanel(
        std::function<void()> onBrowseRequested = nullptr);
    ~RenderSettingsPanel() = default;

    /// Deliver the path chosen by the async file dialog opened via the
    /// onBrowseRequested callback.  An empty path means the user cancelled;
    /// the LUT setting is left unchanged in that case.
    void deliverPath(const std::filesystem::path& path);

    void draw(EditMapDocument& doc);

private:
    std::function<void()>  m_onBrowseRequested;
    std::filesystem::path  m_pendingLutPath;  ///< Set by deliverPath(), consumed by draw().
    bool                   m_hasLutPath = false;
};

} // namespace daedalus::editor
