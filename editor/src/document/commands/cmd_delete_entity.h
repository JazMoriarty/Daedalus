#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/entity_def.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Removes the entity at `entityIndex` from the document.
/// Undo re-inserts the stored entity at the original index.
class CmdDeleteEntity final : public ICommand
{
public:
    CmdDeleteEntity(EditMapDocument& doc, std::size_t entityIndex);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Delete Entity"; }

private:
    EditMapDocument& m_doc;
    std::size_t      m_idx;
    EntityDef        m_stored;           ///< Captured copy (valid after execute).
    bool             m_executed = false;
};

} // namespace daedalus::editor
