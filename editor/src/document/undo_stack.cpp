#include "daedalus/editor/undo_stack.h"

namespace daedalus::editor
{

void UndoStack::push(std::unique_ptr<ICommand> cmd)
{
    // Truncate any redo history beyond the current cursor.
    m_stack.resize(m_top);

    cmd->execute();
    m_stack.push_back(std::move(cmd));
    m_top = m_stack.size();
}

void UndoStack::undo()
{
    if (!canUndo()) return;
    --m_top;
    m_stack[m_top]->undo();
}

void UndoStack::redo()
{
    if (!canRedo()) return;
    m_stack[m_top]->execute();
    ++m_top;
}

void UndoStack::clear() noexcept
{
    m_stack.clear();
    m_top = 0;
}

bool UndoStack::canUndo() const noexcept
{
    return m_top > 0;
}

bool UndoStack::canRedo() const noexcept
{
    return m_top < m_stack.size();
}

std::string UndoStack::undoDescription() const noexcept
{
    return canUndo() ? m_stack[m_top - 1]->description() : std::string{};
}

std::string UndoStack::redoDescription() const noexcept
{
    return canRedo() ? m_stack[m_top]->description() : std::string{};
}

} // namespace daedalus::editor
