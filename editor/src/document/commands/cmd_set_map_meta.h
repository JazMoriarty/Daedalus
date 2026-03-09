#pragma once

#include "daedalus/editor/i_command.h"

#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets the map's name and author metadata.  Undoable.
class CmdSetMapMeta final : public ICommand
{
public:
    CmdSetMapMeta(EditMapDocument& doc,
                  std::string      oldName,
                  std::string      newName,
                  std::string      oldAuthor,
                  std::string      newAuthor);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return "Set Map Meta"; }

private:
    EditMapDocument& m_doc;
    std::string      m_oldName;
    std::string      m_newName;
    std::string      m_oldAuthor;
    std::string      m_newAuthor;
};

} // namespace daedalus::editor
