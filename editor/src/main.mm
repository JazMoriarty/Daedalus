// DaedalusEdit — entry point (Objective-C++ / ARC)
//
// Window management: SDL3 with SDL_WINDOW_METAL.
// ImGui rendering : imgui_impl_sdl3 + imgui_impl_metal.
// The main CAMetalLayer is obtained from SDL_Metal_GetLayer and used
// directly for ImGui — no RHI ISwapchain is created for the editor window.
// The RHI device/queue are used only for the 3D viewport offscreen swapchain.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_metal.h"

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
#include "document/commands/cmd_place_light.h"
#include "document/commands/cmd_delete_light.h"
#include "document/commands/cmd_place_entity.h"
#include "document/commands/cmd_delete_entity.h"
#include "document/commands/cmd_set_player_start.h"
#include "catalog/material_catalog.h"
#include "panels/asset_browser_panel.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace daedalus;
using namespace daedalus::editor;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string executableDir()
{
    const char* base = SDL_GetBasePath();
    return base ? base : ".";
}

// Synchronously show an NSOpenPanel and return the chosen path (empty = cancel).
static std::filesystem::path showOpenPanel()
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles          = YES;
    panel.canChooseDirectories    = NO;
    panel.allowsMultipleSelection = NO;
    panel.title                   = @"Open Map";

    if ([panel runModal] == NSModalResponseOK)
        return std::filesystem::path([[panel.URL path] UTF8String]);
    return {};
}

// Synchronously show an NSOpenPanel filtered to image files.
// Returns the chosen path, or empty on cancel.
static std::filesystem::path showOpenPanelForImage()
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles          = YES;
    panel.canChooseDirectories    = NO;
    panel.allowsMultipleSelection = NO;
    panel.title                   = @"Choose LUT Image";
    panel.allowedContentTypes     = @[
        [UTType typeWithIdentifier:@"public.png"],
        [UTType typeWithIdentifier:@"public.jpeg"],
    ];

    if ([panel runModal] == NSModalResponseOK)
        return std::filesystem::path([[panel.URL path] UTF8String]);
    return {};
}

// Synchronously show an NSOpenPanel for a directory (used for asset root selection).
static std::filesystem::path showOpenPanelForDirectory()
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles          = NO;
    panel.canChooseDirectories    = YES;
    panel.allowsMultipleSelection = NO;
    panel.title                   = @"Choose Asset Root";

    if ([panel runModal] == NSModalResponseOK)
        return std::filesystem::path([[panel.URL path] UTF8String]);
    return {};
}

