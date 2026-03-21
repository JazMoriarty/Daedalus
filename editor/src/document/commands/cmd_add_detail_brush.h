#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"
#include "daedalus/world/map_data.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Appends a new DetailBrush to the end of Sector::details.
/// Undo removes the last-appended brush.  The index of the new brush is
/// (details.size() before execute()) and is available via insertedIndex()
/// after execute() is called.
class CmdAddDetailBrush final : public ICommand
{
public:
    CmdAddDetailBrush(EditMapDocument&       doc,
                      world::SectorId        sectorId,
                      world::DetailBrush     brush);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Add Detail Brush"; }

    /// Index of the inserted brush; only valid after execute() has been called.
    [[nodiscard]] std::size_t insertedIndex() const noexcept { return m_insertedIndex; }

private:
    EditMapDocument&    m_doc;
    world::SectorId     m_sectorId;
    world::DetailBrush  m_brush;
    std::size_t         m_insertedIndex = 0;
};

} // namespace daedalus::editor
