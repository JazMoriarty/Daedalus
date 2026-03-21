// cmd_set_player_start.cpp

#include "cmd_set_player_start.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/selection_state.h"

namespace daedalus::editor
{

CmdSetPlayerStart::CmdSetPlayerStart(EditMapDocument&            doc,
                                     std::optional<PlayerStart>  oldPs,
                                     std::optional<PlayerStart>  newPs)
    : m_doc(doc)
    , m_oldPs(std::move(oldPs))
    , m_newPs(std::move(newPs))
{}

void CmdSetPlayerStart::execute() { apply(m_newPs); }
void CmdSetPlayerStart::undo()    { apply(m_oldPs); }

void CmdSetPlayerStart::apply(const std::optional<PlayerStart>& ps)
{
    SelectionState& sel = m_doc.selection();
    if (ps.has_value())
    {
        m_doc.setPlayerStart(*ps);
        sel.clear();
        sel.items.push_back({SelectionType::PlayerStart,
                             world::INVALID_SECTOR_ID, 0});
    }
    else
    {
        m_doc.clearPlayerStart();
        if (sel.hasSingleOf(SelectionType::PlayerStart))
            sel.clear();
    }
}

} // namespace daedalus::editor
