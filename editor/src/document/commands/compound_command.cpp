#include "daedalus/editor/compound_command.h"

namespace daedalus::editor
{

CompoundCommand::CompoundCommand(std::string                            desc,
                                 std::vector<std::unique_ptr<ICommand>> steps)
    : m_desc(std::move(desc)), m_steps(std::move(steps))
{}

void CompoundCommand::execute()
{
    for (auto& cmd : m_steps)
        cmd->execute();
}

void CompoundCommand::undo()
{
    for (auto it = m_steps.rbegin(); it != m_steps.rend(); ++it)
        (*it)->undo();
}

} // namespace daedalus::editor
