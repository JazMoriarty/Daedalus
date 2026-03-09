#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/light_def.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Removes the light at `lightIndex` from the document.
/// Undo re-inserts the stored light at the original index.
class CmdDeleteLight final : public ICommand
{
public:
    CmdDeleteLight(EditMapDocument& doc, std::size_t lightIndex);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Delete Light"; }

private:
    EditMapDocument& m_doc;
    std::size_t      m_idx;
    LightDef         m_stored;   ///< Copy of the deleted light (captured in execute).
    bool             m_executed = false;
};

} // namespace daedalus::editor