// Synchronously show an NSSavePanel and return the chosen path (empty = cancel).
static std::filesystem::path showSavePanel(const std::string& defaultName)
{
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = @"Save Map";
    panel.nameFieldStringValue = [NSString stringWithUTF8String:defaultName.c_str()];

    if ([panel runModal] == NSModalResponseOK)
        return std::filesystem::path([[panel.URL path] UTF8String]);
    return {};
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
                         MaterialCatalog&  catalog,
                         ActiveTool&       activeTool,
                         SelectTool&       selectTool,
                         DrawSectorTool&   drawSectorTool,
                         VertexTool&       vertexTool,
                         IEditorTool*&     activeToolPtr,
                         float             gridStep,
                         glm::vec2         lastMouseMapPos,
                         bool&             openNewMapDlg,
                         HelpPanel&        helpPanel)
{
    if (ImGui::BeginMainMenuBar())
    {
        // ── File ──────────────────────────────────────────────────────────────
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Cmd+N"))
                openNewMapDlg = true;

            if (ImGui::MenuItem("Open…", "Cmd+O"))
            {
                const auto path = showOpenPanel();
                if (!path.empty())
                    (void)doc.loadFromFile(path);  // failure already logged to OutputLog
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Set Asset Root\xe2\x80\xa6"))
            {
                const auto path = showOpenPanelForDirectory();
                if (!path.empty())
                {
                    doc.setAssetRoot(path.string());
                    catalog.setRoot(path);
                    catalog.scan();
                }
            }

            ImGui::Separator();
            const bool hasSavePath = !doc.filePath().empty();
            if (ImGui::MenuItem("Save", "Cmd+S", false, hasSavePath && doc.isDirty()))
                (void)doc.saveToCurrentPath();  // failure already logged to OutputLog

            if (ImGui::MenuItem("Save As…", "Cmd+Shift+S"))
            {
                const std::string defaultName = doc.filePath().empty()
                    ? "untitled.dmap"
                    : doc.filePath().filename().string();
                const auto path = showSavePanel(defaultName);
                if (!path.empty())
                    (void)doc.saveToFile(path);  // failure already logged to OutputLog
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

            const bool hasSectorSel =
                doc.selection().type == SelectionType::Sector &&
                !doc.selection().sectors.empty();

            if (ImGui::MenuItem("Copy", "Cmd+C", false, hasSectorSel))
                doc.copySector(doc.selection().sectors.front());

            if (ImGui::MenuItem("Paste", "Cmd+V", false, doc.hasClipboard()))
                doc.pushCommand(std::make_unique<CmdPasteSector>(
                    doc, *doc.clipboard(),
                    glm::vec2{gridStep, gridStep}));

            if (ImGui::MenuItem("Duplicate", "Cmd+D", false, hasSectorSel))
                doc.pushCommand(std::make_unique<CmdDuplicateSector>(
                    doc,
                    doc.selection().sectors.front(),
                    glm::vec2{gridStep, gridStep}));

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

    constexpr int INIT_W = 1440;
    constexpr int INIT_H = 900;

    SDL_Window* window = SDL_CreateWindow("DaedalusEdit",
                                          INIT_W, INIT_H,
                                          SDL_WINDOW_METAL |
                                          SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

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

    metalLayer.device      = mtlDevice;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename  = "daedalusedit.ini";

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForMetal(window);
    ImGui_ImplMetal_Init(mtlDevice);

    // ── Editor state ──────────────────────────────────────────────────────────
    const std::string shaderLibPath = executableDir() + "/daedalus_shaders.metallib";

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
    RenderSettingsPanel renderPanel([]() -> std::filesystem::path
    {
        return showOpenPanelForImage();
    });
    ObjectBrowserPanel  objBrowser;
    LayersPanel         layersPanel;
    HelpPanel           helpPanel;
    NewMapDialogState   nmDlg;
    MaterialCatalog     catalog;
    AssetBrowserPanel   assetBrowser;
    std::string         lastKnownAssetRoot;

    bool layoutBuilt = false;

    // ── Window title tracking ─────────────────────────────────────────────────
    // Update the OS title when dirty state or file path changes.
    bool        lastDirty = false;
    std::string lastPath;

    // ── Main loop ─────────────────────────────────────────────────────────────
    bool running = true;
    while (running)
    {
        // ── Event processing ─────────────────────────────────────────────────
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
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

                // Shortcuts (only when ImGui is not capturing keyboard input).
                if (!io.WantCaptureKeyboard)
                {
                    // Tab: toggle 3D viewport mouselook — always processed.
                    if (sc == SDL_SCANCODE_TAB)
                    {
                        vp3d.setMouseCapture(window, !vp3d.isMouseCaptured());
                    }
                    // Escape: release mouselook first; otherwise cancel draw / deselect.
                    else if (sc == SDL_SCANCODE_ESCAPE)
                    {
                        if (vp3d.isMouseCaptured())
                            vp3d.setMouseCapture(window, false);
                        else if (activeToolId == ActiveTool::DrawSector)
                            drawSectorTool.cancel();
                        else
                            doc.selection().clear();
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
                            if (sel.type == SelectionType::Wall)
                            {
                                const world::SectorId sid = sel.wallSectorId;
                                const std::size_t     wi  = sel.wallIndex;

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
                            // L — Place a point light at the last 2D-viewport cursor position.
                            const glm::vec2 mp = vp2d.lastMouseMapPos();
                            LightDef ld;
                            ld.position = {mp.x, 2.0f, mp.y};
                            doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
                        }
                        else if (sc == SDL_SCANCODE_E)
                        {
                            // Place a default entity at the last 2D-viewport cursor position.
                            const glm::vec2 mp = vp2d.lastMouseMapPos();
                            EntityDef ed;
                            ed.position = {mp.x, 0.5f, mp.y};
                            doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, ed));
                        }
                        else if (sc == SDL_SCANCODE_T)
                        {
                            // T: translate gizmo — consumed by 3D viewport when hovered+entity.
                            if (vp3d.isHovered() &&
                                doc.selection().type == SelectionType::Entity)
                                vp3d.setGizmoMode(GizmoMode::Translate);
                            else
                                doc.log("T — Translate: select an entity while hovering the 3D viewport.");
                        }
                        else if (sc == SDL_SCANCODE_Y)
                        {
                            // Y: scale gizmo — consumed by 3D viewport when hovered+entity.
                            if (vp3d.isHovered() &&
                                doc.selection().type == SelectionType::Entity)
                                vp3d.setGizmoMode(GizmoMode::Scale);
                            else
                                doc.log("Y — Scale: select an entity while hovering the 3D viewport.");
                        }
                        else if (sc == SDL_SCANCODE_R)
                        {
                            const SelectionState& sel = doc.selection();
                            if (vp3d.isHovered() &&
                                sel.type == SelectionType::Entity)
                            {
                                // R in 3D viewport: rotate gizmo.
                                vp3d.setGizmoMode(GizmoMode::Rotate);
                            }
                            else if (sel.type == SelectionType::Sector &&
                                     !sel.sectors.empty())
                            {
                                // R elsewhere: rotate selected sector.
                                vp2d.openRotatePopup(sel.sectors.front());
                            }
                            else
                            {
                                doc.log("R — Rotate: select an entity (3D) or sector (2D).");
                            }
                        }
                        else if (sc == SDL_SCANCODE_F5)
                        {
                            // Save the current map (to its file path, or to a
                            // temporary file if it has never been saved).
                            std::filesystem::path mapPath = doc.filePath();
                            if (mapPath.empty())
                                mapPath = std::filesystem::temp_directory_path()
                                          / "daedalus_playtest.dmap";

                            if (!doc.saveToFile(mapPath))
                            {
                                doc.log("F5 — Test Map: save failed.");
                            }
                            else
                            {
                                // DaedalusApp lives in ../app/ relative to this
                                // editor executable (both are in the build tree).
                                const std::filesystem::path appPath =
                                    (std::filesystem::path(executableDir())
                                     / ".." / "app" / "DaedalusApp")
                                    .lexically_normal();

                                if (!std::filesystem::exists(appPath))
                                {
                                    doc.log(std::format(
                                        "F5 — Test Map: DaedalusApp not found at '{}'.",
                                        appPath.string()));
                                }
                                else
                                {
                                    NSString* appNS = [NSString
                                        stringWithUTF8String:appPath.string().c_str()];
                                    NSString* mapNS = [NSString
                                        stringWithUTF8String:mapPath.string().c_str()];

                                    NSTask* task = [[NSTask alloc] init];
                                    task.executableURL =
                                        [NSURL fileURLWithPath:appNS];
                                    task.arguments = @[mapNS];

                                    NSError* launchErr = nil;
                                    if ([task launchAndReturnError:&launchErr])
                                    {
                                        doc.log(std::format(
                                            "F5 — Test Map launched: '{}'.",
                                            mapPath.string()));
                                    }
                                    else
                                    {
                                        const char* why = launchErr
                                            ? launchErr.localizedDescription.UTF8String
                                            : "unknown error";
                                        doc.log(std::format(
                                            "F5 — Test Map launch failed: {}.", why));
                                    }
                                }
                            }
                        }
                        else if (sc == SDL_SCANCODE_F1)
                        {
                            helpPanel.toggle();
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
                            if (sel.type == SelectionType::Sector &&
                                !sel.sectors.empty())
                            {
                                // Delete sectors in reverse index order so earlier
                                // indices stay valid as we remove later ones.
                                std::vector<world::SectorId> sorted = sel.sectors;
                                std::sort(sorted.begin(), sorted.end(),
                                          std::greater<world::SectorId>{});
                                for (const world::SectorId sid : sorted)
                                    doc.pushCommand(
                                        std::make_unique<CmdDeleteSector>(doc, sid));
                            }
                            else if (sel.type == SelectionType::Light)
                            {
                                doc.pushCommand(
                                    std::make_unique<CmdDeleteLight>(doc, sel.lightIndex));
                            }
                            else if (sel.type == SelectionType::Entity)
                            {
                                doc.pushCommand(
                                    std::make_unique<CmdDeleteEntity>(doc, sel.entityIndex));
                            }
                            else if (sel.type == SelectionType::PlayerStart)
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
                        else if (sc == SDL_SCANCODE_HOME)
                        {
                            vp2d.fitToView(doc.mapData());
                        }
                        else if (sc == SDL_SCANCODE_P && !vp3d.isHovered())
                        {
                            // Place a point light at the last 2D-viewport cursor position.
                            // Suppressed when the 3D viewport is hovered: P is reserved
                            // there for snapping the camera to the player start.
                            const glm::vec2 mp = vp2d.lastMouseMapPos();
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
                        if (sel.type == SelectionType::Sector && !sel.sectors.empty())
                            doc.copySector(sel.sectors.front());
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
                        if (sel.type == SelectionType::Sector && !sel.sectors.empty())
                        {
                            doc.pushCommand(
                                std::make_unique<CmdDuplicateSector>(
                                    doc,
                                    sel.sectors.front(),
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

        // ── Acquire next drawable from the Metal layer ────────────────────────
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

        // ── Asset catalog: rescan if doc's assetRoot changed (e.g. after load) ──
        {
            const std::string ar = doc.assetRoot();
            if (ar != lastKnownAssetRoot)
            {
                lastKnownAssetRoot = ar;
                if (!ar.empty())
                {
                    catalog.setRoot(std::filesystem::path(ar));
                    catalog.scan();
                }
            }
        }

        // ── Menu bar ──────────────────────────────────────────────────────────
        drawMenuBar(doc, catalog, activeToolId, selectTool, drawSectorTool, vertexTool, activeTool,
                    vp2d.gridStep(), vp2d.lastMouseMapPos(), nmDlg.open, helpPanel);

        // ── Panels ────────────────────────────────────────────────────────────
        DrawSectorTool* drawToolPtr =
            (activeToolId == ActiveTool::DrawSector) ? &drawSectorTool : nullptr;
        SelectTool* selectToolPtr =
            (activeToolId == ActiveTool::Select) ? &selectTool : nullptr;

        vp2d.draw(doc, activeTool, drawToolPtr, selectToolPtr);
        vp3d.draw(doc, *assetLoader, *device, *queue, catalog, assetBrowser);
        inspector.draw(doc, catalog, *device, *assetLoader, assetBrowser);
        assetBrowser.draw(catalog, *device, *assetLoader);
        renderPanel.draw(doc);
        objBrowser.draw(doc, vp2d.lastMouseMapPos());
        layersPanel.draw(doc);
        outputLog.draw(doc);
        helpPanel.draw();

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

        // ── Window title ──────────────────────────────────────────────────────
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
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
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
