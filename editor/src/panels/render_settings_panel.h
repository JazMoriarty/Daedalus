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
    /// @param browseForFile  Callback invoked when the user clicks "Browse…" for
    ///                       an asset path field.  Returns the chosen path, or
    ///                       an empty path if the user cancelled.  May be null;
    ///                       when null the Browse button is hidden.
    explicit RenderSettingsPanel(
        std::function<std::filesystem::path()> browseForFile = nullptr);
    ~RenderSettingsPanel() = default;

    void draw(EditMapDocument& doc);

private:
    std::function<std::filesystem::path()> m_browseForFile;
};

} // namespace daedalus::editor
