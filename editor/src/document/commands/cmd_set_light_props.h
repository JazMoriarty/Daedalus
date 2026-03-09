#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/light_def.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Replaces all authored properties (type, color, radius, intensity,
/// direction, cone angles, range) of an existing light.
/// Undo restores the previous full definition.
class CmdSetLightProps final : public ICommand
{
public:
    CmdSetLightProps(EditMapDocument& doc,
                     std::size_t      lightIndex,
                     LightDef         oldDef,
                     LightDef         newDef);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Set Light Properties"; }

private:
    EditMapDocument& m_doc;
    std::size_t      m_idx;
    LightDef         m_oldDef;
    LightDef         m_newDef;
};

} // namespace daedalus::editor
