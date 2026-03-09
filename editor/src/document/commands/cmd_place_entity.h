#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/entity_def.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Appends an EntityDef to the document and selects it.
/// Undo removes the entity and clears the selection.
class CmdPlaceEntity final : public ICommand
{
public:
    CmdPlaceEntity(EditMapDocument& doc, EntityDef entity);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Place Entity"; }

private:
    EditMapDocument& m_doc;
    EntityDef        m_entity;
    std::size_t      m_insertIdx = 0;  ///< Index at which the entity was inserted.
};

} // namespace daedalus::editor
