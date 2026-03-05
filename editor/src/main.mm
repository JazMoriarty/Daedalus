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

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_metal.h"

#include "daedalus/render/create_render_device.h"
#include "daedalus/render/rhi/i_render_device.h"
#include "daedalus/render/rhi/i_command_queue.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/i_editor_tool.h"

#include "tools/select_tool.h"
#include "tools/draw_sector_tool.h"
#include "panels/viewport_2d.h"
#include "panels/viewport_3d.h"
#include "panels/property_inspector.h"
#include "panels/output_log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <filesystem>
#include <memory>
#include <string>

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

    ImGui::DockBuilderDockWindow("2D Viewport", dockLeft);
    ImGui::DockBuilderDockWindow("3D Viewport", dock3D);
    ImGui::DockBuilderDockWindow("Properties",  dockProps);
    ImGui::DockBuilderDockWindow("Output",       dockOutput);

    ImGui::DockBuilderFinish(dockspaceId);
}

// ─── Menu bar ─────────────────────────────────────────────────────────────────

enum class ActiveTool { Select, DrawSector };

static void drawMenuBar(EditMapDocument& doc,
                         ActiveTool&       activeTool,
                         SelectTool&       selectTool,
                         DrawSectorTool&   drawSectorTool,
                         IEditorTool*&     activeToolPtr)
{
    if (ImGui::BeginMainMenuBar())
    {
        // ── File ──────────────────────────────────────────────────────────────
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Cmd+N"))
                doc.newMap();

            if (ImGui::MenuItem("Open…", "Cmd+O"))
            {
                const auto path = showOpenPanel();
                if (!path.empty())
                    doc.loadFromFile(path);
            }

            ImGui::Separator();

            const bool hasSavePath = !doc.filePath().empty();
            if (ImGui::MenuItem("Save", "Cmd+S", false, hasSavePath && doc.isDirty()))
                doc.saveToCurrentPath();

            if (ImGui::MenuItem("Save As…", "Cmd+Shift+S"))
            {
                const std::string defaultName = doc.filePath().empty()
                    ? "untitled.dmap"
                    : doc.filePath().filename().string();
                const auto path = showSavePanel(defaultName);
                if (!path.empty())
                    doc.saveToFile(path);
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

            ImGui::EndMenu();
        }

        // ── Tools ─────────────────────────────────────────────────────────────
        if (ImGui::BeginMenu("Tools"))
        {
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

            ImGui::EndMenu();
        }

        // ── Status bar ────────────────────────────────────────────────────────
        const char* toolName =
            activeTool == ActiveTool::DrawSector ? "Draw Sector" : "Select";
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
    auto device = rhi::createRenderDevice();
    auto queue  = device->createCommandQueue("Editor Queue");

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

    Viewport2D      vp2d;
    Viewport3D      vp3d(shaderLibPath);
    PropertyInspector inspector;
    OutputLog         outputLog;

    bool layoutBuilt = false;

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

                // Tool shortcuts (only when ImGui is not capturing keyboard).
                if (!io.WantCaptureKeyboard)
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
                    else if (sc == SDL_SCANCODE_ESCAPE)
                    {
                        // Escape: cancel draw or deselect.
                        if (activeToolId == ActiveTool::DrawSector)
                            drawSectorTool.cancel();
                        else
                            doc.selection().clear();
                    }
                    else if (sc == SDL_SCANCODE_RETURN ||
                             sc == SDL_SCANCODE_KP_ENTER)
                    {
                        if (activeToolId == ActiveTool::DrawSector)
                            drawSectorTool.tryFinish(doc);
                    }
                    // Keys 1–8: set grid step.
                    else
                    {
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
                    // Cmd+S save.
                    if (sc == SDL_SCANCODE_S && !doc.filePath().empty())
                        doc.saveToCurrentPath();
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

        // ── Menu bar ──────────────────────────────────────────────────────────
        drawMenuBar(doc, activeToolId, selectTool, drawSectorTool, activeTool);

        // ── Panels ────────────────────────────────────────────────────────────
        DrawSectorTool* drawToolPtr =
            (activeToolId == ActiveTool::DrawSector) ? &drawSectorTool : nullptr;

        vp2d.draw(doc, activeTool, drawToolPtr);
        vp3d.draw(doc, *device, *queue);
        inspector.draw(doc);
        outputLog.draw(doc);

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
    queue.reset();
    device.reset();

    SDL_Metal_DestroyView(metalView);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
