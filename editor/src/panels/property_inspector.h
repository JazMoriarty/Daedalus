#pragma once

namespace daedalus::editor
{

class EditMapDocument;

class PropertyInspector
{
public:
    PropertyInspector()  = default;
    ~PropertyInspector() = default;

    void draw(EditMapDocument& doc);
};

} // namespace daedalus::editor
