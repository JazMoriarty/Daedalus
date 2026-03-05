#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Links two walls as a portal pair.
/// Sets wallA.portalSectorId = sectorB and wallB.portalSectorId = sectorA.
/// Undo restores both walls' previous portalSectorId values.
class CmdLinkPortal final : public ICommand
{
public:
    CmdLinkPortal(EditMapDocument& doc,
                  world::SectorId  sectorA, std::size_t wallIdxA,
                  world::SectorId  sectorB, std::size_t wallIdxB);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Link Portal"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorA;
    std::size_t      m_wallIdxA;
    world::SectorId  m_sectorB;
    std::size_t      m_wallIdxB;

    // Saved for undo.
    world::SectorId  m_prevPortalA = world::INVALID_SECTOR_ID;
    world::SectorId  m_prevPortalB = world::INVALID_SECTOR_ID;
};

} // namespace daedalus::editor
