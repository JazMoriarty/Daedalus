#pragma once

#include "daedalus/editor/i_command.h"

#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets the map-wide default values: sky texture path, gravity, and
/// the default floor/ceiling height applied when drawing new sectors.  Undoable.
class CmdSetMapDefaults final : public ICommand
{
public:
    CmdSetMapDefaults(EditMapDocument& doc,
                      std::string      oldSkyPath,
                      std::string      newSkyPath,
                      float            oldGravity,
                      float            newGravity,
                      float            oldFloorH,
                      float            newFloorH,
                      float            oldCeilH,
                      float            newCeilH);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Map Defaults"; }

private:
    EditMapDocument& m_doc;
    std::string      m_oldSkyPath;
    std::string      m_newSkyPath;
    float            m_oldGravity;
    float            m_newGravity;
    float            m_oldFloorH;
    float            m_newFloorH;
    float            m_oldCeilH;
    float            m_newCeilH;
};

} // namespace daedalus::editor
