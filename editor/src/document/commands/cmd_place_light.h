#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/light_def.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Appends a LightDef to the document and selects it.
/// Accepts any light type; the caller constructs the full definition.
/// Undo removes the light and clears the selection.
class CmdPlaceLight final : public ICommand
{
public:
    CmdPlaceLight(EditMapDocument& doc, LightDef light);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Place Light"; }

private:
    EditMapDocument& m_doc;
    LightDef         m_light;      ///< Light definition to insert.
    std::size_t      m_insertIdx;  ///< Index where it was inserted (for undo).
};

} // namespace daedalus::editor
