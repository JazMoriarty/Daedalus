#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/entity_def.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Replaces all authored properties of an existing entity.
/// Undo restores the previous full definition.
class CmdSetEntityProps final : public ICommand
{
public:
    CmdSetEntityProps(EditMapDocument& doc,
                      std::size_t      entityIndex,
                      EntityDef        oldDef,
                      EntityDef        newDef);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Set Entity Properties"; }

private:
    EditMapDocument& m_doc;
    std::size_t      m_idx;
    EntityDef        m_oldDef;
    EntityDef        m_newDef;
};

} // namespace daedalus::editor
