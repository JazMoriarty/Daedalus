#include "daedalus/editor/edit_map_document.h"
#include "daedalus/world/dmap_io.h"
#include "emap_sidecar.h"

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
    m_lights.clear();
    m_entities.clear();
    m_sectorLayers.clear();
    m_playerStart.reset();

    // Reset to one default layer.
    m_layers.clear();
    m_layers.push_back(EditorLayer{"Default", true, false});
    m_activeLayerIdx = 0;

    m_filePath.clear();
    m_dirty         = false;
    m_geometryDirty = true;
    m_entityDirty   = true;   // Force entity cache rebuild to free any stale GPU entries.
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
    m_lights.clear();
    m_entities.clear();
    m_sectorLayers.clear();
    m_playerStart.reset();
    m_layers.clear();
    m_layers.push_back(EditorLayer{"Default", true, false});
    m_activeLayerIdx = 0;
    m_filePath      = path;
    m_dirty         = false;
    m_geometryDirty = true;
    m_entityDirty   = true;   // Force entity cache rebuild so loaded entities appear in the 3D viewport.

    // Load editor-only state from sidecar (.emap).  A missing sidecar is not an
    // error — legacy maps simply use the defaults set above.
    auto emapPath = path;
    emapPath.replace_extension(".emap");
    if (std::filesystem::exists(emapPath))
    {
        const auto emapResult = loadEmap(*this, emapPath);
        if (!emapResult)
            log(std::format("Warning: could not parse '{}' — using defaults.",
                            emapPath.string()));
    }

    log(std::format("Loaded '{}'.", path.string()));
    return true;
}

bool EditMapDocument::saveToFile(const std::filesystem::path& path)
{
    // Write world geometry first.
    auto result = world::saveDmap(m_mapData, path);
    if (!result)
    {
        log(std::format("Failed to save '{}'.", path.string()));
        return false;
    }

    // Write editor-only state to the sidecar.  A sidecar failure surfaces as a
    // save error — silently dropping editor state would cause data loss on reload.
    auto emapPath = path;
    emapPath.replace_extension(".emap");
    const auto emapResult = saveEmap(*this, emapPath);
    if (!emapResult)
    {
        log(std::format("Failed to save editor sidecar '{}'.", emapPath.string()));
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

// ─── Clipboard ───────────────────────────────────────────────────────────────

void EditMapDocument::copySector(world::SectorId id)
{
    if (id >= static_cast<world::SectorId>(m_mapData.sectors.size())) return;

    m_clipboard = m_mapData.sectors[id];
    // Clear portal links — clipboard content is unconnected.
    for (auto& wall : m_clipboard->walls)
        wall.portalSectorId = world::INVALID_SECTOR_ID;

    log(std::format("Copied sector {} to clipboard ({} walls).",
                    id, m_clipboard->walls.size()));
}

// ─── Output log ───────────────────────────────────────────────────────────

void EditMapDocument::log(LogEntry entry)
{
    m_log.push_back(std::move(entry));
    // Cap at 2 000 entries to bound memory usage.
    if (m_log.size() > 2000)
        m_log.erase(m_log.begin());
}

} // namespace daedalus::editor
