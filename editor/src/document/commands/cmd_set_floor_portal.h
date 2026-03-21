#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"
#include "daedalus/core/types.h"

#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Identifies which horizontal portal surface is being configured.
enum class HPortalSurface : uint32_t
{
    Floor   = 0,  ///< Sector::floorPortalSectorId / floorPortalMaterialId.
    Ceiling = 1,  ///< Sector::ceilPortalSectorId  / ceilPortalMaterialId.
};

/// Sets the floor or ceiling portal target sector and material for an existing sector.
/// Pass world::INVALID_SECTOR_ID for newTargetId to clear the portal (solid surface).
class CmdSetFloorPortal final : public ICommand
{
public:
    CmdSetFloorPortal(EditMapDocument&  doc,
                      world::SectorId  sectorId,
                      HPortalSurface   surface,
                      world::SectorId  newTargetId,
                      UUID             newMaterialId);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Floor/Ceiling Portal"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorId;
    HPortalSurface   m_surface;
    world::SectorId  m_newTargetId;
    UUID             m_newMaterialId;
    world::SectorId  m_oldTargetId   = world::INVALID_SECTOR_ID;
    UUID             m_oldMaterialId;

    void apply(world::SectorId targetId, UUID materialId);
};

} // namespace daedalus::editor
