#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/world/world_types.h"

#include <cstddef>
#include <optional>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets the per-vertex floor and ceiling height overrides on a single wall vertex
/// (Wall::floorHeightOverride and Wall::ceilHeightOverride at walls[wallIndex]).
///
/// Either override may be std::nullopt to clear it (restore to the sector scalar).
/// The command captures the old overrides at construction and restores them on undo.
class CmdSetVertexHeight final : public ICommand
{
public:
    CmdSetVertexHeight(EditMapDocument&          doc,
                       world::SectorId           sectorId,
                       std::size_t               wallIndex,
                       std::optional<float>      newFloor,
                       std::optional<float>      newCeil);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Vertex Height"; }

private:
    EditMapDocument&      m_doc;
    world::SectorId       m_sectorId;
    std::size_t           m_wallIndex;
    std::optional<float>  m_newFloor;
    std::optional<float>  m_newCeil;
    std::optional<float>  m_oldFloor;
    std::optional<float>  m_oldCeil;
};

} // namespace daedalus::editor
