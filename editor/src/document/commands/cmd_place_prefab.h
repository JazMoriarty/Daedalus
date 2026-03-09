#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/prefab_def.h"

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace daedalus::editor
{

class EditMapDocument;

/// Places all sectors and entities from a PrefabDef into the map at a given
/// world XZ position, offset from the prefab's stored pivot-relative positions.
/// Internally builds one CmdDrawSector per prefab sector and one CmdPlaceEntity
/// per prefab entity; all execute and undo together as a single undo history entry.
class CmdPlacePrefab final : public ICommand
{
public:
    CmdPlacePrefab(EditMapDocument& doc,
                   const PrefabDef& prefab,
                   glm::vec2        placementXZ);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Place Prefab"; }

private:
    std::vector<std::unique_ptr<ICommand>> m_steps;
};

} // namespace daedalus::editor
