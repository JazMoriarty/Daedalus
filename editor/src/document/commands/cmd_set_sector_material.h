#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"
#include "daedalus/core/types.h"

#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Which horizontal surface of a sector receives the material assignment.
enum class SectorSurface : uint32_t
{
    Floor = 0,  ///< Floor surface.
    Ceil  = 1,  ///< Ceiling surface.
};

/// Assigns a material UUID to the floor or ceiling of a sector.  Undoable.
class CmdSetSectorMaterial final : public ICommand
{
public:
    CmdSetSectorMaterial(EditMapDocument& doc,
                         world::SectorId  sectorId,
                         SectorSurface    surface,
                         UUID             newId);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Sector Material"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    SectorSurface    m_surface;
    UUID             m_newId;
    UUID             m_oldId;

    [[nodiscard]] UUID* targetId() noexcept;
};

} // namespace daedalus::editor
