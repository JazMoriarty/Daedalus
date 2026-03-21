// DaedalusEdit — entry point (Objective-C++ / ARC)
//
// Window management: SDL3 with SDL_WINDOW_METAL.
// ImGui rendering : imgui_impl_sdl3 + imgui_impl_metal.
// The main CAMetalLayer is obtained from SDL_Metal_GetLayer and used
// directly for ImGui — no RHI ISwapchain is created for the editor window.
// The RHI device/queue are used only for the 3D viewport offscreen swapchain.

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_metal.h"

#include "daedalus/core/create_platform.h"
#include "daedalus/render/create_render_device.h"
#include "daedalus/render/i_asset_loader.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_command_queue.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/i_editor_tool.h"

#include "document/commands/cmd_delete_sector.h"
#include "document/commands/cmd_duplicate_sector.h"
#include "document/map_doctor.h"
#include "document/commands/cmd_paste_sector.h"
#include "document/commands/cmd_split_wall.h"
#include "tools/select_tool.h"
#include "tools/draw_sector_tool.h"
#include "tools/vertex_tool.h"
#include "document/commands/cmd_rotate_sector.h"
#include "panels/viewport_2d.h"
#include "panels/viewport_3d.h"
#include "panels/property_inspector.h"
#include "panels/output_log.h"
#include "panels/render_settings_panel.h"
#include "panels/object_browser_panel.h"
#include "panels/layers_panel.h"
#include "panels/help_panel.h"
#include "panels/floor_layer_panel.h"
#include "panels/staircase_generator_panel.h"
#include "document/commands/cmd_place_light.h"
#include "document/commands/cmd_delete_light.h"
#include "document/commands/cmd_place_entity.h"
#include "document/commands/cmd_delete_entity.h"
#include "document/commands/cmd_set_player_start.h"
#include "catalog/material_catalog.h"
#include "catalog/model_catalog.h"
#include "panels/asset_browser_panel.h"
#include "panels/splash_screen.h"
#include "panels/normal_map_generator_panel.h"
#include "daedalus/world/dlevel_io.h"

// stb_image (declaration only — implementation compiled once in asset_loader.cpp)
#include "stb_image.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace daedalus;
using namespace daedalus::editor;

// ─── Helpers ───────────────────────────────────────────────────────────────────
// Returns the directory containing runtime resources (assets, shaders, etc.).
// This works for both standalone executables and app bundles.

static std::string resourceDir()
{
    auto platform = createPlatform();
    return platform ? platform->getResourceDir() : ".";
}

// ─── Saved window geometry ────────────────────────────────────────────────────
// Read before the window is created so it can be sized and positioned
// correctly on the first frame — no flicker.

struct SavedWindowGeometry
{
    int  w           = 1440;
    int  h           = 900;
    bool hasPosition = false;
    int  x           = 0;
    int  y           = 0;
};

[[nodiscard]] static SavedWindowGeometry readSavedWindowGeometry()
{
    SavedWindowGeometry g;
    std::ifstream f("daedalusedit.ini");
    if (!f.is_open()) return g;

    bool gotX = false, gotY = false;
    std::string line;
    while (std::getline(f, line))
    {
        int v;
        if      (sscanf(line.c_str(), "windowX=%d", &v) == 1) { g.x = v; gotX = true; }
        else if (sscanf(line.c_str(), "windowY=%d", &v) == 1) { g.y = v; gotY = true; }
        else if (sscanf(line.c_str(), "windowW=%d", &v) == 1 && v > 400) g.w = v;
        else if (sscanf(line.c_str(), "windowH=%d", &v) == 1 && v > 300) g.h = v;
    }
    g.hasPosition = gotX && gotY;
    return g;
}

// ─── File dialog ─────────────────────────────────────────────────────────────
// SDL3's sendEvent: override at the NSApplication level intercepts mouseDown
// events for ALL windows — including NSOpenPanel sheets shown by SDL3's own
// SDL_ShowOpenFileDialog.  The result is that single-clicking file items never
// registers regardless of whether the panel is modal or non-modal.  Scroll and
// mouse-up events still work, but the panel is effectively un-clickable.
//
// Fix: run the dialog in a completely separate process via osascript.  The dialog
// lives inside osascript's NSApplication, which has no SDL3 event filter, so
// clicks work unconditionally.  A detached std::thread blocks on popen() until
// the user responds; the result is delivered via FileDialogState using the same
// ready/result protocol used throughout the editor.

enum class FileDialogPurpose
{
    None,
    OpenMap,
    SaveMap,
    OpenLUT,
    OpenAssetRoot,
};

struct FileDialogState
{
    FileDialogPurpose purpose = FileDialogPurpose::None;
    std::string       result;          ///< Written before ready is set.
    std::atomic<bool> ready{false};
};

/// Spawn a file-picker dialog in a detached background thread using osascript.
/// \p appleScript is a one-line AppleScript expression that evaluates to a
/// POSIX path string (e.g. "POSIX path of (choose file)").  Single quotes
/// inside the script are automatically escaped for the shell.
///
/// The thread blocks on popen() until the user responds, then stores the
/// result path (empty string on cancel) and sets \p state->ready with
/// release ordering so the main loop can safely read result with acquire.
static void spawnFileDialog(FileDialogState* state, std::string appleScript)
{
    // Escape any single quotes in the script so the outer shell quoting is safe.
    for (std::size_t pos = 0;
         (pos = appleScript.find('\'', pos)) != std::string::npos; )
    {
        appleScript.replace(pos, 1, "'\"'\"'");
        pos += 5;
    }
    const std::string cmd = "osascript -e '" + appleScript + "' 2>/dev/null";

    std::thread([state, cmd]() {
        FILE* f = popen(cmd.c_str(), "r");
        std::string result;
        if (f)
        {
            char buf[4096] = {};
            if (fgets(buf, sizeof(buf), f))
                result = buf;
            pclose(f);
            while (!result.empty() &&
                   (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
        }
        state->result = std::move(result);
        state->ready.store(true, std::memory_order_release);
    }).detach();
}

// ─── Initial docking layout ───────────────────────────────────────────────────

static void buildDefaultDockLayout(ImGuiID dockspaceId)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_None);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    // Split: left 35% = 2D viewport, right 65% = rest.
    ImGuiID dockLeft, dockRight;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.35f,
                                 &dockLeft, &dockRight);

    // Split right: far-right 25% = Properties, remainder = 3D + Output.
    ImGuiID dockMid, dockProps;
    ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Right, 0.28f,
                                 &dockProps, &dockMid);

    // Split mid: bottom 25% = Output, top 75% = 3D viewport.
    ImGuiID dock3D, dockOutput;
    ImGui::DockBuilderSplitNode(dockMid, ImGuiDir_Down, 0.25f,
                                 &dockOutput, &dock3D);

    ImGui::DockBuilderDockWindow("2D Viewport",     dockLeft);
    ImGui::DockBuilderDockWindow("3D Viewport",     dock3D);
    ImGui::DockBuilderDockWindow("Properties",      dockProps);
    ImGui::DockBuilderDockWindow("Render Settings", dockProps);  // tab-docked with Properties
    ImGui::DockBuilderDockWindow("Object Browser",  dockProps);  // tab-docked with Properties
    ImGui::DockBuilderDockWindow("Asset Browser",   dockProps);  // tab-docked with Properties
    ImGui::DockBuilderDockWindow("Output",          dockOutput);
    ImGui::DockBuilderDockWindow("Layers",          dockOutput); // tab-docked with Output

    ImGui::DockBuilderFinish(dockspaceId);
}

// ─── Menu bar ─────────────────────────────────────────────────────────────────

enum class ActiveTool { Select, DrawSector, Vertex };

