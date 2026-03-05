// i_command.h
// Base interface for all reversible editor commands.
//
// Commands follow the Command pattern: each action is encapsulated as an
// object that knows how to execute itself and how to undo that execution.
// Commands are owned by UndoStack and must not be executed outside of it.

#pragma once

#include <string>

namespace daedalus::editor
{

class ICommand
{
public:
    virtual ~ICommand() = default;

    ICommand(const ICommand&)            = delete;
    ICommand& operator=(const ICommand&) = delete;

    /// Apply the command.  Called once when pushed onto the UndoStack
    /// and again on each redo.
    virtual void execute() = 0;

    /// Reverse the last execute() call.
    virtual void undo() = 0;

    /// Human-readable description shown in the Edit menu ("Undo Draw Sector").
    [[nodiscard]] virtual std::string description() const = 0;

protected:
    ICommand() = default;
};

} // namespace daedalus::editor
