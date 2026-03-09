// compound_command.h
// A composite ICommand that executes / undoes a sequence of child commands
// as a single atomic undo step.

#pragma once

#include "daedalus/editor/i_command.h"

#include <memory>
#include <string>
#include <vector>

namespace daedalus::editor
{

/// Groups a sequence of ICommand objects so they execute and undo together as
/// one undo history entry.  execute() calls each child in forward order;
/// undo() calls them in reverse.
class CompoundCommand final : public ICommand
{
public:
    explicit CompoundCommand(std::string                            desc,
                             std::vector<std::unique_ptr<ICommand>> steps);

    void execute() override;
    void undo()    override;

    [[nodiscard]] std::string description() const override { return m_desc; }

private:
    std::string                            m_desc;
    std::vector<std::unique_ptr<ICommand>> m_steps;
};

} // namespace daedalus::editor
