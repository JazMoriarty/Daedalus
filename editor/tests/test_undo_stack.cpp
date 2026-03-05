#include "daedalus/editor/undo_stack.h"
#include "daedalus/editor/i_command.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace daedalus::editor;

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// A simple command that appends/removes integers from a log vector.
struct RecordCmd final : public ICommand
{
    RecordCmd(std::vector<int>& log, int value, std::string desc)
        : m_log(log), m_value(value), m_desc(std::move(desc)) {}

    void execute() override { m_log.push_back(m_value); }
    void undo()    override
    {
        if (!m_log.empty() && m_log.back() == m_value)
            m_log.pop_back();
    }
    [[nodiscard]] std::string description() const override { return m_desc; }

private:
    std::vector<int>& m_log;
    int               m_value;
    std::string       m_desc;
};

// ─── Initial state ────────────────────────────────────────────────────────────

TEST(UndoStack, InitiallyEmpty)
{
    UndoStack s;
    EXPECT_FALSE(s.canUndo());
    EXPECT_FALSE(s.canRedo());
    EXPECT_EQ(s.undoDescription(), "");
    EXPECT_EQ(s.redoDescription(), "");
}

// ─── Push and undo ────────────────────────────────────────────────────────────

TEST(UndoStack, PushExecutesAndEnablesUndo)
{
    UndoStack s;
    std::vector<int> log;

    s.push(std::make_unique<RecordCmd>(log, 42, "Push 42"));

    EXPECT_EQ(log, std::vector<int>{42});
    EXPECT_TRUE(s.canUndo());
    EXPECT_FALSE(s.canRedo());
    EXPECT_EQ(s.undoDescription(), "Push 42");
}

TEST(UndoStack, UndoRestoresState)
{
    UndoStack s;
    std::vector<int> log;

    s.push(std::make_unique<RecordCmd>(log, 1, "A"));
    s.push(std::make_unique<RecordCmd>(log, 2, "B"));

    EXPECT_EQ(log, (std::vector<int>{1, 2}));

    s.undo();
    EXPECT_EQ(log, std::vector<int>{1});
    EXPECT_TRUE(s.canUndo());
    EXPECT_TRUE(s.canRedo());
    EXPECT_EQ(s.undoDescription(), "A");
    EXPECT_EQ(s.redoDescription(), "B");

    s.undo();
    EXPECT_EQ(log, std::vector<int>{});
    EXPECT_FALSE(s.canUndo());
    EXPECT_TRUE(s.canRedo());
}

TEST(UndoStack, UndoNoopWhenEmpty)
{
    UndoStack s;
    EXPECT_NO_THROW(s.undo());  // must not crash
}

// ─── Redo ─────────────────────────────────────────────────────────────────────

TEST(UndoStack, RedoReappliesCommand)
{
    UndoStack s;
    std::vector<int> log;

    s.push(std::make_unique<RecordCmd>(log, 7, "Seven"));
    s.undo();
    EXPECT_EQ(log, std::vector<int>{});

    s.redo();
    EXPECT_EQ(log, std::vector<int>{7});
    EXPECT_TRUE (s.canUndo());
    EXPECT_FALSE(s.canRedo());
}

TEST(UndoStack, RedoNoopAtTip)
{
    UndoStack s;
    std::vector<int> log;
    s.push(std::make_unique<RecordCmd>(log, 1, "X"));
    EXPECT_NO_THROW(s.redo());  // must not crash
    EXPECT_EQ(log, std::vector<int>{1});
}

// ─── Redo truncation on push ──────────────────────────────────────────────────

TEST(UndoStack, PushTruncatesRedoHistory)
{
    UndoStack s;
    std::vector<int> log;

    s.push(std::make_unique<RecordCmd>(log, 1, "A"));
    s.push(std::make_unique<RecordCmd>(log, 2, "B"));
    s.push(std::make_unique<RecordCmd>(log, 3, "C"));

    s.undo();  // undo C
    s.undo();  // undo B
    EXPECT_TRUE(s.canRedo());

    // Pushing a new command must discard the B and C redo history.
    s.push(std::make_unique<RecordCmd>(log, 99, "D"));
    EXPECT_FALSE(s.canRedo());
    EXPECT_EQ(s.undoDescription(), "D");
    EXPECT_EQ(log, (std::vector<int>{1, 99}));
}

// ─── Clear ────────────────────────────────────────────────────────────────────

TEST(UndoStack, ClearResetsState)
{
    UndoStack s;
    std::vector<int> log;

    s.push(std::make_unique<RecordCmd>(log, 5, "E"));
    s.clear();

    EXPECT_FALSE(s.canUndo());
    EXPECT_FALSE(s.canRedo());
}

// ─── Multiple undo/redo cycles ────────────────────────────────────────────────

TEST(UndoStack, MultipleUndoRedoCycles)
{
    UndoStack s;
    std::vector<int> log;

    s.push(std::make_unique<RecordCmd>(log, 10, "Ten"));
    s.push(std::make_unique<RecordCmd>(log, 20, "Twenty"));

    s.undo(); s.undo();
    EXPECT_EQ(log, std::vector<int>{});

    s.redo(); s.redo();
    EXPECT_EQ(log, (std::vector<int>{10, 20}));

    s.undo();
    EXPECT_EQ(s.undoDescription(), "Ten");
    EXPECT_EQ(s.redoDescription(), "Twenty");
}
