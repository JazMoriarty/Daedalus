#pragma once

#include <glm/glm.hpp>

namespace daedalus::editor
{

class EditMapDocument;

/// Panel that lists and places scene objects: lights, player start, prefabs.
class ObjectBrowserPanel
{
public:
    /// @param doc          The active document.
    /// @param cursorMapPos Current 2-D map cursor position (for placement).
    void draw(EditMapDocument& doc, glm::vec2 cursorMapPos);
};

} // namespace daedalus::editor
