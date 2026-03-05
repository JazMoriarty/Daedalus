// undo_stack.h
// Linear undo/redo stack that owns ICommand objects.
//
// Design:
//   • The stack is a vector; m_top is the index of the next free slot.
//   • push() truncates any redo history, calls execute(), and appends.
//   • undo() decrements m_top and calls undo() on the command.
//   • redo() calls execute() on the command at m_top and increments m_top.

#pragma once

#include "daedalus/editor/i_command.h"

#include <memory>
#include <string>
#include <vector>

namespace daedalus::editor
{

class UndoStack
{
public:
    UndoStack()  = default;
    ~UndoStack() = default;

    UndoStack(const UndoStack&)            = delete;
    UndoStack& operator=(const UndoStack&) = delete;

    // ─── Mutators ─────────────────────────────────────────────────────────────

    /// Execute cmd and push it.  Any redo history is discarded.
    void push(std::unique_ptr<ICommand> cmd);

    /// Undo the most-recently executed command.  No-op if canUndo() is false.
    void undo();

    /// Re-execute the next redo-able command.  No-op if canRedo() is false.
    void redo();

    /// Discard all commands and reset to empty.
    void clear() noexcept;

    // ─── Queries ──────────────────────────────────────────────────────────────

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;

    /// Description of the command that would be undone (empty if canUndo is false).
    [[nodiscard]] std::string undoDescription() const noexcept;

    /// Description of the command that would be redone (empty if canRedo is false).
    [[nodiscard]] std::string redoDescription() const noexcept;

private:
    std::vector<std::unique_ptr<ICommand>> m_stack;
    std::size_t                            m_top = 0;
};

} // namespace daedalus::editor
