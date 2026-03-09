#pragma once

namespace daedalus::editor
{

class EditMapDocument;

/// Panel that lists all editor layers and lets the user add, rename, toggle
/// visibility/lock, and select the active layer.
class LayersPanel
{
public:
    void draw(EditMapDocument& doc);
};

} // namespace daedalus::editor
