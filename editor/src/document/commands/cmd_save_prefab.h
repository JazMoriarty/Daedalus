#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/prefab_def.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Captures the currently selected sectors (and any entities whose XZ
/// position falls within the selection bounding box) as a named PrefabDef,
/// then appends it to the document's prefab library.  Undo removes it.
///
/// The selection snapshot is taken at construction time.  If the selection
/// is not of type Sector, or is empty, execute() is a harmless no-op.
class CmdSavePrefab final : public ICommand
{
public:
    /// Immediately captures the current selection from @p doc.
    CmdSavePrefab(EditMapDocument& doc, std::string name);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Save Prefab"; }

private:
    EditMapDocument& m_doc;
    PrefabDef        m_prefab;       ///< Built at construction from current selection.
    std::size_t      m_insertedIdx = 0;
};

} // namespace daedalus::editor
