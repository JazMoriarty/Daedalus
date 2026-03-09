#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"
#include "daedalus/core/types.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Which face of a wall receives the material assignment.
enum class WallSurface : uint32_t
{
    Front = 0,  ///< Full-height solid face (or portal strips).
    Upper = 1,  ///< Strip above a portal opening.
    Lower = 2,  ///< Strip below a portal opening.
    Back  = 3,  ///< Face seen looking back through the portal.
};

/// Assigns a material UUID to one surface of a wall.  Undoable.
class CmdSetWallMaterial final : public ICommand
{
public:
    CmdSetWallMaterial(EditMapDocument& doc,
                       world::SectorId  sectorId,
                       std::size_t      wallIndex,
                       WallSurface      surface,
                       UUID             newId);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Wall Material"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    std::size_t      m_wallIndex;
    WallSurface      m_surface;
    UUID             m_newId;
    UUID             m_oldId;

    /// Returns a reference to the target material UUID, or nullptr if out-of-range.
    [[nodiscard]] UUID* targetId() noexcept;
};

} // namespace daedalus::editor