static void drawMenuBar(EditMapDocument& doc,
                         ActiveTool&       activeTool,
                         SelectTool&       selectTool,
                         DrawSectorTool&   drawSectorTool,
                         VertexTool&       vertexTool,
                         IEditorTool*&     activeToolPtr,
                         float             gridStep,
                         glm::vec2         lastMouseMapPos,
                         bool&             openNewMapDlg,
                         HelpPanel&        helpPanel,
                         FileDialogState&  dlgState,
                         bool&             show2DViewport,
                         bool&             show3DViewport,
                         bool&             showPropertyInspector,
                         bool&             showObjectBrowser,
                         bool&             showAssetBrowser,
                         bool&             showLayers,
                         bool&             showOutputLog,
                         bool&             showRenderSettings,
                         bool&             showNormalMapGen,
                         bool&             showFloorLayers,
                         bool&             showStaircaseGen,
                         bool&             showHelp)
{
    if (ImGui::BeginMainMenuBar())
    {
        // ── File ──────────────────────────────────────────────────────────────
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Cmd+N"))
                openNewMapDlg = true;

            if (ImGui::MenuItem("Open\xe2\x80\xa6", "Cmd+O"))
            {
                const std::string mapsDir = resourceDir() + "/assets/maps";
                spawnFileDialog(&dlgState,
                    "POSIX path of (choose file default location (POSIX file \"" + mapsDir + "\"))");
                dlgState.purpose = FileDialogPurpose::OpenMap;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Set Asset Root\xe2\x80\xa6"))
            {
                spawnFileDialog(&dlgState, "POSIX path of (choose folder)");
                dlgState.purpose = FileDialogPurpose::OpenAssetRoot;
            }

            ImGui::Separator();
            const bool hasSavePath = !doc.filePath().empty();
            if (ImGui::MenuItem("Save", "Cmd+S", false, hasSavePath))
                (void)doc.saveToCurrentPath();  // failure already logged to OutputLog

            if (ImGui::MenuItem("Save As\xe2\x80\xa6", "Cmd+Shift+S"))
            {
                const std::string defaultName = doc.filePath().empty()
                    ? "untitled.dmap"
                    : doc.filePath().filename().string();
                const std::string mapsDir = resourceDir() + "/assets/maps";
                spawnFileDialog(&dlgState,
                    "POSIX path of (choose file name default name \"" + defaultName +
                    "\" default location (POSIX file \"" + mapsDir + "\"))");
                dlgState.purpose = FileDialogPurpose::SaveMap;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Cmd+Q"))
            {
                SDL_Event quitEvent{};
                quitEvent.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quitEvent);
            }

            ImGui::EndMenu();
        }

        // ── Edit ──────────────────────────────────────────────────────────────
        if (ImGui::BeginMenu("Edit"))
        {
            const bool canUndo = doc.undoStack().canUndo();
            const bool canRedo = doc.undoStack().canRedo();

            const std::string undoLabel = canUndo
                ? ("Undo " + doc.undoStack().undoDescription())
                : "Undo";
            const std::string redoLabel = canRedo
                ? ("Redo " + doc.undoStack().redoDescription())
                : "Redo";

            if (ImGui::MenuItem(undoLabel.c_str(), "Cmd+Z", false, canUndo))
                doc.undo();
            if (ImGui::MenuItem(redoLabel.c_str(), "Cmd+Shift+Z", false, canRedo))
                doc.redo();

            ImGui::Separator();

            const SelectionState& editSel    = doc.selection();
            const bool            hasSectorSel =
                editSel.uniformType() == SelectionType::Sector &&
                !editSel.items.empty();

            if (ImGui::MenuItem("Copy", "Cmd+C", false, hasSectorSel))
                doc.copySector(editSel.items[0].sectorId);

            if (ImGui::MenuItem("Paste", "Cmd+V", false, doc.hasClipboard()))
                doc.pushCommand(std::make_unique<CmdPasteSector>(
                    doc, *doc.clipboard(),
                    glm::vec2{gridStep, gridStep}));

            if (ImGui::MenuItem("Duplicate", "Cmd+D", false, hasSectorSel))
                doc.pushCommand(std::make_unique<CmdDuplicateSector>(
                    doc,
                    editSel.items[0].sectorId,
                    glm::vec2{gridStep, gridStep}));

            ImGui::EndMenu();
        }

        // ── Window ────────────────────────────────────────────────────────────
        if (ImGui::BeginMenu("Window"))
        {
            ImGui::MenuItem("2D Viewport", nullptr, &show2DViewport);
            ImGui::MenuItem("3D Viewport", nullptr, &show3DViewport);
            ImGui::MenuItem("Property Inspector", nullptr, &showPropertyInspector);
            ImGui::MenuItem("Object Browser", nullptr, &showObjectBrowser);
            ImGui::MenuItem("Asset Browser", nullptr, &showAssetBrowser);
            ImGui::MenuItem("Layers", nullptr, &showLayers);
            ImGui::MenuItem("Output Log", nullptr, &showOutputLog);
            ImGui::Separator();
            ImGui::MenuItem("Render Settings", nullptr, &showRenderSettings);
            ImGui::MenuItem("Normal Map Generator", nullptr, &showNormalMapGen);
            ImGui::MenuItem("Floor Layers", nullptr, &showFloorLayers);
            ImGui::MenuItem("Staircase Generator", nullptr, &showStaircaseGen);
            ImGui::Separator();
            ImGui::MenuItem("Help", "F1", &showHelp);
            ImGui::EndMenu();
        }

        // ── Help ──────────────────────────────────────────────────────────────
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Help", "F1"))
                helpPanel.setOpen(true);
            ImGui::EndMenu();
        }

        // ── Tools ──────────────────────────────────────────────────────────────
        if (ImGui::BeginMenu("Tools"))
        {
            if (ImGui::MenuItem("Map Doctor", "Cmd+Shift+M"))
                runMapDoctor(doc);

            if (ImGui::MenuItem("Place Light Here", "P"))
            {
                LightDef ld;
                ld.position = {lastMouseMapPos.x, 2.0f, lastMouseMapPos.y};
                doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Select",      "S",
                                activeTool == ActiveTool::Select))
            {
                if (activeTool != ActiveTool::Select)
                {
                    activeToolPtr->deactivate(doc);
                    activeTool    = ActiveTool::Select;
                    activeToolPtr = &selectTool;
                    activeToolPtr->activate(doc);
                }
            }
            if (ImGui::MenuItem("Draw Sector", "D",
                                activeTool == ActiveTool::DrawSector))
            {
                if (activeTool != ActiveTool::DrawSector)
                {
                    activeToolPtr->deactivate(doc);
                    activeTool    = ActiveTool::DrawSector;
                    activeToolPtr = &drawSectorTool;
                    activeToolPtr->activate(doc);
                }
            }
            if (ImGui::MenuItem("Vertex", "V",
                                activeTool == ActiveTool::Vertex))
            {
                if (activeTool != ActiveTool::Vertex)
                {
                    activeToolPtr->deactivate(doc);
                    activeTool    = ActiveTool::Vertex;
                    activeToolPtr = &vertexTool;
                    activeToolPtr->activate(doc);
                }
            }

            ImGui::EndMenu();
        }

        // ── Status bar ────────────────────────────────────────────────────────
        const char* toolName =
            activeTool == ActiveTool::DrawSector ? "Draw Sector" :
            activeTool == ActiveTool::Vertex     ? "Vertex"      : "Select";
        ImGui::Separator();
        ImGui::Text("Tool: %s", toolName);

        if (doc.isDirty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("*");
        }

        ImGui::EndMainMenuBar();
    }
}

// ─── Editor settings persisted across sessions ────────────────────────────────
// Stored in the [DaedalusEdit] section of daedalusedit.ini via ImGui's
// settings-handler API.  pendingApply is set on the first NewFrame() when
// ImGui loads the INI; the main loop applies it that same frame.

struct EditorPersistState
{
    std::string  assetRoot;            // last-used asset root directory
    bool         pendingApply = false; // true until the root has been applied
    SDL_Window*  sdlWindow    = nullptr; // set after creation; queried by WriteAllFn
};

// ─── New Map dialog state ────────────────────────────────────────────────────

struct NewMapDialogState
{
    bool  open     = false;
    char  name[256]   = "Untitled";
    char  author[256] = "";
    float floorH      = 0.0f;
    float ceilH       = 4.0f;
    float gravity     = 9.81f;
    char  skyPath[512] = "";
    float ambientColor[3] = {0.05f, 0.05f, 0.08f};
};

// ─── Entry point ─────────────────────────────────────────────────────────────

