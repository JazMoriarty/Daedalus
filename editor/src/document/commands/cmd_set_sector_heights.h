#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

namespace daedalus::editor
{

class EditMapDocument;

/// Sets the floor and ceiling heights of an existing sector.
class CmdSetSectorHeights final : public ICommand
{
public:
    CmdSetSectorHeights(EditMapDocument& doc,
                        world::SectorId  sectorId,
                        float            newFloor,
                        float            newCeil);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Sector Heights"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    float            m_newFloor;
    float            m_newCeil;
    float            m_oldFloor = 0.0f;
    float            m_oldCeil  = 4.0f;
};

} // namespace daedalus::editor
