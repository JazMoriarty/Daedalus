// cmd_set_player_start.h
// Command that sets or clears the map's player start position with full undo.

#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/editor_layer.h"  // PlayerStart

#include <optional>
#include <string>

namespace daedalus::editor
{

class EditMapDocument;

/// Sets (or clears) the player start in the document.
/// Passing std::nullopt as newPs clears the player start.
/// execute() selects the player start when newPs is set.
/// undo() restores the previous value.
class CmdSetPlayerStart final : public ICommand
{
public:
    CmdSetPlayerStart(EditMapDocument&            doc,
                      std::optional<PlayerStart>  oldPs,
                      std::optional<PlayerStart>  newPs);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override
    { return "Set Player Start"; }

private:
    EditMapDocument&            m_doc;
    std::optional<PlayerStart>  m_oldPs;
    std::optional<PlayerStart>  m_newPs;

    void apply(const std::optional<PlayerStart>& ps);
};

} // namespace daedalus::editor
