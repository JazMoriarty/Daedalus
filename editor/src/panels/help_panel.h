#pragma once

namespace daedalus::editor
{

/// Full-featured editor help screen.
/// A floating window with a left-side category list and a right-side scrollable
/// content area.  Opened/closed by pressing F1 or via the Help menu.
class HelpPanel
{
public:
    /// Draw the panel if it is currently open.  Call every frame.
    void draw();

    bool isOpen()          const noexcept { return m_open; }
    void setOpen(bool v)         noexcept { m_open = v; }
    void toggle()                noexcept { m_open = !m_open; }

private:
    bool m_open     = false;
    int  m_category = 0;       ///< Index of the selected sidebar category.

    void drawQuickStart();
    void drawKeyboard();
    void draw2DViewport();
    void draw3DViewport();
    void drawTools();
    void drawSectorsWalls();
    void drawAdvancedGeometry();
    void drawDetailGeometry();
    void drawEntities();
    void drawLights();
    void drawPlayerStart();
    void drawPortalsLayers();
    void drawPrefabs();
    void drawAssetBrowser();
    void drawRenderSettings();
    void drawMapDoctor();
};

} // namespace daedalus::editor
