// splash_screen.h
// Cinematic splash/loading modal shown on startup before the editor is ready.
//
// Usage:
//   SplashScreen splash;
//   splash.init((void*)mtlDevice, executableDir());  // after ImGui is set up
//   // each frame:
//   if (splash.isVisible()) splash.draw(dt);

#pragma once

#include <string>

namespace daedalus::editor
{

class SplashScreen
{
public:
    SplashScreen()  = default;
    ~SplashScreen();

    SplashScreen(const SplashScreen&)            = delete;
    SplashScreen& operator=(const SplashScreen&) = delete;

    /// Load the splash image and gather hardware/OS info.
    /// @param mtlDevice  id<MTLDevice> cast to void* (avoids Obj-C in header).
    /// @param resDir     Resource directory path (works for standalone + app bundles).
    void init(void* mtlDevice, const std::string& resDir);

    /// Returns true while the modal is still on screen.
    [[nodiscard]] bool isVisible() const noexcept { return m_visible; }

    /// Draw the splash modal.  Call every frame while isVisible() is true.
    /// @param dt  Frame delta-time in seconds.
    void draw(float dt);

private:
    // ── Texture ───────────────────────────────────────────────────────────────
    void* m_texture = nullptr;   ///< id<MTLTexture>, ARC-retained via __bridge_retained.
    int   m_texW    = 0;
    int   m_texH    = 0;

    // ── Timing ────────────────────────────────────────────────────────────────
    float m_total   = 0.0f;  ///< Seconds since splash first appeared.
    float m_msgT    = 0.0f;  ///< Seconds into the current message.
    int   m_msgIdx  = 0;     ///< Index into the message table.

    // ── State ─────────────────────────────────────────────────────────────────
    bool m_visible  = true;
    bool m_opened   = false;  ///< Whether OpenPopup has been called.

    // ── System info (pre-formatted for display) ───────────────────────────────
    std::string m_infoLine;   ///< "macOS X.Y  ·  Apple M3 Max  ·  12 cores  ·  64 GB RAM"
};

} // namespace daedalus::editor
