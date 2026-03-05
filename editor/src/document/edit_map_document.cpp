#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/dmap_io.h"

#include <format>

namespace daedalus::editor
{

EditMapDocument::EditMapDocument()
{
    newMap();
}

// ─── Undo / redo ──────────────────────────────────────────────────────────────

void EditMapDocument::pushCommand(std::unique_ptr<ICommand> cmd)
{
    m_undoStack.push(std::move(cmd));
    markDirty();
}

void EditMapDocument::undo()
{
    if (!m_undoStack.canUndo()) return;
    m_undoStack.undo();
    markDirty();
}

void EditMapDocument::redo()
{
    if (!m_undoStack.canRedo()) return;
    m_undoStack.redo();
    markDirty();
}

// ─── File I/O ─────────────────────────────────────────────────────────────────

void EditMapDocument::newMap()
{
    m_mapData        = {};
    m_mapData.name   = "Untitled";
    m_mapData.author = "";
    m_undoStack.clear();
    m_selection.clear();
    m_filePath.clear();
    m_dirty         = false;
    m_geometryDirty = true;
    log("New map created.");
}

bool EditMapDocument::loadFromFile(const std::filesystem::path& path)
{
    auto result = world::loadDmap(path);
    if (!result)
    {
        log(std::format("Failed to load '{}'.", path.string()));
        return false;
    }
    m_mapData       = std::move(*result);
    m_undoStack.clear();
    m_selection.clear();
    m_filePath      = path;
    m_dirty         = false;
    m_geometryDirty = true;
    log(std::format("Loaded '{}'.", path.string()));
    return true;
}

bool EditMapDocument::saveToFile(const std::filesystem::path& path)
{
    auto result = world::saveDmap(m_mapData, path);
    if (!result)
    {
        log(std::format("Failed to save '{}'.", path.string()));
        return false;
    }
    m_filePath = path;
    m_dirty    = false;
    log(std::format("Saved '{}'.", path.string()));
    return true;
}

bool EditMapDocument::saveToCurrentPath()
{
    if (m_filePath.empty()) return false;
    return saveToFile(m_filePath);
}

// ─── Output log ───────────────────────────────────────────────────────────────

void EditMapDocument::log(std::string msg)
{
    m_log.push_back(std::move(msg));
    // Cap at 2 000 entries to bound memory usage.
    if (m_log.size() > 2000)
        m_log.erase(m_log.begin());
}

} // namespace daedalus::editor