int main(int /*argc*/, char* /*argv*/[])
{
    // ── SDL3 initialisation ───────────────────────────────────────────────────
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Read saved window geometry before creating the window so it appears
    // at the right size and position with no visible repositioning.
    const SavedWindowGeometry savedWin = readSavedWindowGeometry();

    SDL_Window* window = SDL_CreateWindow("DaedalusEdit",
                                          savedWin.w, savedWin.h,
                                          SDL_WINDOW_METAL    |
                                          SDL_WINDOW_RESIZABLE|
                                          SDL_WINDOW_HIDDEN);
    if (!window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (savedWin.hasPosition)
        SDL_SetWindowPosition(window, savedWin.x, savedWin.y);
    SDL_ShowWindow(window);

    // ── Metal setup ───────────────────────────────────────────────────────────
    // Create a Metal view for the editor window (used by ImGui directly).
    SDL_MetalView metalView = SDL_Metal_CreateView(window);
    CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(metalView);

    // ── RHI device + queue ────────────────────────────────────────────────────
    // Obtained separately from the SDL Metal view — used only for the 3D
    // viewport offscreen rendering.  The editor window never uses an RHI swapchain.
    auto device      = rhi::createRenderDevice();
    auto queue       = device->createCommandQueue("Editor Queue");
    auto assetLoader = render::makeAssetLoader();

    // Wire the RHI device to the Metal layer so ImGui can share the same GPU.
    id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device->nativeDevice();
    id<MTLCommandQueue> mtlQueue =
        (__bridge id<MTLCommandQueue>)queue->nativeHandle();

    metalLayer.device               = mtlDevice;
    metalLayer.pixelFormat          = MTLPixelFormatBGRA8Unorm;
    // displaySyncEnabled = NO: prevents [CAMetalLayer nextDrawable] from blocking on
    // vsync.  With the default of YES the Window Server's cursor-compositing work
    // (active when the cursor is over the Metal window) pushes frame time past the
    // vsync boundary, stalling nextDrawable 16-32 ms.  During that stall mouse-motion
    // events accumulate and land all at once on the next frame → stutter.  With sync
    // disabled, drawables are released as soon as the GPU finishes; the main loop
    // applies its own ~60 fps cap via SDL_DelayNS to avoid over-driving the GPU.
    metalLayer.maximumDrawableCount = 2;
    metalLayer.displaySyncEnabled   = NO;

    // ── Working directory
    // Ensure relative paths (asset paths, INI, shaders) resolve from the
    // resource directory regardless of how the editor was launched.
    {
        std::error_code ec;
        std::filesystem::current_path(std::filesystem::path(resourceDir()), ec);
        // Non-fatal: if this fails, relative paths may not resolve correctly.
    }

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename  = "daedalusedit.ini";

    // ── Persist editor settings in daedalusedit.ini ───────────────────────────
    // Must be registered before the first NewFrame(), which is when ImGui reads
    // the INI file and calls our ReadLineFn.  State is accessed through UserData
    // (stateless lambdas convertible to plain function pointers).
    EditorPersistState persistState;
    // Ensure the first-frame block always fires so we can apply the default
    // asset root even when no INI exists yet.
    persistState.pendingApply = true;
    persistState.sdlWindow    = window;
    {
        ImGuiSettingsHandler h = {};
        h.TypeName   = "DaedalusEdit";
        h.TypeHash   = ImHashStr("DaedalusEdit");
        h.UserData   = &persistState;
        h.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void*
        {
            return (void*)1u;  // non-null = accept this section entry
        };
        h.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* handler,
                          void*, const char* line)
        {
            auto* s = static_cast<EditorPersistState*>(handler->UserData);
            char  buf[4096] = {};
            if (sscanf(line, "assetRoot=%4095[^\n]", buf) == 1)
            {
                s->assetRoot    = buf;
                s->pendingApply = true;
            }
        };
        h.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler,
                          ImGuiTextBuffer* out)
        {
            const auto* s = static_cast<const EditorPersistState*>(handler->UserData);
            out->appendf("[%s][Data]\n", handler->TypeName);
            out->appendf("assetRoot=%s\n", s->assetRoot.c_str());
            if (s->sdlWindow)
            {
                int x = 0, y = 0, w = 0, h = 0;
                SDL_GetWindowPosition(s->sdlWindow, &x, &y);
                SDL_GetWindowSize(s->sdlWindow, &w, &h);
                out->appendf("windowX=%d\n", x);
                out->appendf("windowY=%d\n", y);
                out->appendf("windowW=%d\n", w);
                out->appendf("windowH=%d\n", h);
            }
            out->append("\n");
        };
        ImGui::AddSettingsHandler(&h);
    }

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForMetal(window);
    ImGui_ImplMetal_Init(mtlDevice);

    // ── Splash screen ──────────────────────────────────────────────────────────────────────────
    // Must be initialised AFTER the ImGui context exists and the Metal device
    // is configured — stb_image needs the texture pixel format to be set.
    SplashScreen splash;
    splash.init(device->nativeDevice(), resourceDir());

    // ── Editor state
    const std::string shaderLibPath = resourceDir() + "/daedalus_shaders.metallib";

    EditMapDocument   doc;
    SelectTool        selectTool;
    DrawSectorTool    drawSectorTool;
    ActiveTool        activeToolId  = ActiveTool::Select;
    IEditorTool*      activeTool    = &selectTool;
    activeTool->activate(doc);

    VertexTool        vertexTool;
    Viewport2D        vp2d;
    Viewport3D        vp3d(shaderLibPath);
    PropertyInspector inspector;
    OutputLog         outputLog;
    FileDialogState   dlgState;
    RenderSettingsPanel renderPanel([&dlgState]()
    {
        spawnFileDialog(&dlgState,
            "POSIX path of (choose file of type {\"public.image\"})");
        dlgState.purpose = FileDialogPurpose::OpenLUT;
    });
    ObjectBrowserPanel  objBrowser;
    LayersPanel         layersPanel;
    HelpPanel           helpPanel;
    NewMapDialogState   nmDlg;
    MaterialCatalog     catalog;
    ModelCatalog        modelCatalog;
    ModelCatalog        voxCatalog;
    AssetBrowserPanel   assetBrowser;
    NormalMapGeneratorPanel  normalMapGenPanel;
    FloorLayerPanel          floorLayerPanel;
    StaircaseGeneratorPanel  staircaseGenPanel;
    std::string              lastKnownAssetRoot;

    // Panel visibility flags (controlled by Window menu)
    bool show2DViewport          = true;
    bool show3DViewport          = true;
    bool showPropertyInspector   = true;
    bool showObjectBrowser       = true;
    bool showAssetBrowser        = true;
    bool showLayers              = true;
    bool showOutputLog           = true;
    bool showRenderSettings      = true;
    bool showNormalMapGen        = false;  // Hidden by default
    bool showFloorLayers         = false;  // Hidden by default
    bool showStaircaseGen        = false;  // Hidden by default
    bool showHelp                = false;  // Hidden by default (opened via F1)

    // ── Wire up Asset Browser right-click callback to Normal Map Generator ───────
    assetBrowser.setNormalMapGenCallback([&](const UUID& textureUUID)
    {
        normalMapGenPanel.openWithTexture(textureUUID);
        showNormalMapGen = true;  // Make panel visible
    });

    // ── Model catalog: scan once at startup from the standard models directory.
    // ── Vox catalog: scan once at startup from assets/voxels/.
    // scan() silently returns if the root doesn't exist.
    {
        const std::filesystem::path modelRoot =
            std::filesystem::path(resourceDir()) / "assets" / "models";
        modelCatalog.setRoot(modelRoot, modelRoot.parent_path().parent_path());
        modelCatalog.scan();
    }
    {
        const std::filesystem::path voxRoot =
            std::filesystem::path(resourceDir()) / "assets" / "voxels";
        voxCatalog.setRoot(voxRoot, voxRoot.parent_path().parent_path());
        voxCatalog.setExtensions({".vox"});
        voxCatalog.scan();
    }

    bool layoutBuilt = false;

    // ── Test Map process tracking ─────────────────────────────────────────────
    // When non-nil, the Test Map app is running. We pause 3D rendering and
    // periodically check if the process has terminated.
    NSTask* __block testMapTask = nil;

    // ── Window title tracking ─────────────────────────────────────────────────
    // Update the OS title when dirty state or file path changes.
    bool        lastDirty = false;
    std::string lastPath;

    // ── Main loop ────────────────────────────────────────────────────────────
    bool running = true;
    Uint64 prevFrameNs = 0;  ///< Previous frame start time for delta-time.
    while (running)
    {
        // ── Frame timing: record start so we can cap at ~60 fps later ─────
        const Uint64 frameStartNs = SDL_GetTicksNS();
        const float  dt = prevFrameNs
            ? std::min(float(frameStartNs - prevFrameNs) / 1e9f, 0.05f)
            : 0.016f;
        prevFrameNs = frameStartNs;

        // ── Test Map task monitoring ─────────────────────────────────────────────
        // Check if the Test Map app has exited and resume rendering.
        if (testMapTask != nil && !testMapTask.isRunning)
        {
            doc.log("\\ — Test Map exited. Resuming 3D viewport rendering.");
            vp3d.setRenderingPaused(false);
            testMapTask = nil;
        }

        // ── Event processing ─────────────────────────────────────────────────
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            // During fly mode, handle mouse motion ourselves and skip ImGui so it
            // doesn't accumulate the relative-mouse absolute position into io.MousePos
            // (which would cause spurious hover state changes and io.MouseDelta spikes).
            if (vp3d.isMouseCaptured() && event.type == SDL_EVENT_MOUSE_MOTION)
            {
                vp3d.addMouseDelta(event.motion.xrel, event.motion.yrel);
                continue;  // do NOT pass to ImGui
            }

            ImGui_ImplSDL3_ProcessEvent(&event);

            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                metalLayer.drawableSize = CGSizeMake(event.window.data1,
                                                      event.window.data2);
                break;

            case SDL_EVENT_KEY_DOWN:
            {
                const SDL_Scancode sc = event.key.scancode;

                // Tab: toggle mouselook — always fires, even when ImGui has focus.
                if (sc == SDL_SCANCODE_TAB)
                    vp3d.setMouseCapture(window, !vp3d.isMouseCaptured());

                // Escape: release mouselook — always fires while captured.
                if (sc == SDL_SCANCODE_ESCAPE && vp3d.isMouseCaptured())
                    vp3d.setMouseCapture(window, false);

                // Remaining shortcuts suppressed when ImGui wants keyboard input.
                if (!io.WantCaptureKeyboard)
                {
                    if (sc == SDL_SCANCODE_TAB)
                    {
                        // Handled above.
                    }
                    // Escape without mouselook: cancel pending placement, draw, or deselect.
                    else if (sc == SDL_SCANCODE_ESCAPE)
                    {
                        if (vp2d.hasPendingPlacement())
                            vp2d.cancelPendingPlacement();
                        else if (activeToolId == ActiveTool::DrawSector)
                            drawSectorTool.cancel();
                        else
                        {
                        doc.selection().clear();
                        }
                    }
                    // Tool shortcuts suppressed while mouselook is active.
                    else if (!vp3d.isMouseCaptured())
                    {
                        if (sc == SDL_SCANCODE_S &&
                            activeToolId != ActiveTool::Select)
                        {
                            activeTool->deactivate(doc);
                            activeToolId = ActiveTool::Select;
                            activeTool   = &selectTool;
                            activeTool->activate(doc);
                        }
                        else if (sc == SDL_SCANCODE_D &&
                                 activeToolId != ActiveTool::DrawSector)
                        {
                            activeTool->deactivate(doc);
                            activeToolId = ActiveTool::DrawSector;
                            activeTool   = &drawSectorTool;
                            activeTool->activate(doc);
                        }
                        else if (sc == SDL_SCANCODE_V &&
                                 activeToolId != ActiveTool::Vertex)
                        {
                            activeTool->deactivate(doc);
                            activeToolId = ActiveTool::Vertex;
                            activeTool   = &vertexTool;
                            activeTool->activate(doc);
                        }
                        else if (sc == SDL_SCANCODE_W)
                        {
                            // Split wall at the cursor position projected onto the
                            // selected wall.  This lets you place the split vertex
                            // exactly where your cursor is — move the cursor to a
                            // corridor junction before pressing W.
                            // Hold Shift to force a midpoint split instead.
                            const SelectionState& sel = doc.selection();
                            if (sel.hasSingleOf(SelectionType::Wall))
                            {
                                const world::SectorId sid = sel.items[0].sectorId;
                                const std::size_t     wi  = sel.items[0].index;

                                glm::vec2 splitPt =
                                    {CmdSplitWall::k_useMidpoint,
                                     CmdSplitWall::k_useMidpoint};

                                const bool forceMiddle =
                                    (event.key.mod & SDL_KMOD_SHIFT) != 0;

                                if (!forceMiddle)
                                {
                                    const auto& sectors = doc.mapData().sectors;
                                    if (sid < static_cast<world::SectorId>(sectors.size()))
                                    {
                                        const auto& sector = sectors[sid];
                                        const std::size_t n = sector.walls.size();
                                        if (wi < n)
                                        {
                                            const glm::vec2 p0 = sector.walls[wi].p0;
                                            const glm::vec2 p1 =
                                                sector.walls[(wi + 1) % n].p0;
                                            const glm::vec2 ab    = p1 - p0;
                                            const float     lenSq = glm::dot(ab, ab);
                                            if (lenSq > 1e-12f)
                                            {
                                                // Project cursor onto wall; clamp
                                                // to (5%–95%) to avoid zero-length
                                                // degenerate sub-walls.
                                                const float t = glm::clamp(
                                                    glm::dot(
                                                        vp2d.lastMouseMapPos() - p0,
                                                        ab) / lenSq,
                                                    0.05f, 0.95f);
                                                splitPt = p0 + t * ab;
                                            }
                                        }
                                    }
                                }

                                doc.pushCommand(
                                    std::make_unique<CmdSplitWall>(
                                        doc, sid, wi, splitPt));
                            }
                            else
                            {
                                doc.log("W — Split Wall: select a wall first.");
                            }
                        }
                        else if (sc == SDL_SCANCODE_L)
                        {
                            // L — arm ghost-placement mode for a point light.
                            vp2d.beginPendingPlacement(PendingPlacement::Light);
                        }
                        else if (sc == SDL_SCANCODE_E)
                        {
                            // E — arm ghost-placement mode for an entity.
                            vp2d.beginPendingPlacement(PendingPlacement::Entity);
                        }
                        else if (sc == SDL_SCANCODE_T)
                        {
                            // T: translate gizmo — consumed by 3D viewport when hovered+entity.
                            if (vp3d.isHovered() &&
                                doc.selection().hasSingleOf(SelectionType::Entity))
                                vp3d.setGizmoMode(GizmoMode::Translate);
                            else
                                doc.log("T — Translate: select an entity while hovering the 3D viewport.");
                        }
                        else if (sc == SDL_SCANCODE_Y)
                        {
                            // Y: scale gizmo — consumed by 3D viewport when hovered+entity.
                            if (vp3d.isHovered() &&
                                doc.selection().hasSingleOf(SelectionType::Entity))
                                vp3d.setGizmoMode(GizmoMode::Scale);
                            else
                                doc.log("Y — Scale: select an entity while hovering the 3D viewport.");
                        }
                        else if (sc == SDL_SCANCODE_R)
                        {
                            const SelectionState& sel = doc.selection();
                            if (vp3d.isHovered() &&
                                sel.hasSingleOf(SelectionType::Entity))
                            {
                                // R in 3D viewport: rotate gizmo.
                                vp3d.setGizmoMode(GizmoMode::Rotate);
                            }
                            else if (sel.uniformType() == SelectionType::Sector &&
                                     !sel.items.empty())
                            {
                                // R elsewhere: rotate selected sector.
                                vp2d.openRotatePopup(sel.items[0].sectorId);
                            }
                            else
                            {
                                doc.log("R — Rotate: select an entity (3D) or sector (2D).");
                            }
                        }
                        else if (sc == SDL_SCANCODE_BACKSLASH)
                        {
                            // Compile the current editor document to a .dlevel bundle
                            // and launch DaedalusApp with it.  All textures referenced
                            // by the map are embedded as RGBA8 pixel blobs keyed by UUID.
                            const std::filesystem::path dlevelPath =
                                std::filesystem::temp_directory_path()
                                / "daedalus_playtest.dlevel";

                            // ── Collect material UUIDs referenced by the map ──────
                            std::unordered_set<UUID, UUIDHash> uuids;
                            for (const auto& sector : doc.mapData().sectors)
                            {
                                if (sector.floorMaterialId.isValid())
                                    uuids.insert(sector.floorMaterialId);
                                if (sector.ceilMaterialId.isValid())
                                    uuids.insert(sector.ceilMaterialId);
                        for (const auto& wall : sector.walls)
                            {
                                if (wall.frontMaterialId.isValid())
                                    uuids.insert(wall.frontMaterialId);
                                if (wall.upperMaterialId.isValid())
                                    uuids.insert(wall.upperMaterialId);
                                if (wall.lowerMaterialId.isValid())
                                    uuids.insert(wall.lowerMaterialId);
                                if (wall.backMaterialId.isValid())
                                    uuids.insert(wall.backMaterialId);
                            }
                        // Phase 1F: floor/ceiling portal surface materials
                        if (sector.floorPortalMaterialId.isValid())
                            uuids.insert(sector.floorPortalMaterialId);
                        if (sector.ceilPortalMaterialId.isValid())
                            uuids.insert(sector.ceilPortalMaterialId);
                        // Phase 1F: detail brush materials
                        for (const auto& db : sector.details)
                            if (db.materialId.isValid())
                                uuids.insert(db.materialId);
                            }

                            // ── Build LevelPackData ───────────────────────────────
                            world::LevelPackData pack;
                            pack.map = doc.mapData();

                            // Sun from scene settings.
                            const auto& ss   = doc.sceneSettings();
                            pack.sun.direction = ss.sunDirection;
                            pack.sun.color     = ss.sunColor;
                            pack.sun.intensity = ss.sunIntensity;

                            // Player start.
                            if (doc.playerStart().has_value())
                            {
                                world::LevelPlayerStart ps;
                                ps.position = doc.playerStart()->position;
                                ps.yaw      = doc.playerStart()->yaw;
                                pack.playerStart = ps;
                            }

                            // Lights.
                            for (const auto& ld : doc.lights())
                            {
                                world::LevelLight ll;
                                ll.type = static_cast<world::LevelLightType>(
                                    static_cast<uint32_t>(ld.type));
                                ll.position       = ld.position;
                                ll.color          = ld.color;
                                ll.radius         = ld.radius;
                                ll.intensity      = ld.intensity;
                                ll.direction      = ld.direction;
                                ll.innerConeAngle = ld.innerConeAngle;
                                ll.outerConeAngle = ld.outerConeAngle;
                                ll.range          = ld.range;
                                
                                // Debug: log first spotlight being packed
                                static bool logged = false;
                                if (!logged && ll.type == world::LevelLightType::Spot) {
                                    std::printf("[DLevel] Packing spotlight: inner=%.1f° (%.4f rad) outer=%.1f° (%.4f rad) range=%.1f intensity=%.1f\n",
                                                glm::degrees(ll.innerConeAngle), ll.innerConeAngle,
                                                glm::degrees(ll.outerConeAngle), ll.outerConeAngle,
                                                ll.range, ll.intensity);
                                    logged = true;
                                }
                                
                                pack.lights.push_back(ll);
                            }

                            // Textures: load each referenced UUID from disk.
                            // Also pack companion normal maps with a derived UUID.
                            for (const UUID& uuid : uuids)
                            {
                                const MaterialEntry* entry = catalog.find(uuid);
                                if (!entry) continue;

                                // Albedo texture
                                int imgW = 0, imgH = 0, channels = 0;
                                stbi_uc* pixels = stbi_load(
                                    entry->absPath.string().c_str(),
                                    &imgW, &imgH, &channels, 4);
                                if (pixels)
                                {
                                    world::LevelTexture tex;
                                    tex.width  = static_cast<u32>(imgW);
                                    tex.height = static_cast<u32>(imgH);
                                    tex.pixels.assign(pixels,
                                                      pixels + imgW * imgH * 4);
                                    stbi_image_free(pixels);
                                    pack.textures.emplace(uuid, std::move(tex));
                                }

                                // Normal map companion (if present)
                                if (!entry->normalPath.empty())
                                {
                                    int normW = 0, normH = 0, normChannels = 0;
                                    stbi_uc* normPixels = stbi_load(
                                        entry->normalPath.string().c_str(),
                                        &normW, &normH, &normChannels, 4);
                                    if (normPixels)
                                    {
                                        world::LevelTexture normTex;
                                        normTex.width  = static_cast<u32>(normW);
                                        normTex.height = static_cast<u32>(normH);
                                        normTex.pixels.assign(normPixels,
                                                              normPixels + normW * normH * 4);
                                        stbi_image_free(normPixels);
                                        // Pack with derived UUID: XOR low 32 bits to encode type.
                                        // UUID is stored as two u64: hi and lo.
                                        // We XOR the lower 32 bits of `lo` with 0xDEADBEEF.
                                        UUID normalUuid = uuid;
                                        normalUuid.lo ^= 0xDEADBEEFull;
                                        pack.textures.emplace(normalUuid, std::move(normTex));
                                        std::printf("[DLevel] Packed normal map: %s (%ux%u)\n",
                                                    entry->normalPath.filename().string().c_str(),
                                                    normW, normH);
                                    }
                                    else
                                    {
                                        std::printf("[DLevel] Warning: Failed to load normal map: %s\n",
                                                    entry->normalPath.string().c_str());
                                    }
                                }
                            }

                            // ── Entities ─────────────────────────────────────────
                            for (const EntityDef& ed : doc.entities())
                            {
                                world::LevelEntity ent;
                                ent.name     = ed.entityName;
                                ent.position = ed.position;
                                ent.yaw      = ed.yaw;
                                ent.sectorId = 0xFFFFFFFFu;  // INVALID; no spatial query here

                                // CollisionShape (editor) → LevelCollisionShape (runtime)
                                switch (ed.physics.shape)
                                {
                                    case CollisionShape::Box:
                                        ent.shape       = world::LevelCollisionShape::Box;
                                        ent.halfExtents = ed.scale * 0.5f;
                                        break;
                                    case CollisionShape::Sphere:
                                        // No sphere in LevelCollisionShape — use box
                                        ent.shape       = world::LevelCollisionShape::Box;
                                        ent.halfExtents = glm::vec3(
                                            std::max({ed.scale.x, ed.scale.y, ed.scale.z}) * 0.5f);
                                        break;
                                    case CollisionShape::Capsule:
                                        ent.shape       = world::LevelCollisionShape::Capsule;
                                        ent.halfExtents = glm::vec3(
                                            std::min(ed.scale.x, ed.scale.z) * 0.5f,
                                            ed.scale.y * 0.5f,
                                            0.0f);
                                        break;
                                }
                                ent.dynamic = !ed.physics.isStatic;
                                ent.mass    = ed.physics.mass;

                                // Script descriptor (v3)
                                ent.scriptPath  = ed.script.scriptPath;
                                ent.exposedVars = ed.script.exposedVars;

                                // Audio descriptor
                                ent.soundPath          = ed.audio.soundPath;
                                ent.soundFalloffRadius = ed.audio.falloffRadius;
                                ent.soundVolume        = ed.audio.volume;
                                ent.soundLoop          = ed.audio.loop;
                                ent.soundAutoPlay      = ed.audio.autoPlay;

                                // Visual descriptor (v4)
                                ent.visualType = static_cast<world::LevelEntityVisualType>(
                                    static_cast<uint32_t>(ed.visualType));
                                ent.assetPath   = ed.assetPath;
                                ent.tint        = ed.tint;
                                ent.visualScale = ed.scale;
                                ent.visualPitch = ed.pitch;
                                ent.visualRoll  = ed.roll;

                                // Animated billboard / RotatedSpriteSet
                                ent.animFrameCount       = ed.anim.frameCount;
                                ent.animCols             = ed.anim.cols;
                                ent.animRows             = ed.anim.rows;
                                ent.animFrameRate        = ed.anim.frameRate;
                                ent.rotatedSpriteDirCount = ed.rotatedSprite.directionCount;

                                // Decal
                                ent.decalNormalPath = ed.decalMat.normalPath;
                                ent.decalRoughness  = ed.decalMat.roughness;
                                ent.decalMetalness  = ed.decalMat.metalness;
                                ent.decalOpacity    = ed.decalMat.opacity;

                                // Particle emitter
                                ent.particleEmissionRate  = ed.particle.emissionRate;
                                ent.particleEmitDir       = ed.particle.emitDir;
                                ent.particleConeHalfAngle = ed.particle.coneHalfAngle;
                                ent.particleSpeedMin      = ed.particle.speedMin;
                                ent.particleSpeedMax      = ed.particle.speedMax;
                                ent.particleLifetimeMin   = ed.particle.lifetimeMin;
                                ent.particleLifetimeMax   = ed.particle.lifetimeMax;
                                ent.particleColorStart    = ed.particle.colorStart;
                                ent.particleColorEnd      = ed.particle.colorEnd;
                                ent.particleSizeStart     = ed.particle.sizeStart;
                                ent.particleSizeEnd       = ed.particle.sizeEnd;
                                ent.particleDrag             = ed.particle.drag;
                                ent.particleGravity          = ed.particle.gravity;
                                // Extended v5 particle fields
                                ent.particleSoftRange        = ed.particle.softRange;
                                ent.particleEmissiveStart    = ed.particle.emissiveStart;
                                ent.particleEmissiveEnd      = ed.particle.emissiveEnd;
                                ent.particleEmitsLight       = ed.particle.emitsLight;
                                ent.particleShadowDensity    = ed.particle.shadowDensity;
                                // Atlas grid from AnimSettings (shared with billboard anim)
                                ent.particleAtlasCols        = ed.anim.cols;
                                ent.particleAtlasRows        = ed.anim.rows;
                                ent.particleAtlasFrameRate   = ed.anim.frameRate;

                                pack.entities.push_back(std::move(ent));
                            }

                            // ── Render settings (v6) ─────────────────────────────
                            const auto& rs = doc.renderSettings();
                            
                            // Fog
                            pack.renderSettings.fogEnabled   = rs.fog.enabled;
                            pack.renderSettings.fogAmbientR  = rs.fog.ambientFogR;
                            pack.renderSettings.fogAmbientG  = rs.fog.ambientFogG;
                            pack.renderSettings.fogAmbientB  = rs.fog.ambientFogB;
                            pack.renderSettings.fogDensity   = rs.fog.density;
                            pack.renderSettings.fogAnisotropy = rs.fog.anisotropy;
                            pack.renderSettings.fogScattering = rs.fog.scattering;
                            pack.renderSettings.fogNear      = rs.fog.fogNear;
                            pack.renderSettings.fogFar       = rs.fog.fogFar;
                            
                            // SSR
                            pack.renderSettings.ssrEnabled         = rs.ssr.enabled;
                            pack.renderSettings.ssrMaxDistance     = rs.ssr.maxDistance;
                            pack.renderSettings.ssrThickness       = rs.ssr.thickness;
                            pack.renderSettings.ssrRoughnessCutoff = rs.ssr.roughnessCutoff;
                            pack.renderSettings.ssrFadeStart       = rs.ssr.fadeStart;
                            pack.renderSettings.ssrMaxSteps        = rs.ssr.maxSteps;
                            
                            // DoF
                            pack.renderSettings.dofEnabled         = rs.dof.enabled;
                            pack.renderSettings.dofFocusDistance   = rs.dof.focusDistance;
                            pack.renderSettings.dofFocusRange      = rs.dof.focusRange;
                            pack.renderSettings.dofBokehRadius     = rs.dof.bokehRadius;
                            pack.renderSettings.dofNearTransition  = rs.dof.nearTransition;
                            pack.renderSettings.dofFarTransition   = rs.dof.farTransition;
                            
                            // Motion blur
                            pack.renderSettings.motionBlurEnabled      = rs.motionBlur.enabled;
                            pack.renderSettings.motionBlurShutterAngle = rs.motionBlur.shutterAngle;
                            pack.renderSettings.motionBlurNumSamples   = rs.motionBlur.numSamples;
                            
                            // Color grading
                            pack.renderSettings.colorGradingEnabled   = rs.colorGrading.enabled;
                            pack.renderSettings.colorGradingIntensity = rs.colorGrading.intensity;
                            pack.renderSettings.colorGradingLutPath   = rs.colorGrading.lutPath;
                            
                            // Optional FX (vignette, grain, CA)
                            pack.renderSettings.optFxEnabled           = rs.optionalFx.enabled;
                            pack.renderSettings.optFxCaAmount          = rs.optionalFx.caAmount;
                            pack.renderSettings.optFxVignetteIntensity = rs.optionalFx.vignetteIntensity;
                            pack.renderSettings.optFxVignetteRadius    = rs.optionalFx.vignetteRadius;
                            pack.renderSettings.optFxGrainAmount       = rs.optionalFx.grainAmount;
                            
                            // FXAA
                            pack.renderSettings.fxaaEnabled = rs.upscaling.fxaaEnabled;
                            
                            // Ray tracing
                            pack.renderSettings.rtEnabled         = rs.rt.enabled;
                            pack.renderSettings.rtMaxBounces      = rs.rt.maxBounces;
                            pack.renderSettings.rtSamplesPerPixel = rs.rt.samplesPerPixel;
                            pack.renderSettings.rtDenoise         = rs.rt.denoise;

                            // ── Serialise .dlevel ────────────────────────────────
                            const auto saveResult =
                                world::saveDlevel(pack, dlevelPath);
                            if (!saveResult.has_value())
                            {
                                doc.log("\\ — Test Map: .dlevel save failed.");
                            }
                            else
                            {
                                // Locate DaedalusApp: try standalone (debug) first,
                                // then app bundle (release) as fallback.
                                std::filesystem::path resDir = resourceDir();
                                std::filesystem::path appPath;
                                
                                // From standalone editor: ../app/DaedalusApp
                                std::filesystem::path standalonePath =
                                    (resDir / ".." / "app" / "DaedalusApp")
                                    .lexically_normal();
                                
                                // From app bundle editor: navigate to sibling app bundle
                                // (build/app/DaedalusApp.app/Contents/MacOS/DaedalusApp)
                                std::filesystem::path bundlePath =
                                    (resDir / ".." / ".." / ".." / ".." / "app" 
                                     / "DaedalusApp.app" / "Contents" / "MacOS" / "DaedalusApp")
                                    .lexically_normal();
                                
                                // Prefer standalone (debug) for development
                                if (std::filesystem::exists(standalonePath))
                                    appPath = standalonePath;
                                else if (std::filesystem::exists(bundlePath))
                                    appPath = bundlePath;

                                if (appPath.empty())
                                {
                                        doc.log(std::format(
                                            "\\ — Test Map: DaedalusApp not found at '{}' or '{}'.",
                                        bundlePath.string(), standalonePath.string()));
                                }
                                else
                                {
                                    NSString* appNS = [NSString
                                        stringWithUTF8String:appPath.string().c_str()];
                                    NSString* levelNS = [NSString
                                        stringWithUTF8String:dlevelPath.string().c_str()];

                                    NSTask* task = [[NSTask alloc] init];
                                    task.executableURL =
                                        [NSURL fileURLWithPath:appNS];

                                    // Build argument list: pass all render settings
                                    // so the app starts with exactly what the editor
                                    // has configured in the Render Settings panel.
                                    const auto& rs = doc.renderSettings();
                                    NSMutableArray<NSString*>* launchArgs =
                                        [NSMutableArray arrayWithObject:levelNS];

                                    // RT mode + sub-settings
                                    if (rs.rt.enabled)
                                        [launchArgs addObject:@"--rt"];
                                    [launchArgs addObject:
                                        [NSString stringWithFormat:@"--rt-bounces=%u",
                                            rs.rt.maxBounces]];
                                    [launchArgs addObject:
                                        [NSString stringWithFormat:@"--rt-spp=%u",
                                            rs.rt.samplesPerPixel]];
                                    if (!rs.rt.denoise)
                                        [launchArgs addObject:@"--rt-nodenoise"];

                                    // Post-FX toggles
                                    if (rs.fog.enabled)
                                        [launchArgs addObject:@"--fog"];
                                    if (rs.ssr.enabled)
                                        [launchArgs addObject:@"--ssr"];
                                    if (rs.dof.enabled)
                                        [launchArgs addObject:@"--dof"];
                                    if (!rs.motionBlur.enabled)
                                        [launchArgs addObject:@"--nomotionblur"];
                                    if (!rs.colorGrading.enabled)
                                        [launchArgs addObject:@"--nocolorgrade"];
                                    if (!rs.optionalFx.enabled)
                                        [launchArgs addObject:@"--nooptfx"];
                                    if (!rs.upscaling.fxaaEnabled)
                                        [launchArgs addObject:@"--nofxaa"];

                                    task.arguments = launchArgs;

                                    NSError* launchErr = nil;
                                    if ([task launchAndReturnError:&launchErr])
                                    {
                                        // Store the task and pause 3D rendering to improve test app performance.
                                        testMapTask = task;
                                        vp3d.setRenderingPaused(true);
                                        
                                        // On macOS 14+, a process launched via NSTask
                                        // cannot steal focus using
                                        // activateIgnoringOtherApps: — the system
                                        // silently ignores the call unless the calling
                                        // process is already front.  SDL tries this at
                                        // DaedalusApp startup and it fails.
                                        //
                                        // Fix: activate the child FROM HERE (the editor
                                        // currently has focus), with a short delay so
                                        // DaedalusApp's NSApplication finishes
                                        // initialising before the request is sent.
                                        pid_t childPid = task.processIdentifier;
                                        dispatch_after(
                                            dispatch_time(DISPATCH_TIME_NOW,
                                                (int64_t)(0.4 * NSEC_PER_SEC)),
                                            dispatch_get_main_queue(),
                                            ^{
                                                NSRunningApplication* child =
                                                    [NSRunningApplication
                                                        runningApplicationWithProcessIdentifier:
                                                            childPid];
                                                if (child) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                                                    [child activateWithOptions:
                                                        NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
                                                }
                                            });
                                        doc.log(std::format(
                                            "\\ — Test Map launched: '{}'.",
                                            dlevelPath.string()));
                                    }
                                    else
                                    {
                                        const char* why = launchErr
                                            ? launchErr.localizedDescription.UTF8String
                                            : "unknown error";
                                        doc.log(std::format(
                                            "\\ — Test Map launch failed: {}.", why));
                                    }
                                }
                            }
                        }
                        else if (sc == SDL_SCANCODE_F1)
                        {
                            showHelp = !showHelp;
                            if (showHelp)
                                helpPanel.setOpen(true);
                        }
                        else if (sc == SDL_SCANCODE_F9)
                        {
                            doc.log("F9 — Multi-user: deferred (requires networking).");
                        }
                        else if (sc == SDL_SCANCODE_RETURN ||
                                 sc == SDL_SCANCODE_KP_ENTER)
                        {
                            if (activeToolId == ActiveTool::DrawSector)
                                drawSectorTool.tryFinish(doc);
                        }
                        else if (sc == SDL_SCANCODE_BACKSPACE ||
                                 sc == SDL_SCANCODE_DELETE)
                        {
                        // Delete — remove selected sector(s) or light.
                            const SelectionState& sel = doc.selection();
                            if (sel.uniformType() == SelectionType::Sector &&
                                !sel.items.empty())
                            {
                                // Delete sectors in reverse index order so earlier
                                // indices stay valid as we remove later ones.
                                std::vector<world::SectorId> sorted = sel.selectedSectors();
                                std::sort(sorted.begin(), sorted.end(),
                                          std::greater<world::SectorId>{});
                                for (const world::SectorId sid : sorted)
                                    doc.pushCommand(
                                        std::make_unique<CmdDeleteSector>(doc, sid));
                            }
                            else if (sel.hasSingleOf(SelectionType::Light))
                            {
                                doc.pushCommand(
                                    std::make_unique<CmdDeleteLight>(doc, sel.items[0].index));
                            }
                            else if (sel.hasSingleOf(SelectionType::Entity))
                            {
                                doc.pushCommand(
                                    std::make_unique<CmdDeleteEntity>(doc, sel.items[0].index));
                            }
                            else if (sel.hasSingleOf(SelectionType::PlayerStart))
                            {
                                doc.pushCommand(
                                    std::make_unique<CmdSetPlayerStart>(
                                        doc, doc.playerStart(), std::nullopt));
                            }
                            else
                            {
                                doc.log("Delete — no sector, light, entity, or player start selected.");
                            }
                        }
                        else if (sc == SDL_SCANCODE_G)
                        {
                            vp2d.setGridVisible(!vp2d.gridVisible());
                        }
                        else if (sc == SDL_SCANCODE_X)
                        {
                            // X — toggle grid snap on/off.
                            vp2d.setSnapEnabled(!vp2d.snapEnabled());
                        }
                        else if (sc == SDL_SCANCODE_HOME ||
                                 (sc == SDL_SCANCODE_F && !vp3d.isHovered()))
                        {
                            vp2d.fitToView(doc.mapData());
                        }
                        else if (sc == SDL_SCANCODE_P && !vp3d.isHovered())
                        {
                            // Place a point light at the centre of the 2D view so it is
                            // always visible.  Suppressed when the 3D viewport is hovered:
                            // P is reserved there for snapping the camera to player start.
                            const glm::vec2 mp = vp2d.viewCenterMapPos();
                            LightDef ld;
                            ld.position = {mp.x, 2.0f, mp.y};
                            doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
                        }
                        else
                        {
                            // Keys 1–8: set grid step.
                            static constexpr float k_gridSteps[8] =
                                { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f };
                            static constexpr SDL_Scancode k_gridKeys[8] = {
                                SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
                                SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
                                SDL_SCANCODE_7, SDL_SCANCODE_8
                            };
                            for (int gi = 0; gi < 8; ++gi)
                            {
                                if (sc == k_gridKeys[gi])
                                {
                                    vp2d.setGridStep(k_gridSteps[gi]);
                                    break;
                                }
                            }
                        }
                    }
                }

                // Cmd+Z / Cmd+Shift+Z undo/redo.
                if (event.key.mod & SDL_KMOD_GUI)
                {
                    if (sc == SDL_SCANCODE_Z)
                    {
                        if (event.key.mod & SDL_KMOD_SHIFT)
                            doc.redo();
                        else
                            doc.undo();
                    }
                    // Cmd+N — open New Map dialog.
                    if (sc == SDL_SCANCODE_N)
                        nmDlg.open = true;

                    // Cmd+S save.
                    if (sc == SDL_SCANCODE_S && !doc.filePath().empty())
                        (void)doc.saveToCurrentPath();  // failure already logged to OutputLog

                    // Cmd+A — select all sectors.
                    if (sc == SDL_SCANCODE_A)
                        doc.selection().selectAll(doc.mapData().sectors.size());

                    // Cmd+C — copy selected sector to clipboard.
                    if (sc == SDL_SCANCODE_C)
                    {
                        const SelectionState& sel = doc.selection();
                        if (sel.uniformType() == SelectionType::Sector &&
                            !sel.items.empty())
                            doc.copySector(sel.items[0].sectorId);
                        else
                            doc.log("Cmd+C — Copy: select a sector first.");
                    }

                    // Cmd+V — paste clipboard sector.
                    if (sc == SDL_SCANCODE_V)
                    {
                        if (doc.hasClipboard())
                            doc.pushCommand(std::make_unique<CmdPasteSector>(
                                doc, *doc.clipboard(),
                                glm::vec2{vp2d.gridStep(), vp2d.gridStep()}));
                        else
                            doc.log("Cmd+V — Paste: clipboard is empty.");
                    }

                    // Cmd+Shift+M — Map Doctor.
                    if (sc == SDL_SCANCODE_M && (event.key.mod & SDL_KMOD_SHIFT))
                        runMapDoctor(doc);

                    // Cmd+D — duplicate selected sector.
                    if (sc == SDL_SCANCODE_D)
                    {
                        const SelectionState& sel = doc.selection();
                        if (sel.uniformType() == SelectionType::Sector &&
                            !sel.items.empty())
                        {
                            doc.pushCommand(
                                std::make_unique<CmdDuplicateSector>(
                                    doc,
                                    sel.items[0].sectorId,
                                    glm::vec2{vp2d.gridStep(), vp2d.gridStep()}));
                        }
                    }
                }
                break;
            }

            default:
                break;
            }
        }

        if (!running) break;

        // ── Acquire next drawable from the Metal layer
        id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
        if (!drawable) continue;

        // ── Set up ImGui render-pass descriptor ───────────────────────────────
        MTLRenderPassDescriptor* rpd =
            [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture     = drawable.texture;
        rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
        rpd.colorAttachments[0].clearColor  = MTLClearColorMake(0.12, 0.12, 0.14, 1.0);
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

        // ── ImGui new frame ───────────────────────────────────────────────────
        ImGui_ImplMetal_NewFrame(rpd);
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // While mouselook is captured, hide the cursor from ImGui so panel hover
        // states don't change and UI widgets don't interfere with fly-mode input.
        if (vp3d.isMouseCaptured())
            ImGui::GetIO().MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

        // ── Full-screen dockspace ─────────────────────────────────────────────
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::SetNextWindowViewport(vp->ID);

            ImGuiWindowFlags hostFlags =
                ImGuiWindowFlags_NoTitleBar      |
                ImGuiWindowFlags_NoCollapse      |
                ImGuiWindowFlags_NoResize        |
                ImGuiWindowFlags_NoMove          |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus      |
                ImGuiWindowFlags_MenuBar;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
            ImGui::Begin("##DockHost", nullptr, hostFlags);
            ImGui::PopStyleVar(3);

            const ImGuiID dsId = ImGui::GetID("MainDockSpace");
            ImGui::DockSpace(dsId, ImVec2(0, 0), ImGuiDockNodeFlags_None);

            // Build the default layout exactly once (first frame when no ini exists).
            if (!layoutBuilt && ImGui::DockBuilderGetNode(dsId) == nullptr)
            {
                buildDefaultDockLayout(dsId);
                layoutBuilt = true;
            }
            else if (!layoutBuilt)
            {
                layoutBuilt = true;
            }

            ImGui::End();
        }

        // ── First-frame: restore asset root persisted in a previous session ────────
        if (persistState.pendingApply)
        {
            persistState.pendingApply = false;
            if (doc.assetRoot().empty())
            {
                if (!persistState.assetRoot.empty())
                    doc.setAssetRoot(persistState.assetRoot);
                else
                {
                    // No persisted root — default to the bundled textures folder
                    // shipped next to the resource directory.  The user can always override
                    // this via File > Set Asset Root.
                    const std::string def = resourceDir() + "/assets/textures";
                    if (std::filesystem::is_directory(def))
                        doc.setAssetRoot(def);
                }
            }
        }

        // ── Asset catalog: rescan whenever doc's assetRoot changes ──────────────────────────────────────
        // Triggered by: INI restore above, File > Set Asset Root, or loading a .dmap
        // that carries its own assetRoot.  Also persists the new root to the INI.
        {
            const std::string ar = doc.assetRoot();
            bool needsRescan = (ar != lastKnownAssetRoot);
            
            // Also rescan if explicitly requested (e.g., after generating a normal map).
            if (!needsRescan && !ar.empty())
                needsRescan = catalog.consumeRescanRequest();
            
            if (needsRescan)
            {
                if (ar != lastKnownAssetRoot)
                {
                    lastKnownAssetRoot = ar;
                    persistState.assetRoot = ar;
                    ImGui::MarkIniSettingsDirty();
                }
                
                if (!ar.empty())
                {
                    catalog.setRoot(std::filesystem::path(ar));
                    catalog.scan();
                    
                    // Catalog rescan invalidates MaterialEntry pointers.
                    // Mark geometry and entities dirty to force reload.
                    doc.markDirty();      // Marks geometry dirty
                    doc.markEntityDirty(); // Marks entity cache dirty

                    // Also scan the sibling models and voxels directories.
                    const std::filesystem::path modelRoot =
                        std::filesystem::path(ar).parent_path() / "models";
                    if (std::filesystem::is_directory(modelRoot))
                    {
                        modelCatalog.setRoot(modelRoot,
                            modelRoot.parent_path().parent_path());
                        modelCatalog.scan();
                    }
                    const std::filesystem::path voxRoot =
                        std::filesystem::path(ar).parent_path() / "voxels";
                    voxCatalog.setRoot(voxRoot, voxRoot.parent_path().parent_path());
                    voxCatalog.setExtensions({".vox"});
                    voxCatalog.scan();
                }
            }
        }

        // ── File dialog results ───────────────────────────────────────────────
        if (dlgState.ready.load(std::memory_order_acquire))
        {
            dlgState.ready.store(false, std::memory_order_relaxed);
            const std::string dlgPath = std::exchange(dlgState.result, {});
            switch (dlgState.purpose)
            {
            case FileDialogPurpose::OpenMap:
                if (!dlgPath.empty() && doc.loadFromFile(dlgPath))
                {
                    if (const auto& cam = doc.viewportCamera(); cam.has_value())
                        vp3d.setCameraState(cam->eye, cam->yaw, cam->pitch);
                }
                break;
        case FileDialogPurpose::SaveMap:
                if (!dlgPath.empty())
                {
                    std::filesystem::path p(dlgPath);
                    // Strip any existing .dmap extension (case-insensitive),
                    // then unconditionally re-apply it.  This prevents doubles
                    // when the default name already contains the extension.
                    {
                        std::string ext = p.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(),
                                       [](unsigned char c){ return std::tolower(c); });
                        if (ext == ".dmap")
                            p = p.parent_path() / p.stem();
                    }
                    p += ".dmap";
                    (void)doc.saveToFile(p);
                }
                break;
            case FileDialogPurpose::OpenLUT:
                renderPanel.deliverPath(dlgPath);
                break;
            case FileDialogPurpose::OpenAssetRoot:
                if (!dlgPath.empty())
                    doc.setAssetRoot(dlgPath);
                break;
            default: break;
            }
            dlgState.purpose = FileDialogPurpose::None;
        }

        // Snapshot 3D camera into doc every frame so any save path captures it.
        doc.setViewportCamera({vp3d.eye(), vp3d.yaw(), vp3d.pitch()});

        // ── Menu bar ──────────────────────────────────────────────────────────
        drawMenuBar(doc, activeToolId, selectTool, drawSectorTool, vertexTool, activeTool,
                    vp2d.gridStep(), vp2d.viewCenterMapPos(), nmDlg.open, helpPanel,
                    dlgState,
                    show2DViewport, show3DViewport, showPropertyInspector,
                    showObjectBrowser, showAssetBrowser, showLayers,
                    showOutputLog, showRenderSettings, showNormalMapGen,
                    showFloorLayers, showStaircaseGen, showHelp);

        // ── Panels ────────────────────────────────────────────────────────────
        DrawSectorTool* drawToolPtr =
            (activeToolId == ActiveTool::DrawSector) ? &drawSectorTool : nullptr;
        SelectTool* selectToolPtr =
            (activeToolId == ActiveTool::Select) ? &selectTool : nullptr;
        VertexTool* vertexToolPtr =
            (activeToolId == ActiveTool::Vertex) ? &vertexTool : nullptr;

        if (show2DViewport)
        {
            vp2d.updateFlyCamera(vp3d.isMouseCaptured(),
                                  {vp3d.eye().x, vp3d.eye().z},
                                  vp3d.yaw());
            vp2d.draw(doc, activeTool, drawToolPtr, selectToolPtr, vertexToolPtr,
                      showFloorLayers ? &floorLayerPanel : nullptr);
        }

        // Consume a pending placement committed by a click in the 2D viewport.
        {
            const PendingPlacement pendingType = vp2d.pendingPlacementType();
            glm::vec2 placedPos;
            if (vp2d.consumePendingPlacement(placedPos))
            {
                if (pendingType == PendingPlacement::Entity)
                {
                    EntityDef ed;
                    ed.position = {placedPos.x, 0.5f, placedPos.y};
                    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, ed));
                }
                else if (pendingType == PendingPlacement::Light)
                {
                    LightDef ld;
                    ld.position = {placedPos.x, 2.0f, placedPos.y};
                    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
                }
            }
        }

        if (show3DViewport)
        {
            vp3d.draw(doc, *assetLoader, *device, *queue, catalog, assetBrowser,
                      vp2d.isEntityRotating(), vp2d.rotatingEntityIdx());
        }
        
        if (showPropertyInspector)
        {
            inspector.draw(doc, catalog, *device, *assetLoader, assetBrowser,
                            &voxCatalog, vp2d.gridStep());
        }
        
        if (showAssetBrowser)
        {
            assetBrowser.draw(catalog, *device, *assetLoader, modelCatalog, &voxCatalog);
        }
        
        if (showRenderSettings)
        {
            renderPanel.draw(doc);
        }
        
        if (showObjectBrowser)
        {
            objBrowser.draw(doc, vp2d.viewCenterMapPos());
        }
        
        if (showLayers)
        {
            layersPanel.draw(doc);
        }
        
        if (showOutputLog)
        {
            outputLog.draw(doc);
        }
        
        if (showHelp)
        {
            helpPanel.draw();
        }
        
        if (showNormalMapGen)
        {
            normalMapGenPanel.draw(&showNormalMapGen, catalog, *device, *assetLoader, doc);
        }

        if (showFloorLayers)
            floorLayerPanel.draw(doc);

        if (showStaircaseGen)
            staircaseGenPanel.draw(doc);

        // ── New Map dialog ──────────────────────────────────────────────────────────────────────
        if (nmDlg.open)
        {
            ImGui::OpenPopup("New Map##dlg");
            nmDlg.open = false;  // only trigger OpenPopup once per frame
        }
        if (ImGui::BeginPopupModal("New Map##dlg", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name",   nmDlg.name,   sizeof(nmDlg.name));
            ImGui::InputText("Author", nmDlg.author, sizeof(nmDlg.author));
            ImGui::Separator();
            ImGui::DragFloat("Floor Height", &nmDlg.floorH,  0.1f);
            ImGui::DragFloat("Ceil Height",  &nmDlg.ceilH,   0.1f);
            ImGui::DragFloat("Gravity",      &nmDlg.gravity, 0.1f, 0.0f, 100.0f);
            ImGui::InputText("Sky Path",     nmDlg.skyPath, sizeof(nmDlg.skyPath));
            ImGui::Separator();
            ImGui::ColorEdit3("Ambient Color", nmDlg.ambientColor);
            ImGui::Spacing();

            if (ImGui::Button("Create", ImVec2(120.0f, 0.0f)))
            {
                doc.newMap();
                doc.setDefaultFloorHeight(nmDlg.floorH);
                doc.setDefaultCeilHeight(nmDlg.ceilH);
                doc.setGravity(nmDlg.gravity);
                doc.setSkyPath(nmDlg.skyPath);
                doc.mapData().globalAmbientColor =
                    glm::vec3{nmDlg.ambientColor[0],
                              nmDlg.ambientColor[1],
                              nmDlg.ambientColor[2]};
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        // ── Splash screen modal ─────────────────────────────────────────────
        if (splash.isVisible())
            splash.draw(dt);

        // ── Window title ────────────────────────────────────────────────────
        {
            const bool        dirty   = doc.isDirty();
            const std::string curPath = doc.filePath().empty()
                ? std::string{}
                : doc.filePath().filename().string();

            if (dirty != lastDirty || curPath != lastPath)
            {
                std::string title = "DaedalusEdit";
                if (!curPath.empty())
                    title += " \xe2\x80\x94 " + curPath;  // em-dash
                if (dirty)
                    title += " *";
                SDL_SetWindowTitle(window, title.c_str());
                lastDirty = dirty;
                lastPath  = curPath;
            }
        }

        // ── Render ImGui ──────────────────────────────────────────────────────
        ImGui::Render();

        id<MTLCommandBuffer>        cmdBuf = [mtlQueue commandBuffer];
        id<MTLRenderCommandEncoder> enc    =
            [cmdBuf renderCommandEncoderWithDescriptor:rpd];

        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmdBuf, enc);
        [enc endEncoding];
        [cmdBuf presentDrawable:drawable];
        [cmdBuf commit];

        // ── Frame rate cap: ~60 fps ───────────────────────────────────────
        // displaySyncEnabled = NO means nextDrawable never blocks on vsync.
        // Sleep for any remaining budget to avoid over-driving the GPU.
        constexpr Uint64 kTargetFrameNs = 16'666'667ULL;
        const Uint64 elapsed = SDL_GetTicksNS() - frameStartNs;
        if (elapsed < kTargetFrameNs)
            SDL_DelayNS(kTargetFrameNs - elapsed);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    // Flush window geometry to the INI before tearing down ImGui, so the
    // final position/size is always captured even if nothing else was dirty.
    ImGui::MarkIniSettingsDirty();
    ImGui::SaveIniSettingsToDisk("daedalusedit.ini");

    ImGui_ImplMetal_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    // Release RHI resources before destroying the SDL window.
    assetLoader.reset();
    queue.reset();
    device.reset();

    SDL_Metal_DestroyView(metalView);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
