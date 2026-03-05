#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <cstddef>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Removes the portal link from wall A and its partner wall B.
/// Undo restores both links.
class CmdUnlinkPortal final : public ICommand
{
public:
    /// sectorB / wallIdxB are the current partner stored in wallA.portalSectorId.
    /// The caller must pass the correct partner indices at construction time.
    CmdUnlinkPortal(EditMapDocument& doc,
                    world::SectorId  sectorA, std::size_t wallIdxA,
                    world::SectorId  sectorB, std::size_t wallIdxB);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Unlink Portal"; }

private:
    EditMapDocument& m_doc;
    world::SectorId  m_sectorA;
    std::size_t      m_wallIdxA;
    world::SectorId  m_sectorB;
    std::size_t      m_wallIdxB;
};

} // namespace daedalus::editor
