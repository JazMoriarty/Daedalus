// edit_map_document.h
// The central model of the editor.  Owns the map data, undo/redo history,
// selection state, and the output log.
//
// All map mutations go through pushCommand() so they are undoable.

#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/undo_stack.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/world/map_data.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace daedalus::editor
{

class EditMapDocument
{
public:
    EditMapDocument();
    ~EditMapDocument() = default;

    EditMapDocument(const EditMapDocument&)            = delete;
    EditMapDocument& operator=(const EditMapDocument&) = delete;

    // ─── Map data ─────────────────────────────────────────────────────────────

    [[nodiscard]] world::WorldMapData&       mapData()       noexcept { return m_mapData; }
    [[nodiscard]] const world::WorldMapData& mapData() const noexcept { return m_mapData; }

    // ─── Undo / redo ──────────────────────────────────────────────────────────

    /// Execute cmd and push it onto the undo stack.  Marks the document dirty.
    void pushCommand(std::unique_ptr<ICommand> cmd);

    /// Undo the most-recently pushed command.
    void undo();

    /// Redo the next command.
    void redo();

    [[nodiscard]] UndoStack&       undoStack()       noexcept { return m_undoStack; }
    [[nodiscard]] const UndoStack& undoStack() const noexcept { return m_undoStack; }

    // ─── Selection ────────────────────────────────────────────────────────────

    [[nodiscard]] SelectionState&       selection()       noexcept { return m_selection; }
    [[nodiscard]] const SelectionState& selection() const noexcept { return m_selection; }

    // ─── Dirty state ──────────────────────────────────────────────────────────

    /// True when there are unsaved changes.
    [[nodiscard]] bool isDirty()         const noexcept { return m_dirty; }

    /// True when the sector geometry has changed and the 3D viewport must
    /// re-tessellate.  Cleared by the 3D viewport after re-tessellation.
    [[nodiscard]] bool isGeometryDirty() const noexcept { return m_geometryDirty; }

    void clearGeometryDirty() noexcept { m_geometryDirty = false; }

    // ─── File I/O ─────────────────────────────────────────────────────────────

    /// Reset to an empty map with default settings.
    void newMap();

    /// Load from a binary .dmap file.  Returns true on success.
    [[nodiscard]] bool loadFromFile(const std::filesystem::path& path);

    /// Save to a binary .dmap file and update filePath().  Returns true on success.
    [[nodiscard]] bool saveToFile(const std::filesystem::path& path);

    /// Save to filePath() (no-op if path is empty).  Returns true on success.
    [[nodiscard]] bool saveToCurrentPath();

    [[nodiscard]] const std::filesystem::path& filePath() const noexcept { return m_filePath; }

    // ─── Output log ───────────────────────────────────────────────────────────

    /// Append a message to the output log (consumed by the Output panel).
    void log(std::string msg);

    [[nodiscard]] const std::vector<std::string>& logMessages() const noexcept
    {
        return m_log;
    }

    /// Mark the document as having unsaved changes and flag geometry for
    /// re-tessellation.  Call this when directly mutating map data outside
    /// of pushCommand() (e.g. live vertex drag).
    void markDirty() noexcept { m_dirty = true; m_geometryDirty = true; }

private:
    world::WorldMapData   m_mapData;
    UndoStack             m_undoStack;
    SelectionState        m_selection;
    std::filesystem::path m_filePath;

    bool m_dirty         = false;
    bool m_geometryDirty = false;

    std::vector<std::string> m_log;
};

} // namespace daedalus::editor
