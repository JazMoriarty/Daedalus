// edit_map_document.h
// The central model of the editor.  Owns the map data, undo/redo history,
// selection state, and the output log.
//
// All map mutations go through pushCommand() so they are undoable.

#pragma once

#include "daedalus/editor/i_command.h"
#include "daedalus/editor/undo_stack.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/editor/scene_settings.h"
#include "daedalus/editor/render_settings_data.h"
#include "daedalus/editor/light_def.h"
#include "daedalus/editor/entity_def.h"
#include "daedalus/editor/editor_layer.h"
#include "daedalus/editor/prefab_def.h"
#include "daedalus/world/map_data.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace daedalus::editor
{

// ─── LogEntry ─────────────────────────────────────────────────────────────────
// A single output log message.  When jumpTo is set, OutputLog renders a
// clickable arrow button that applies the stored selection to the document.

struct LogEntry
{
    std::string                  message;
    std::optional<SelectionState> jumpTo;  ///< If set, clicking navigates to this element.

    /// Convenience: plain-string entry with no navigation target.
    explicit LogEntry(std::string msg) noexcept
        : message(std::move(msg)), jumpTo(std::nullopt) {}

    LogEntry(std::string msg, std::optional<SelectionState> target) noexcept
        : message(std::move(msg)), jumpTo(std::move(target)) {}
};

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

    /// True when entity data has changed and the 3D viewport must
    /// rebuild its entity GPU cache.  Cleared by the 3D viewport after rebuild.
    [[nodiscard]] bool isEntityDirty() const noexcept { return m_entityDirty; }

    void clearEntityDirty() noexcept { m_entityDirty = false; }

    /// Mark entity data dirty (unsaved changes + entity GPU cache invalid).
    /// Called by entity commands in execute() / undo().
    void markEntityDirty() noexcept { m_dirty = true; m_entityDirty = true; }

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

    // ─── Clipboard ────────────────────────────────────────────────────────────

    /// Copy the sector at `id` to the internal clipboard, clearing portal links.
    /// No-op if `id` is out of range.
    void copySector(world::SectorId id);

    [[nodiscard]] bool                  hasClipboard() const noexcept { return m_clipboard.has_value(); }
    [[nodiscard]] const world::Sector*  clipboard()    const noexcept
    {
        return m_clipboard ? &*m_clipboard : nullptr;
    }

    // ─── Scene settings ───────────────────────────────────────────────────────

    [[nodiscard]] SceneSettings&       sceneSettings()       noexcept { return m_sceneSettings; }
    [[nodiscard]] const SceneSettings& sceneSettings() const noexcept { return m_sceneSettings; }

    // ─── Render settings (post-processing) ───────────────────────────────────

    [[nodiscard]] RenderSettingsData&       renderSettings()       noexcept { return m_renderSettings; }
    [[nodiscard]] const RenderSettingsData& renderSettings() const noexcept { return m_renderSettings; }

    // ─── Lights ───────────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<LightDef>&       lights()       noexcept { return m_lights; }
    [[nodiscard]] const std::vector<LightDef>& lights() const noexcept { return m_lights; }

    // ─── Entities ───────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<EntityDef>&       entities()       noexcept { return m_entities; }
    [[nodiscard]] const std::vector<EntityDef>& entities() const noexcept { return m_entities; }

    // ─── Prefab library ──────────────────────────────────────────────────────

    [[nodiscard]] std::vector<PrefabDef>&       prefabs()       noexcept { return m_prefabs; }
    [[nodiscard]] const std::vector<PrefabDef>& prefabs() const noexcept { return m_prefabs; }

    // ─── New-map defaults (applied by DrawSectorTool) ─────────────────────────────

    [[nodiscard]] float  defaultFloorHeight() const noexcept { return m_defaultFloorH; }
    [[nodiscard]] float  defaultCeilHeight()  const noexcept { return m_defaultCeilH;  }
    [[nodiscard]] float  gravity()            const noexcept { return m_gravity;        }
    [[nodiscard]] const std::string& skyPath()const noexcept { return m_skyPath;        }

    void setDefaultFloorHeight(float v)       noexcept { m_defaultFloorH = v; }
    void setDefaultCeilHeight (float v)       noexcept { m_defaultCeilH  = v; }
    void setGravity           (float v)       noexcept { m_gravity        = v; }
    void setSkyPath           (std::string s)          { m_skyPath        = std::move(s); }

    // ─── Layers ───────────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<EditorLayer>&       layers()       noexcept { return m_layers; }
    [[nodiscard]] const std::vector<EditorLayer>& layers() const noexcept { return m_layers; }

    [[nodiscard]] uint32_t activeLayerIdx() const noexcept { return m_activeLayerIdx; }
    void setActiveLayerIdx(uint32_t idx) noexcept
    {
        if (idx < m_layers.size()) m_activeLayerIdx = idx;
    }

    /// Returns the layer index for sector at position `sectorIdx`.
    /// Returns 0 (default layer) if the index is out of range.
    [[nodiscard]] uint32_t sectorLayerIndex(std::size_t sectorIdx) const noexcept
    {
        return (sectorIdx < m_sectorLayers.size()) ? m_sectorLayers[sectorIdx] : 0u;
    }

    /// Called by DrawSectorTool after pushing CmdDrawSector to register the
    /// new sector on the active layer.
    void appendSectorLayer()
    {
        m_sectorLayers.push_back(m_activeLayerIdx);
    }

    /// Replace the entire sector-layer parallel vector (used by sidecar load).
    void resetSectorLayers(std::vector<uint32_t> sl) noexcept
    {
        m_sectorLayers = std::move(sl);
    }

    // ─── Asset root ───────────────────────────────────────────────────────────
    // Path to the root directory that the MaterialCatalog should scan.
    // Stored in the .emap sidecar as "asset_root" so it survives project reload.

    [[nodiscard]] const std::string& assetRoot() const noexcept { return m_assetRoot; }
    void setAssetRoot(std::string root) { m_assetRoot = std::move(root); }

    // ─── Player start ────────────────────────────────────────────────────────────

    [[nodiscard]] std::optional<PlayerStart>&       playerStart()       noexcept { return m_playerStart; }
    [[nodiscard]] const std::optional<PlayerStart>& playerStart() const noexcept { return m_playerStart; }

    void setPlayerStart(PlayerStart ps) noexcept { m_playerStart = std::move(ps); }
    void clearPlayerStart()             noexcept { m_playerStart.reset(); }

    // ─── Output log ───────────────────────────────────────────────────────────

    /// Append a log entry (with optional click-to-navigate selection).
    void log(LogEntry entry);

    /// Convenience overload — plain string with no navigation target.
    void log(std::string msg) { log(LogEntry{std::move(msg)}); }

    [[nodiscard]] const std::vector<LogEntry>& logMessages() const noexcept
    {
        return m_log;
    }

    /// Clear all output log entries.
    void clearLog() noexcept { m_log.clear(); }

    /// Mark the document as having unsaved changes and flag geometry for
    /// re-tessellation.  Call this when directly mutating map data outside
    /// of pushCommand() (e.g. live vertex drag).
    void markDirty() noexcept { m_dirty = true; m_geometryDirty = true; }

private:
    world::WorldMapData          m_mapData;
    UndoStack                    m_undoStack;
    SelectionState               m_selection;
    SceneSettings                m_sceneSettings;
    RenderSettingsData           m_renderSettings;
    std::vector<LightDef>        m_lights;
    std::vector<EntityDef>       m_entities;
    std::vector<PrefabDef>       m_prefabs;

    // New-map defaults
    float       m_defaultFloorH = 0.0f;
    float       m_defaultCeilH  = 4.0f;
    float       m_gravity       = 9.81f;
    std::string m_skyPath;

    // Layers
    uint32_t                m_activeLayerIdx = 0;
    std::vector<EditorLayer> m_layers;
    std::vector<uint32_t>   m_sectorLayers;  ///< Parallel to WorldMapData::sectors.

    // Player start
    std::optional<PlayerStart> m_playerStart;

    std::optional<world::Sector> m_clipboard;
    std::filesystem::path        m_filePath;
    std::string                  m_assetRoot;

    bool m_dirty         = false;
    bool m_geometryDirty = false;
    bool m_entityDirty   = false;

    std::vector<LogEntry> m_log;
};

} // namespace daedalus::editor
