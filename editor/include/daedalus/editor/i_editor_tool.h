// i_editor_tool.h
// Interface for interactive map-editing tools (select, draw sector, …).
//
// The 2D viewport calls these methods after converting raw window events into
// map-space coordinates.  All positions are in world XZ map coordinates
// (world X → mapX, world Z → mapZ).

#pragma once

#include <glm/glm.hpp>

namespace daedalus::editor
{

class EditMapDocument;

class IEditorTool
{
public:
    virtual ~IEditorTool() = default;

    IEditorTool(const IEditorTool&)            = delete;
    IEditorTool& operator=(const IEditorTool&) = delete;

    // ─── Lifecycle ────────────────────────────────────────────────────────────

    /// Called once when the tool becomes the active tool.
    virtual void activate(EditMapDocument& /*doc*/) {}

    /// Called once when another tool replaces this tool.
    virtual void deactivate(EditMapDocument& /*doc*/) {}

    // ─── Input ────────────────────────────────────────────────────────────────

    /// Mouse button pressed.  button: 0=left, 1=right, 2=middle.
    virtual void onMouseDown(EditMapDocument& /*doc*/,
                             float /*mapX*/, float /*mapZ*/,
                             int   /*button*/) {}

    /// Mouse moved (may be called every frame while hovering).
    virtual void onMouseMove(EditMapDocument& /*doc*/,
                             float /*mapX*/, float /*mapZ*/) {}

    /// Mouse button released.
    virtual void onMouseUp(EditMapDocument& /*doc*/,
                           float /*mapX*/, float /*mapZ*/,
                           int   /*button*/) {}

    /// Keyboard event.  keyCode is an ImGuiKey value cast to int.
    virtual void onKey(EditMapDocument& /*doc*/,
                       int  /*keyCode*/,
                       bool /*pressed*/) {}

    /// Drag-rectangle selection completed.  Both corners are in map XZ space
    /// with minCorner guaranteed to have smaller x and y than maxCorner.
    /// Default implementation is a no-op; SelectTool overrides this.
    virtual void onRectSelect(EditMapDocument& /*doc*/,
                              glm::vec2 /*minCorner*/,
                              glm::vec2 /*maxCorner*/) {}

protected:
    IEditorTool() = default;
};

} // namespace daedalus::editor
