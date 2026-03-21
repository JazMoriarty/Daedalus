#include "help_panel.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace daedalus::editor
{

// ─── Formatting helpers ───────────────────────────────────────────────────────

/// Render a keyboard key / shortcut label in a bright accent colour.
static void Key(const char* k)
{
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
    ImGui::Text("[%s]", k);
    ImGui::PopStyleColor();
}

/// Start a new line, then render a key badge followed by a description.
static void KeyRow(const char* k, const char* desc)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
    ImGui::Text("  %-18s", (std::string("[") + k + "]").c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(desc);
}

/// Render a numbered walkthrough step.
static void Step(int n, const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.90f, 0.55f, 1.0f));
    ImGui::Text("  %d.", n);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text);
}

/// A dimmed tip / note line.
static void Tip(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.0f));
    ImGui::TextWrapped("   Note: %s", text);
    ImGui::PopStyleColor();
}

/// A bullet point.
static void Bullet(const char* text)
{
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text);
}

/// A sub-bullet (indented one level).
static void SubBullet(const char* text)
{
    ImGui::Indent(16.0f);
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text);
    ImGui::Unindent(16.0f);
}

// ─── Category table ───────────────────────────────────────────────────────────

static const char* k_categories[] =
{
    "Quick Start",
    "Keyboard Reference",
    "2D Viewport",
    "3D Viewport",
    "Tools",
    "Sectors & Walls",
    "Advanced Geometry",
    "Detail Geometry",
    "Entities",
    "Lights",
    "Player Start",
    "Portals & Layers",
    "Prefabs",
    "Asset Browser",
    "Render Settings",
    "Map Doctor",
};
static constexpr int k_numCategories = static_cast<int>(
    sizeof(k_categories) / sizeof(k_categories[0]));

// ─── draw ─────────────────────────────────────────────────────────────────────

void HelpPanel::draw()
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(920.0f, 680.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(600.0f, 400.0f),
                                        ImVec2(2000.0f, 2000.0f));

    if (!ImGui::Begin("DaedalusEdit Help   [F1]", &m_open))
    {
        ImGui::End();
        return;
    }

    // ── Sidebar ───────────────────────────────────────────────────────────────
    ImGui::BeginChild("##HelpSidebar",
                      ImVec2(168.0f, 0.0f),
                      ImGuiChildFlags_Borders);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.65f, 0.25f, 1.0f));
    ImGui::TextUnformatted("  DAEDALUS EDIT");
    ImGui::PopStyleColor();
    ImGui::TextDisabled("  Help & Reference");
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < k_numCategories; ++i)
    {
        ImGui::PushID(i);
        const bool selected = (m_category == i);
        if (ImGui::Selectable(k_categories[i], selected,
                              ImGuiSelectableFlags_None,
                              ImVec2(0.0f, 18.0f)))
        {
            m_category = i;
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("  Press [F1] to close");

    ImGui::EndChild();

    ImGui::SameLine();

    // ── Content area ─────────────────────────────────────────────────────────
    ImGui::BeginChild("##HelpContent",
                      ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders);
    ImGui::Spacing();

    switch (m_category)
    {
    case  0: drawQuickStart();       break;
    case  1: drawKeyboard();         break;
    case  2: draw2DViewport();       break;
    case  3: draw3DViewport();       break;
    case  4: drawTools();            break;
    case  5: drawSectorsWalls();     break;
    case  6: drawAdvancedGeometry(); break;
    case  7: drawDetailGeometry();   break;
    case  8: drawEntities();         break;
    case  9: drawLights();           break;
    case 10: drawPlayerStart();      break;
    case 11: drawPortalsLayers();    break;
    case 12: drawPrefabs();          break;
    case 13: drawAssetBrowser();     break;
    case 14: drawRenderSettings();   break;
    case 15: drawMapDoctor();        break;
    default: break;
    }

    ImGui::Spacing();
    ImGui::EndChild();

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Quick Start
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawQuickStart()
{
    ImGui::SeparatorText("QUICK START  —  Building Your First Level");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Follow these steps to create a playable room, link it to a second "
        "room through a portal, add a player spawn point, place some lights, "
        "and test the result in the engine.");

    // ─ Step 1: New map ───────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Step 1  —  Create a New Map");
    Step(1, "Open the File menu and choose New, or press");
    Key("Cmd+N");
    ImGui::Text(".");
    Step(2, "In the dialog, enter a Map Name and Author.");
    Step(3, "Set the Default Floor Height (e.g. 0.0) and Default Ceiling "
            "Height (e.g. 4.0).  These become the defaults for every new "
            "sector you draw.");
    Step(4, "Optionally set Gravity (default 9.81 m/s^2), Sky Path (path to "
            "an HDR sky texture), and Ambient Color for the initial global "
            "fill light.");
    Step(5, "Click Create.");

    // ─ Step 2: Draw your first room ──────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Step 2  —  Draw Your First Room");
    Step(1, "Press");
    Key("D");
    ImGui::SameLine(0, 2);
    ImGui::Text("to activate the Draw Sector tool.  The cursor changes to a crosshair.");
    Step(2, "Click four points in the 2D viewport to trace the corners of a "
            "room — click counter-clockwise when viewed from above, starting at e.g. "
            "(-5, -5), (-5, 5), (5, 5), (5, -5).");
    Step(3, "Press");
    Key("Enter");
    ImGui::SameLine(0, 2);
    ImGui::Text("or");
    Key("Return");
    ImGui::SameLine(0, 2);
    ImGui::Text("to close the shape and create the sector.");
    Tip("Press Escape at any point to cancel and discard the in-progress shape.");
    Step(4, "The new sector appears in both the 2D and 3D viewports.  Press");
    Key("F");
    ImGui::SameLine(0, 2);
    ImGui::Text("while hovering the 3D viewport to frame it.");

    // ─ Step 3: Add a second room ─────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Step 3  —  Add a Second Room");
    Step(1, "Still in the Draw Sector tool, draw a second sector that shares "
            "one wall with the first.  The shared edge must be exactly "
            "co-linear and the same length for portal linking to work.");
    Tip("Use the grid (toggle with G, set snap with keys 1-8) to keep "
        "vertices aligned.");

    // ─ Step 4: Link the portal ───────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Step 4  —  Connect the Rooms with a Portal");
    Step(1, "Press");
    Key("S");
    ImGui::SameLine(0, 2);
    ImGui::Text("to switch to the Select tool.");
    Step(2, "Click on the shared wall between the two rooms in the 2D viewport.  "
            "The wall turns highlighted.");
    Step(3, "In the Properties panel, scroll to the Portal section and click "
            "Link Portal.  The editor automatically finds the matching wall on "
            "the adjacent sector and links both sides.");
    Step(4, "In the 3D viewport you can now walk through the opening between "
            "the two rooms.");
    Tip("If Link Portal shows a message that no matching wall was found, the "
        "wall vertices do not align exactly.  Use the Vertex tool (V) to "
        "correct them.");

    // ─ Step 5: Place a player start ──────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Step 5  —  Place the Player Start");
    Step(1, "Position the 2D viewport cursor inside one of the rooms.");
    Step(2, "Open the Object Browser panel (tab in the Properties column) and "
            "expand the Player Start section.  Click Set Here.");
    Step(3, "A directional arrow icon appears in the 2D viewport.  "
            "Drag it to reposition.  Right-click-drag to rotate the yaw.");
    Step(4, "In the 3D viewport, press");
    Key("P");
    ImGui::SameLine(0, 2);
    ImGui::Text("while hovering it to snap the camera to the player's perspective.");

    // ─ Step 6: Add lights ────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Step 6  —  Add Lights");
    Step(1, "Move the 2D cursor to the centre of a room and press");
    Key("L");
    ImGui::SameLine(0, 2);
    ImGui::Text("to drop a point light there, or use Object Browser > Lights.");
    Step(2, "Click the light icon in the 2D viewport to select it, then "
            "adjust its colour, radius and intensity in the Properties panel.");
    Tip("Lights are rendered in the 3D viewport in real time; adjust them "
        "while watching the live 3D view.");

    // ─ Step 7: Set sector properties ─────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Step 7  —  Tune Sector Properties");
    Step(1, "Press S to use the Select tool, click a sector in the 2D viewport.");
    Step(2, "In Properties, adjust Floor Height and Ceiling Height to shape the space.");
    Step(3, "Expand the Lighting section to set per-sector ambient colour and intensity.");
    Step(4, "Check Outdoors in the Flags section for sectors that should receive "
            "sunlight from the Render Settings panel.");

    // ─ Step 8: Test in engine ────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Step 8  —  Test in the Engine");
    Step(1, "Save the map with");
    Key("Cmd+S");
    ImGui::Text(".");
    Step(2, "Press");
    Key("\\");
    ImGui::SameLine(0, 2);
    ImGui::Text("to compile the map to a .dlevel bundle and launch DaedalusApp.");
    Tip("\\ always compiles the current in-memory state to a temporary "
        ".dlevel file in the system temp directory — you do not need to "
        "save the .dmap first.");
    Tip("The bundle embeds: map geometry, lights, player start, all entity "
        "visual descriptors (8 visual types), physics shapes, Lua scripts, "
        "audio settings, and every referenced texture as RGBA8 pixel data "
        "keyed by material UUID.");

    // ─ Tips ──────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Useful Habits");
    Bullet("Undo (Cmd+Z) and Redo (Cmd+Shift+Z) work for every edit including "
           "sector drawing, light placement and property changes.");
    Bullet("Run Map Doctor (Cmd+Shift+M) before testing to catch duplicate "
           "vertices, degenerate sectors and orphaned portals.");
    Bullet("Use Layers (Layers panel) to organise large maps — hide geometry "
           "you are not currently working on.");
    Bullet("Prefabs (Object Browser > Prefabs) let you stamp reusable room "
           "templates across the map in seconds.");
    Bullet("Set an Asset Root (File > Set Asset Root\xe2\x80\xa6) to browse material "
           "thumbnails in the Asset Browser panel and drag-assign them to "
           "surfaces in the 3D viewport.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard Reference
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawKeyboard()
{
    ImGui::SeparatorText("KEYBOARD REFERENCE");
    ImGui::Spacing();

    ImGui::SeparatorText("File");
    KeyRow("Cmd+N",        "New map (opens dialog)");
    KeyRow("Cmd+O",        "Open map file");
    KeyRow("Cmd+S",        "Save to current path");
    KeyRow("Cmd+Shift+S",  "Save As…");
    KeyRow("Cmd+Q",        "Quit");

    ImGui::Spacing();
    ImGui::SeparatorText("Edit");
    KeyRow("Cmd+Z",        "Undo");
    KeyRow("Cmd+Shift+Z",  "Redo");
    KeyRow("Cmd+A",        "Select all sectors");
    KeyRow("Cmd+C",        "Copy selected sector");
    KeyRow("Cmd+V",        "Paste copied sector");
    KeyRow("Cmd+D",        "Duplicate selected sector");
    KeyRow("Del / Bksp",   "Delete selected sector, light, entity, or player start");
    KeyRow("Escape",       "Deselect / cancel draw / release mouse capture");

    ImGui::Spacing();
    ImGui::SeparatorText("Tools");
    KeyRow("S",            "Activate Select tool");
    KeyRow("D",            "Activate Draw Sector tool");
    KeyRow("V",            "Activate Vertex tool");
    KeyRow("Enter",        "Finish current sector (Draw Sector tool)");
    KeyRow("W",            "Split selected wall at 2D cursor position (projected onto the wall)");
    KeyRow("Shift+W",      "Split selected wall at its midpoint");
    KeyRow("R",            "Rotate selected sector (2D)  /  Rotate gizmo (3D entity)");

    ImGui::Spacing();
    ImGui::SeparatorText("Placement");
    KeyRow("E",            "Place entity at 2D cursor position");
    KeyRow("L",            "Place point light at 2D cursor position");
    KeyRow("P",            "Place point light at 2D cursor  (when 3D viewport is NOT hovered)");

    ImGui::Spacing();
    ImGui::SeparatorText("2D Viewport");
    KeyRow("Arrow keys",   "Pan the 2D view  (Shift = 5\xc3\x97 speed)");
    KeyRow("G",            "Toggle grid visibility");
    KeyRow("Home",         "Fit entire map to 2D view");
    KeyRow("1 \xe2\x80\x93 8",        "Set grid snap step (0.25, 0.5, 1, 2, 4, 8, 16, 32 units)");

    ImGui::Spacing();
    ImGui::SeparatorText("3D Viewport");
    KeyRow("Tab",          "Toggle fly mode (captures/releases mouse)");
    KeyRow("Escape",       "Release fly mode");
    KeyRow("W A S D",      "Move forward / left / backward / right  (fly mode only)");
    KeyRow("Q / E",        "Move down / up  (fly mode only)");
    KeyRow("Shift",        "Hold for 5× faster movement  (fly mode only)");
    KeyRow("F",            "Frame selected content (or whole map if nothing selected)");
    KeyRow("P",            "Snap camera to player start position and yaw  (when 3D viewport IS hovered)");
    KeyRow("Alt+LMB drag", "Orbit around pivot");
    KeyRow("Scroll",       "Dolly forward / backward");

    ImGui::Spacing();
    ImGui::SeparatorText("3D Gizmos (entity must be selected)");
    KeyRow("T",            "Translate gizmo — drag coloured arrows to move");
    KeyRow("Y",            "Scale gizmo — drag coloured squares to scale");
    KeyRow("R",            "Rotate gizmo — drag yaw ring to rotate");

    ImGui::Spacing();
    ImGui::SeparatorText("Advanced Geometry");
    KeyRow("LMB drag",     "Drag a wall midpoint handle (cyan circle) to add / edit a Bezier curve");

    ImGui::Spacing();
    ImGui::SeparatorText("Terrain Paint  (fly mode + Heightfield sector selected)");
    Tip("These are not keyboard shortcuts but active controls while fly mode is running.");
    KeyRow("LMB hold",     "Raise terrain within brush radius");
    KeyRow("RMB hold",     "Lower terrain within brush radius");
    KeyRow("Shift+LMB",    "Smooth terrain (blend toward local average)");
    KeyRow("Alt+LMB",      "Flatten terrain (stamp the height at the brush anchor)");
    KeyRow("Scroll",       "Adjust brush radius  (0.25 \xe2\x80\x93 20 m)");

    ImGui::Spacing();
    ImGui::SeparatorText("Vertex Height Handles  (3D viewport, Vertex selection, no fly mode)");
    KeyRow("Scroll",       "Raise / lower a hovered floor or ceiling handle by 0.25 m per tick");

    ImGui::Spacing();
    ImGui::SeparatorText("Map Tools");
    KeyRow("Cmd+Shift+M",  "Run Map Doctor");
    KeyRow("\\",            "Save + launch map in DaedalusApp (play test)");
    KeyRow("F1",           "Open / close this Help window");
}

// ─────────────────────────────────────────────────────────────────────────────
// 2D Viewport
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::draw2DViewport()
{
    ImGui::SeparatorText("2D VIEWPORT");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "The 2D viewport is the primary map-authoring canvas.  It shows a "
        "top-down (XZ plane) view of all sectors, walls, entities, lights, "
        "and the player start.  All geometry drawing and selection happens here.");

    ImGui::Spacing();
    ImGui::SeparatorText("Navigation");
    Bullet("Arrow keys — pan the view  (hold Shift for 5\xc3\x97 speed).");
    Bullet("Middle-mouse drag — pan the view.");
    Bullet("Scroll wheel — zoom in / out centred on the cursor.");
    Bullet("Home key — fit the entire map to the viewport.");

    ImGui::Spacing();
    ImGui::SeparatorText("Grid");
    Bullet("Press G to toggle the grid overlay.");
    Bullet("Keys 1–8 set the snap grid step: "
           "1=0.25, 2=0.5, 3=1, 4=2, 5=4, 6=8, 7=16, 8=32 world units.");
    Tip("All vertex placement and dragging snaps to the current grid step.");

    ImGui::Spacing();
    ImGui::SeparatorText("Selection (Select tool)");
    Bullet("Left-click a sector interior — selects that sector.");
    Bullet("Left-click a wall segment — selects that wall.");
    Bullet("Left-click a vertex (white dot) — selects that vertex.");
    Bullet("Left-click a light icon — selects that light.");
    Bullet("Left-click an entity icon — selects that entity.");
    Bullet("Left-click the player start arrow — selects the player start.");
    Bullet("Clicking empty space deselects everything.");

    ImGui::Spacing();
    ImGui::SeparatorText("Player Start Interaction");
    Bullet("Left-click the player start icon to select it.");
    Bullet("Left-click-drag the icon to move it to a new position.");
    Bullet("Right-click-drag the icon to rotate the player's facing direction (yaw).");
    Tip("The facing arrow shows which direction the player will look at spawn.");

    ImGui::Spacing();
    ImGui::SeparatorText("Entity Interaction");
    Bullet("Left-click an entity icon to select it.");
    Bullet("Left-click-drag to reposition an entity.");
    Tip("Use the 3D gizmos (T/R/Y keys in the 3D viewport) for precise entity transforms.");

    ImGui::Spacing();
    ImGui::SeparatorText("Light Interaction");
    Bullet("Left-click a light icon to select it.");
    Bullet("Left-click-drag to reposition a light.");
    Bullet("The coloured circle shows the light's radius at the current zoom level.");

    ImGui::Spacing();
    ImGui::SeparatorText("Bezier Curve Arc Overlay");
    Bullet("Walls with an active Bezier curve handle are drawn as a smooth arc "
           "rather than a straight segment.");
    Bullet("Every wall always shows a small cyan midpoint circle.  The circle "
           "brightens when the cursor is near it.  Click and drag it to create or "
           "move the Bezier control point.  A cyan diamond and stem line show the "
           "active control point position.");

    ImGui::Spacing();
    ImGui::SeparatorText("Detail Brush Footprints");
    ImGui::TextWrapped(
        "Detail brushes placed in a sector are shown as dashed XZ outlines "
        "in the 2D viewport so their footprint is always visible at a glance:");
    Bullet("Box / Wedge \xe2\x80\x94 purple dashed rectangle.");
    Bullet("Cylinder \xe2\x80\x94 green dashed circle.");
    Bullet("Arch Span \xe2\x80\x94 amber dashed rectangle.");
    Tip("The outline is computed from the brush's world-space transform, so it "
        "correctly reflects any rotation or non-uniform scale set in Properties.");

    ImGui::Spacing();
    ImGui::SeparatorText("Floor Layer Dimming  (Floor Layers panel)");
    ImGui::TextWrapped(
        "When the Floor Layers panel (Window > Floor Layers) has height "
        "filtering enabled, sectors whose Y range [floorHeight, ceilHeight] "
        "does not contain the current Edit Height are rendered at 20%% opacity.  "
        "This makes multi-storey maps legible by keeping only the active floor "
        "at full brightness.");
    Bullet("Use the Edit Height slider to sweep through the map vertically.");
    Bullet("Click a Floor Group in the list to jump the slider to that floor's midpoint.");
    Bullet("Toggle Show All to turn filtering off and see everything at once.");
}

// ─────────────────────────────────────────────────────────────────────────────
// 3D Viewport
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::draw3DViewport()
{
    ImGui::SeparatorText("3D VIEWPORT");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "The 3D viewport renders the map through the full deferred PBR pipeline "
        "in real time.  It is a free-flying editorial camera — completely "
        "independent of the player start.  Use it to preview lighting, "
        "geometry, entities, and post-processing effects while you build.");

    ImGui::Spacing();
    ImGui::SeparatorText("Orbit / Dolly Mode  (default)");
    Bullet("Alt + Left-click drag — orbit the view around a pivot point.");
    Bullet("Scroll wheel — dolly forward/backward along the view direction.");
    Tip("When you first orbit, the pivot is set 10 units in front of the camera. "
        "After pressing F, the pivot resets to the centre of the framed content.");

    ImGui::Spacing();
    ImGui::SeparatorText("Fly Mode");
    ImGui::TextWrapped(
        "Fly mode locks the mouse inside the 3D viewport and enables "
        "first-person navigation, exactly like walking through the level.");
    Bullet("Press Tab to enter fly mode.  The cursor disappears.");
    Bullet("W / S — move forward / backward.");
    Bullet("A / D — strafe left / right.");
    Bullet("Q / E — move straight down / up.");
    Bullet("Move the mouse — look around (mouselook).");
    Bullet("Hold Shift — move at 5× speed.");
    Bullet("Press Tab or Escape to release fly mode and restore the cursor.");
    Tip("Fly mode ignores ImGui shortcuts; all keyboard input goes to movement.");

    ImGui::Spacing();
    ImGui::SeparatorText("Camera Shortcuts");
    Bullet("F — frame the currently selected sector(s), or the whole map if "
           "nothing is selected.  Tilts the view 30° downward for a good overview.");
    Bullet("P — instantly snap the camera to the player start position and yaw, "
           "at eye height (1.75 m above the spawn point).  Useful for seeing "
           "exactly what the player will see at game start.");

    ImGui::Spacing();
    ImGui::SeparatorText("Entity Gizmos");
    ImGui::TextWrapped(
        "When an entity is selected, transform gizmos can be activated "
        "while hovering the 3D viewport:");
    Bullet("T — translate gizmo: coloured X/Y/Z arrow handles.");
    Bullet("Y — scale gizmo: coloured X/Y/Z square handles.");
    Bullet("R — rotate gizmo: yaw ring around the entity.");
    Bullet("Drag a handle to transform; release to commit an undoable command.");
    Tip("Gizmos are hidden during fly mode.  Press Tab to exit fly mode first.");

    ImGui::Spacing();
    ImGui::SeparatorText("Terrain Paint Brushes");
    ImGui::TextWrapped(
        "When a Heightfield-floor sector is selected and fly mode is active "
        "(Tab to enter), the 3D viewport switches to terrain paint mode.  "
        "The overlay text at the top confirms the active brush mode.");
    Bullet("Left-mouse hold \xe2\x80\x94 Raise: lifts terrain within brush radius.");
    Bullet("Right-mouse hold \xe2\x80\x94 Lower: pushes terrain down within brush radius.");
    Bullet("Shift + Left-mouse \xe2\x80\x94 Smooth: blends heights toward the local average, "
           "removing spikes and sharpness.");
    Bullet("Alt + Left-mouse \xe2\x80\x94 Flatten: stamps every sample within the radius "
           "to the height directly under the brush centre at drag-start.");
    Bullet("Scroll wheel \xe2\x80\x94 adjusts brush radius from 0.25 m to 20 m.");
    ImGui::TextWrapped(
        "A coloured circle and crosshair show the brush position and radius in world space.  "
        "Green = Raise, Red = Lower, Amber = Smooth or Flatten.  "
        "The entire stroke from first click to release is a single undoable step.");
    Tip("The brush hits the horizontal plane at the sector's floor height.  "
        "Fly at a low angle pointing toward the terrain for best accuracy.");

    ImGui::Spacing();
    ImGui::SeparatorText("Per-Vertex Height Handles");
    ImGui::TextWrapped(
        "When the Vertex tool is active and a vertex is selected, height handles "
        "appear in the 3D viewport at every polygon vertex of the owning sector.  "
        "Hover a handle and scroll the mouse wheel to raise or lower it.");
    Bullet("Cyan handle \xe2\x80\x94 floor height override at that vertex.");
    Bullet("Magenta handle \xe2\x80\x94 ceiling height override at that vertex.");
    Bullet("Each scroll tick adjusts the height by 0.25 m and pushes an undoable command.");
    Tip("Setting a floor override on one vertex and not the others produces a sloped "
        "floor that rises or falls toward that corner.  Use multiple overrides to "
        "create ramps, saddle shapes, and warped rooms.");

    ImGui::Spacing();
    ImGui::SeparatorText("Render Pipeline Preview");
    ImGui::TextWrapped(
        "The 3D viewport uses the same rendering pipeline as DaedalusApp: "
        "deferred shading, PBR materials, volumetric fog, SSR, DoF, motion blur, "
        "colour grading, vignette, film grain, and FXAA.  All settings in the "
        "Render Settings panel are reflected live in this viewport.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Tools
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawTools()
{
    ImGui::SeparatorText("TOOLS");
    ImGui::Spacing();

    // ─ Select ────────────────────────────────────────────────────────
    ImGui::SeparatorText("Select Tool  [S]");
    ImGui::TextWrapped(
        "The default tool.  Click any object in the 2D viewport to select "
        "it and show its properties in the Properties panel.");
    Bullet("Click a sector interior — sector selected; Properties shows "
           "heights, flags, lighting, materials and wall list.");
    Bullet("Click a wall segment — wall selected; Properties shows flags, "
           "UV mapping, materials and portal controls.");
    Bullet("Click a vertex — vertex selected; Properties shows position.  "
           "Drag to move it (Vertex tool is faster for bulk edits).");
    Bullet("Click a light icon — light selected.");
    Bullet("Click an entity icon — entity selected.");
    Bullet("Click the player start arrow — player start selected.");
    Bullet("Click empty space — deselects everything; Properties shows map-level "
           "properties (name, author, global ambient, defaults).");
    KeyRow("Cmd+A", "Select all sectors at once.");
    KeyRow("R",     "Open Rotate Sector dialog when a sector is selected.");

    ImGui::Spacing();
    // ─ Draw Sector ───────────────────────────────────────────────────
    ImGui::SeparatorText("Draw Sector Tool  [D]");
    ImGui::TextWrapped(
        "Creates new convex or concave sectors by placing vertices one click "
        "at a time.  Sectors are the fundamental building blocks of every room, "
        "corridor, and outdoor space.");
    Bullet("Left-click to place each vertex.  Vertices snap to the current grid.");
    Bullet("A preview line is drawn from the last vertex to the cursor so you "
           "can visualise the next edge.");
    Bullet("Press Enter or Return to close the shape: the final edge connects "
           "the last vertex back to the first and the sector is created.");
    Bullet("Press Escape to cancel and discard all placed vertices.");
    Tip("Sectors should be drawn counter-clockwise when viewed from above (XZ plane). "
        "The engine derives wall normals from this winding; reversed winding causes "
        "back-faces to point inward.");
    Tip("Keep sectors convex where possible.  The portal traversal system "
        "handles concave rooms but keeps performance best with simple shapes.");

    ImGui::Spacing();
    // ─ Vertex ────────────────────────────────────────────────────────
    ImGui::SeparatorText("Vertex Tool  [V]");
    ImGui::TextWrapped(
        "Allows direct manipulation of sector vertices without switching to "
        "the Select tool.");
    Bullet("Hover over any vertex (white dot) to highlight it.");
    Bullet("Left-click-drag to move the vertex; it snaps to the grid.");
    Bullet("Moving a vertex simultaneously updates all sectors that share it, "
           "keeping adjacent rooms watertight.");
    Tip("After moving vertices, run Map Doctor (Cmd+Shift+M) to verify that "
        "no degenerate zero-length walls were created.");
    Tip("Use the Vertex tool to precisely align shared edges between rooms "
        "before attempting to link portals.");

    ImGui::Spacing();
    // ─ Vertex height handles ─────────────────────────────────────────
    ImGui::SeparatorText("Per-Vertex Heights  (Vertex tool)");
    ImGui::TextWrapped(
        "With the Vertex tool active and a vertex selected in the 2D viewport, "
        "height handles appear on every polygon vertex in the 3D viewport.  "
        "Hover a handle and scroll the mouse wheel to raise or lower that vertex's "
        "floor or ceiling independently of the sector scalar.");
    Bullet("Cyan handle — floor height override.  Overrides the sector's Floor Height "
           "at this vertex only, enabling sloped floors, ramps, and angled sills.");
    Bullet("Magenta handle — ceiling height override.  Same idea for the ceiling.");
    Bullet("You can also enable / clear overrides with checkboxes in the Properties "
           "panel > Height Overrides section when a vertex is selected.");
    Tip("Overrides are per-vertex: set one corner high and the rest low to create a "
        "sloped floor.  Adjacent portal wall heights update automatically.");

    ImGui::Spacing();
    // ─ Bezier curve handles ─────────────────────────────────────────
    ImGui::SeparatorText("Bezier Curve Handles  (Select / Draw Sector tool)");
    ImGui::TextWrapped(
        "Every wall has a small cyan midpoint handle circle.  Drag it directly "
        "in the 2D viewport to pull out a Bezier control handle.  The wall "
        "immediately bends into a smooth arc; both the 2D and 3D viewports "
        "update in real time.  No modifier key is required.");
    Bullet("One control point — quadratic Bezier (smooth arc).");
    Bullet("Enable \"Cubic (add B)\" in Properties > Bezier Curve to add a second "
           "control point for an S-curve.");
    Bullet("Subdivision count (4–64) controls arc smoothness; adjust in Properties.");
    Bullet("To remove a curve: uncheck \"Enable Curve\" in Properties > Bezier Curve.");
    Tip("Portal walls can be curved.  The portal clipping window uses the chord "
        "(straight line between endpoints) as a conservative approximation.");

    ImGui::Spacing();
    // ─ Wall operations ─────────────────────────────────────────────────────
    ImGui::SeparatorText("Wall Operations  (Select tool, wall selected)");
    Bullet("W key — split the selected wall at the 2D cursor position "
           "(the cursor is projected onto the wall).  Move the cursor to a "
           "corridor junction point before pressing W to create a sub-wall of "
           "exactly the right width for portal linking.");
    Bullet("Shift+W — split at midpoint regardless of cursor position.");
    Bullet("Properties > Portal > Link Portal — auto-detect and link a "
           "matching co-linear wall on an adjacent sector.");
    Bullet("Properties > Portal > Unlink Portal — remove the portal link on "
           "both sides of the shared wall.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Sectors & Walls
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawSectorsWalls()
{
    ImGui::SeparatorText("SECTORS & WALLS");
    ImGui::Spacing();

    // ─ Sectors ───────────────────────────────────────────────────────
    ImGui::SeparatorText("Sector Properties  (Properties panel)");
    ImGui::TextWrapped(
        "Select a sector with the Select tool to view and edit all of its "
        "properties in the Properties panel.");

    Bullet("Floor Height / Ceiling Height — drag to adjust.  "
           "All walls and portals use these values for tessellation.");
    Bullet("Walls — each wall is listed below the sector properties.  "
           "Click to expand a wall's flags and UV controls inline.");

    ImGui::Spacing();
    ImGui::SeparatorText("Sector Flags");
    Bullet("Outdoors — sector receives direct sunlight and sun shadows.  "
           "Use for open courtyards, rooftops, and exterior spaces.");
    Bullet("Underwater — applies a blue tint and refraction overlay in the engine.");
    Bullet("Damage Zone — game logic damages the player each frame while inside.");
    Bullet("Trigger Zone — fires a script event when the player enters.");

    ImGui::Spacing();
    ImGui::SeparatorText("Sector Lighting");
    Bullet("Ambient Colour — base emissive tint applied to all surfaces in the sector.");
    Bullet("Intensity — multiplier for the ambient colour (0 = pitch black, "
           "values > 1 are HDR).");
    Tip("Sector ambient is additive on top of any point/spot lights and the "
        "global ambient from Render Settings.");

    ImGui::Spacing();
    ImGui::SeparatorText("Sector Materials");
    Bullet("Floor Material UUID — hexadecimal asset ID for the floor texture.");
    Bullet("Ceiling Material UUID — hexadecimal asset ID for the ceiling texture.");
    Tip("Type or paste a UUID hex value; press X to clear and restore the "
        "default white material.");

    ImGui::Spacing();
    ImGui::SeparatorText("Floor Shape  (Properties panel > Floor Shape)");
    ImGui::TextWrapped(
        "Each sector's floor mesh can be generated in one of three ways:");
    Bullet("Flat (default) — standard flat floor, optionally with per-vertex "
           "height overrides for slopes and ramps.");
    Bullet("Visual Stairs — the tessellator generates a stair-step mesh from "
           "the Stair Profile settings (step count, riser height, tread depth, "
           "direction angle).  Physics collision uses a linear ramp equivalent, "
           "so the player walks smoothly up/down while the visuals show steps.");
    Bullet("Heightfield — the floor is a terrain mesh generated from a sample "
           "grid.  See Advanced Geometry for full details.");
    Tip("The Staircase Generator panel (Window > Staircase Generator) can "
        "apply a Stair Profile to the selected sector automatically, or "
        "generate a chain of portal-linked step sectors.");

    ImGui::Spacing();
    ImGui::SeparatorText("Floor / Ceiling Portals  (Properties panel)");
    ImGui::TextWrapped(
        "A sector can open downward into another sector through its floor, or "
        "upward through its ceiling.  This is the Sector-Over-Sector stacking "
        "system.  See Advanced Geometry for the full workflow.");
    Bullet("Floor portal ID — drag to the ID of the sector visible below.");
    Bullet("Ceiling portal ID — drag to the ID of the sector visible above.");
    Tip("Set the ID to -1 (none) to clear the portal and restore a solid surface.");

    ImGui::Spacing();
    ImGui::SeparatorText("Sector Operations");
    Bullet("Cmd+C — copy the selected sector to the clipboard.");
    Bullet("Cmd+V — paste the clipboard sector at a grid-offset position.");
    Bullet("Cmd+D — duplicate the selected sector.");
    Bullet("Delete / Backspace — delete the selected sector(s).  "
           "Portal links to deleted sectors are automatically cleared.");
    Bullet("R key — rotate the selected sector by a user-specified angle.");

    ImGui::Spacing();
    // ─ Walls ─────────────────────────────────────────────────────────
    ImGui::SeparatorText("Wall Flags  (Properties panel, wall selected)");
    Bullet("Blocking — solid collision wall; the player and physics objects "
           "cannot pass through.");
    Bullet("Two-sided — the wall face is visible from both sides.  "
           "Use for thin pillar walls that can be viewed from either sector.");
    Bullet("Climbable — the player can climb this wall surface (ladder logic).");
    Bullet("Trigger Zone — fires a script event when the player touches the wall.");
    Bullet("Invisible — no geometry is tessellated for this wall; useful for "
           "invisible collision barriers.");
    Bullet("Mirror — the wall's quad is rendered as a planar mirror.  "
           "The 3D viewport renders a reflected view into a 512\xc3\x97" "512 RT each frame.");
    Bullet("No Physics — the wall has no collision shape.  Use for decorative "
           "background panels, non-blocking architectural surfaces, and visual-only "
           "detail walls.");

    ImGui::Spacing();
    ImGui::SeparatorText("Wall UV Mapping  (Properties panel, wall selected)");
    Bullet("UV Offset — shifts the texture in U (horizontal) and V (vertical).");
    Bullet("UV Scale — tiles the texture.  Values > 1 tile more, < 1 stretch.");
    Bullet("UV Rotation — rotates the texture on the wall face (in degrees).  "
           "The Properties panel displays and edits this value in degrees; it is stored "
           "internally as radians in the map format.");

    ImGui::Spacing();
    ImGui::SeparatorText("Wall Materials  (Properties panel, wall selected)");
    Bullet("Front — texture visible from inside the sector.");
    Bullet("Upper — strip above a lower portal opening (step-down rooms).");
    Bullet("Lower — strip below a higher portal opening (step-up rooms).");
    Bullet("Back — texture visible from the adjacent sector through a portal.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Entities
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawEntities()
{
    ImGui::SeparatorText("ENTITIES");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Entities are objects placed in the map: sprites, 3D meshes, voxel "
        "objects, decals, particle systems, and rotated sprites.  Place them "
        "via the Object Browser or by pressing E in the 2D viewport.  "
        "Select and transform them with the gizmos in the 3D viewport.");

    ImGui::Spacing();
    ImGui::SeparatorText("Placing Entities");
    Bullet("E key — places a default entity at the current 2D cursor position.");
    Bullet("Object Browser > Entities — buttons for each type, placed at cursor.");
    Tip("After placing, select the entity and set its Asset Path in Properties "
        "to point to a texture, mesh or VOX file.");

    ImGui::Spacing();
    ImGui::SeparatorText("Visual Types");
    Bullet("Billboard (Cutout) — alpha-tested sprite, always faces the camera.  "
           "Good for trees, torches, pickup items.");
    Bullet("Billboard (Blended) — alpha-blended sprite.  "
           "Good for smoke, holograms, translucent effects.");
    Bullet("Billboard (Animated) — sprite-sheet animation.  "
           "Set Frame Count, Cols, Rows, FPS in the Animation section.");
    Bullet("Voxel Object — voxel sprite from a .vox file, rendered as a "
           "greedy-meshed PBR block model.");
    Bullet("Static Mesh — a full 3D mesh asset (glTF/OBJ).  "
           "Supports PBR materials.");
    Bullet("Rotated Sprite — classic Doom-style directional sprite.  "
           "Choose 8 or 16 directions; each direction is a column in the "
           "sprite sheet.");
    Bullet("Decal — projected albedo + normal map decal stamped onto nearby "
           "geometry (impact marks, blood, paint).  Scale the entity OBB to "
           "set the projection box size.");
    Bullet("Particle Emitter — GPU particle system.  Configure emission rate, "
           "direction, cone, speed, lifetime, colours, size, drag and gravity "
           "in the Particle Emitter section.");

    ImGui::Spacing();
    ImGui::SeparatorText("Transform  (Properties panel)");
    Bullet("Position (X, Y, Z) — world-space position.  Drag or use gizmo.");
    Bullet("Yaw / Pitch / Roll — Euler angles in degrees.  Use the Rotate "
           "gizmo (R key in 3D viewport) for yaw.");
    Bullet("Scale (X, Y, Z) — non-uniform scale.  Use Scale gizmo (Y key).");

    ImGui::Spacing();
    ImGui::SeparatorText("Identity");
    Bullet("Name — optional label used by the script system to find this entity.");
    Bullet("Alignment — Floor, Ceiling, Wall, or Free.  Controls how the "
           "entity sticks to map surfaces at runtime.");
    Bullet("Layer — which editor layer the entity belongs to (for visibility control).");

    ImGui::Spacing();
    ImGui::SeparatorText("Physics  (Properties panel, Physics header)");
    Bullet("Shape — Box, Sphere, or Capsule collision volume.");
    Bullet("Static — if checked, the entity has no physics simulation (immovable).");
    Bullet("Mass — simulation mass in kg for dynamic entities.");

    ImGui::Spacing();
    ImGui::SeparatorText("Script  (Properties panel > Script header)");
    Bullet("Script Path — path to the Lua script file bound to this entity at runtime.");
    Bullet("Exposed Vars — a table of key/value string pairs injected as Lua globals "
           "before the script's init() function runs.  Use these to configure the same "
           "script template differently for each instance (e.g. patrol speed, dialog ID).");

    ImGui::Spacing();
    ImGui::SeparatorText("Audio  (Properties panel > Audio header)");
    Bullet("Sound Path — path to the audio file for this entity's ambient / one-shot sound.");
    Bullet("Volume — playback gain (0.0 = silent, 1.0 = full).");
    Bullet("Falloff Radius — world-space distance at which the sound attenuates to silence.");
    Bullet("Loop — if checked, the sound plays on repeat.");
    Bullet("Auto Play — if checked, the sound starts playing automatically when the level loads.");

    ImGui::Spacing();
    ImGui::SeparatorText("Runtime Export (\\)");
    ImGui::TextWrapped(
        "All entity data — visual type, asset path, transform, physics shape, "
        "Lua script + exposed variables, and audio settings — is compiled into "
        "the .dlevel v4 bundle when you press \\.  The runtime ECS instantiates "
        "matching components automatically: BillboardSpriteComponent, "
        "AnimationStateComponent, StaticMeshComponent, VoxelObjectComponent, "
        "DecalComponent, ParticleEmitterComponent, ScriptComponent, and "
        "AudioSourceComponent.");

    ImGui::Spacing();
    ImGui::SeparatorText("3D Gizmo Controls (entity selected, hover 3D viewport)");
    Bullet("T — translate: red/green/blue arrows for X/Y/Z.");
    Bullet("Y — scale: coloured squares on each axis tip.");
    Bullet("R — rotate: yaw ring dragged horizontally.");
    Tip("Releasing the mouse button commits the change as a single undoable command.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Lights
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawLights()
{
    ImGui::SeparatorText("LIGHTS");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "DaedalusEdit supports two runtime light types: Point and Spot.  "
        "All lights are visible and interactive in both the 2D and 3D viewports.");

    ImGui::Spacing();
    ImGui::SeparatorText("Placing Lights");
    Bullet("L key — places a point light at the current 2D cursor position.");
    Bullet("Object Browser > Lights > Place Point Light — same as L key.");
    Bullet("Object Browser > Lights > Place Spot Light — places a spot light "
           "aimed downward at ceiling height.");
    Bullet("Delete / Backspace — removes the selected light.");

    ImGui::Spacing();
    ImGui::SeparatorText("Light Properties  (Properties panel)");

    Bullet("Type — switch between Point and Spot.  Spot-specific controls "
           "appear when Spot is selected.");
    Bullet("Position (X, Y, Z) — drag to reposition, or drag the light icon "
           "in the 2D viewport.");
    Bullet("Color — RGB light colour.");
    Bullet("Radius — distance in metres over which the light attenuates to zero.");
    Bullet("Intensity — HDR multiplier.  Values > 1 contribute to bloom.");

    ImGui::Spacing();
    ImGui::SeparatorText("Spot Light Additional Properties");
    Bullet("Direction (X, Y, Z) — normalised aim direction; drag to edit.");
    Bullet("Inner Cone — angle in degrees for the fully-lit inner cone.");
    Bullet("Outer Cone — angle in degrees for the penumbra falloff ring.  "
           "Outer must be >= Inner.");
    Bullet("Range — maximum distance the spot light reaches (separate from Radius).");

    ImGui::Spacing();
    ImGui::SeparatorText("Global Ambient");
    ImGui::TextWrapped(
        "The Render Settings panel and the Properties panel (when nothing is "
        "selected) both expose a Global Ambient colour and intensity that "
        "applies uniformly to every surface in the map.  "
        "Individual sectors can override this with their own Ambient Colour.");

    ImGui::Spacing();
    ImGui::SeparatorText("Sun / Directional Light");
    ImGui::TextWrapped(
        "The Render Settings panel (Sun / Ambient section) controls a single "
        "directional 'sun' light.  Only sectors flagged Outdoors receive "
        "direct sunlight.  Interior sectors are unaffected.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Player Start
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawPlayerStart()
{
    ImGui::SeparatorText("PLAYER START");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "The player start is a single marker that tells the engine where the "
        "player spawns and which direction they initially face.  Each map can "
        "have at most one player start.");

    ImGui::Spacing();
    ImGui::SeparatorText("Placing the Player Start");
    Bullet("Object Browser > Player Start > Set Here — places the marker at "
           "the current 2D cursor position with yaw = 0 (facing +Z).");
    Bullet("A directional arrow icon appears in the 2D viewport at the "
           "chosen position.");

    ImGui::Spacing();
    ImGui::SeparatorText("Moving & Rotating in the 2D Viewport");
    Bullet("Left-click the arrow icon to select the player start.");
    Bullet("Left-click-drag the selected icon to move it to a new map position.");
    Bullet("Right-click-drag the selected icon to rotate the facing direction "
           "(yaw).  The arrow tip points in the direction the player will look.");

    ImGui::Spacing();
    ImGui::SeparatorText("Editing in the Properties Panel");
    Bullet("Position (X, Y, Z) — type or drag exact world coordinates.");
    Bullet("Yaw (degrees) — spin box for precise orientation.  "
           "0° = facing +Z, 90° = facing +X.");
    Bullet("Clear Player Start button — removes the marker.");
    Tip("You can also delete the selected player start with Delete / Backspace.");

    ImGui::Spacing();
    ImGui::SeparatorText("Player Start & the 3D Viewport");
    Bullet("Press P while hovering the 3D viewport to snap the camera to the "
           "player's spawn position and yaw at eye height (1.75 m).");
    Bullet("This is purely an editorial preview convenience; the 3D camera is "
           "always a free-fly tool and has no effect on gameplay.");

    ImGui::Spacing();
    ImGui::SeparatorText("How the Engine Uses It");
    ImGui::TextWrapped(
        "When DaedalusApp loads the map it reads the player start position and "
        "yaw to initialise the player controller.  If no player start is set, "
        "the engine places the player at the world origin (0, 0, 0) facing +Z.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Portals & Layers
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawPortalsLayers()
{
    ImGui::SeparatorText("PORTALS");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Portals are shared walls that let the engine see through from one "
        "sector into an adjacent one.  The engine uses portal-based traversal "
        "to cull geometry: only sectors visible through the portal graph are "
        "rendered each frame, keeping performance predictable at any map size.");

    ImGui::Spacing();
    ImGui::SeparatorText("Creating a Portal");
    Step(1, "Draw the corridor so two of its vertices land exactly on the room "
            "wall it will connect to (use grid snapping or the Vertex tool).");
    Step(2, "Select the room wall, then press W twice — once per corridor "
            "junction — to split it into three sub-walls.  Move the cursor to "
            "each junction point before pressing W.  The middle sub-wall will "
            "become the portal opening.");
    Step(3, "Select that middle sub-wall with the Select tool.");
    Step(4, "In the Properties panel, Portal section, click Link Portal.");
    Step(5, "The editor finds the matching corridor wall (same endpoints) and "
            "links both directions automatically.");
    Tip("The two linked walls must have exactly matching endpoint coordinates. "
        "A corridor wall can be narrower than the original room wall — just "
        "split the room wall first so a sub-wall matches the corridor width. "
        "Use Shift+W to split at midpoint, or plain W to split at the cursor.");

    ImGui::Spacing();
    ImGui::SeparatorText("Removing a Portal");
    Bullet("Select either wall of the linked pair and click Unlink Portal in "
           "Properties.  Both sides are cleared simultaneously.");

    ImGui::Spacing();
    ImGui::SeparatorText("Portal Display in 3D");
    ImGui::TextWrapped(
        "Linked portal walls render no geometry — they are transparent openings.  "
        "Step-up / step-down rooms automatically generate upper/lower wall strips "
        "to fill the height difference on either side of the portal.");

    ImGui::Spacing();
    ImGui::SeparatorText("Sector-Over-Sector (SoS) Floor / Ceiling Portals");
    ImGui::TextWrapped(
        "Any sector can open vertically into another sector through its floor or "
        "ceiling.  This creates true multi-storey stacking — a balcony looking "
        "down into a hall, a window in the floor of a room above a lower space, "
        "or a grate you can see through.  The portal system handles vertical "
        "traversal the same way it handles horizontal wall portals.");
    Step(1, "Draw both sectors at the correct floor and ceiling heights.");
    Step(2, "Select the upper sector with the Select tool.");
    Step(3, "In Properties > Floor / Ceiling Portals, drag the \"Floor portal\" "
            "ID field to the index of the lower sector (shown as text next to "
            "the sector in the 2D viewport label, or visible in Properties header).");
    Step(4, "Optionally set a portal material on the floor surface "
            "(e.g. a grate or glass texture) using the Properties panel.");
    Tip("For a transparent opening with no material, leave the material slot "
        "empty.  The portal surface is rendered as a passthrough gap.");
    Tip("The 2D viewport in \"Show All\" mode will show both sectors overlapping. "
        "Enable the Floor Layers panel filter to isolate each floor for editing.");

    ImGui::Spacing();
    ImGui::SeparatorText("Floor Layers Panel  (Window > Floor Layers)");
    ImGui::TextWrapped(
        "The Floor Layers panel makes multi-storey maps legible in the 2D "
        "viewport by dimming sectors that are not on the active working floor.");
    Bullet("Edit Height slider — drag to set the current working Y level.  "
           "Sectors whose [floorHeight, ceilHeight] range does not include this "
           "value are drawn at 20%% opacity.");
    Bullet("Floor Groups list — the editor automatically partitions sectors "
           "into vertical stacks based on floor/ceiling portal links.  Click a "
           "group to jump the Edit Height slider to that floor's midpoint.");
    Bullet("Show All toggle — disables the filter and renders all floors at "
           "full opacity.  Useful for cross-floor portal linking.");
    Bullet("Named Presets — right-click the Edit Height slider to save the "
           "current height as a named preset (Ground, Floor 1, Roof, etc.) for "
           "quick switching during level design.");

    ImGui::Spacing();
    ImGui::SeparatorText("LAYERS");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Layers are purely an editor organisational tool; they have no effect "
        "on the saved map or the engine.  Use layers to hide areas while "
        "working on a specific section of a large map.");

    ImGui::Spacing();
    ImGui::SeparatorText("Layers Panel");
    Bullet("+ Add Layer button — creates a new named layer.");
    Bullet("Eye button (O) — toggles visibility for that layer in the 2D viewport.");
    Bullet("Lock button (L) — prevents accidental selection of sectors on "
           "that layer.");
    Bullet("Sel button — selects all sectors assigned to that layer.");
    Bullet("Click a layer row to make it the active layer.  "
           "All new sectors drawn go onto the active layer.");
    Bullet("X button — deletes the layer (disabled if only one layer exists).");

    ImGui::Spacing();
    ImGui::SeparatorText("Assigning a Sector to a Layer");
    Bullet("New sectors are automatically placed on the active layer at draw time.");
    Bullet("Entity layer assignment is done in Properties > Appearance > Layer.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Prefabs
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawPrefabs()
{
    ImGui::SeparatorText("PREFABS");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Prefabs are named snapshots of one or more selected sectors, including any "
        "entities whose positions fall within the selection bounding box.  "
        "Save a prefab once, then stamp it anywhere in the map with a single click.  "
        "All wall flags, materials, heights, and portal links within the saved "
        "selection are preserved; portal links to sectors outside the selection are "
        "cleared on placement.");

    ImGui::Spacing();
    ImGui::SeparatorText("Saving a Prefab");
    Step(1, "Select one or more sectors in the 2D viewport with the Select tool.");
    Step(2, "Open the Object Browser panel and expand the Prefabs section.");
    Step(3, "Type a name in the text field next to the Save Selection button.");
    Step(4, "Click Save Selection.  The prefab appears in the library list below.");
    Tip("Prefab names do not need to be unique, but descriptive names make "
        "the library easier to manage.");

    ImGui::Spacing();
    ImGui::SeparatorText("Placing a Prefab");
    Step(1, "Position the 2D cursor where you want the prefab's origin to appear.");
    Step(2, "In Object Browser > Prefabs, click Place next to the desired entry.");
    Step(3, "The prefab sectors appear offset from the cursor by the current "
            "grid step so they do not overlap each other.");
    Tip("After placing, select the new sectors and use the Vertex tool to "
        "align shared edges with the surrounding map before linking portals.");

    ImGui::Spacing();
    ImGui::SeparatorText("Prefab Storage");
    ImGui::TextWrapped(
        "Prefabs are stored in the map's sidecar file (*.emap) alongside "
        "the main binary map data (.dmap).  The .emap is a versioned UTF-8 JSON "
        "file that also holds layers, entities, lights, scene settings, render "
        "settings, and the asset root path.  It is written automatically "
        "whenever the map is saved.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Settings
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawRenderSettings()
{
    ImGui::SeparatorText("RENDER SETTINGS");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "The Render Settings panel (tab in the Properties column) controls the "
        "full deferred PBR post-processing pipeline.  All changes are reflected "
        "in the 3D viewport immediately and saved with the map.");

    ImGui::Spacing();
    ImGui::SeparatorText("Sun / Ambient");
    Bullet("Sun Color — colour of the directional sun light.");
    Bullet("Sun Intensity — brightness multiplier (0 = night; 1–2 = day).");
    Bullet("Sun Direction — normalised XYZ vector pointing away from the sun.  "
           "Only affects Outdoors-flagged sectors.");
    Bullet("Ambient Color — global fill colour applied uniformly everywhere.");

    ImGui::Spacing();
    ImGui::SeparatorText("Volumetric Fog");
    Bullet("Density — higher values produce thicker fog.  Start with 0.005–0.02.");
    Bullet("Anisotropy — -1 = scatter back toward camera; +1 = forward scatter "
           "(Mie scattering); 0 = isotropic.");
    Bullet("Scattering — fraction of light that scatters.  1.0 = full scatter.");
    Bullet("Near / Far — fog starts / ends at these camera distances.");
    Bullet("Ambient Fog — tint added to fog from ambient light sources.");

    ImGui::Spacing();
    ImGui::SeparatorText("Screen-Space Reflections (SSR)");
    Bullet("Enabled — toggle SSR for reflective surfaces (roughness < cutoff).");
    Bullet("Max Distance — maximum reflection ray march distance in metres.");
    Bullet("Thickness — depth tolerance for hit detection (avoid leaking).");
    Bullet("Roughness Cutoff — surfaces rougher than this receive no SSR.");
    Bullet("Fade Start — UV margin at which the reflection fades out near edges.");
    Bullet("Max Steps — ray march iteration count; higher = slower + more accurate.");

    ImGui::Spacing();
    ImGui::SeparatorText("Depth of Field");
    Bullet("Focus Distance — distance from the camera to the sharp plane (metres).");
    Bullet("Focus Range — band around the focus plane that stays sharp.");
    Bullet("Bokeh Radius — maximum blur radius in pixels.");
    Bullet("Near / Far Transition — blend distances from sharp to fully blurred.");

    ImGui::Spacing();
    ImGui::SeparatorText("Motion Blur");
    Bullet("Shutter Angle — fraction of a frame exposure (0 = off, 1 = full).");
    Bullet("Samples — TAA/velocity sample count; 8–16 is typical.");

    ImGui::Spacing();
    ImGui::SeparatorText("Colour Grading");
    Bullet("Intensity — blend between identity (0) and the LUT (1).");
    Bullet("LUT Path — path to a 1024×32 RGBA8 PNG strip (32×32×32 3D LUT).  "
           "Click Browse to open a file picker.  Leave empty for identity.");

    ImGui::Spacing();
    ImGui::SeparatorText("Optional FX");
    Bullet("Chromatic Aberration — RGB channel fringe at screen edges (0–0.05).");
    Bullet("Vignette Intensity / Radius — darkens screen corners.");
    Bullet("Film Grain — additive per-pixel noise; adds organic texture.");

    ImGui::Spacing();
    ImGui::SeparatorText("Upscaling / Anti-aliasing");
    Bullet("FXAA — 9-tap fast approximate anti-aliasing.  Inexpensive and "
           "suitable for most maps.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Map Doctor
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawMapDoctor()
{
    ImGui::SeparatorText("MAP DOCTOR");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Map Doctor is an automated map-validation and repair tool.  "
        "Run it before saving or playtesting to catch common authoring errors "
        "that would cause visual glitches or crashes in the engine.");

    ImGui::Spacing();
    ImGui::SeparatorText("Running Map Doctor");
    Bullet("Press Cmd+Shift+M, or choose Tools > Map Doctor from the menu bar.");
    ImGui::TextWrapped("All findings are printed to the Output panel.  "
                       "Click the arrow button next to any finding to jump "
                       "directly to the affected element in the 2D viewport.");

    ImGui::Spacing();
    ImGui::SeparatorText("What It Checks");
    Bullet("Degenerate sectors — sectors with fewer than 3 walls or with "
           "zero-area polygons.");
    Bullet("Zero-length walls — walls whose start and end vertex are at the "
           "same position (caused by overlapping vertex moves).");
    Bullet("Duplicate vertices — two vertices that occupy exactly the same "
           "XZ position in the same sector.");
    Bullet("Orphaned portal links — walls that point to a sector that no "
           "longer exists or to a wall index that is out of range.");
    Bullet("Mismatched portal pairs — wall A links to wall B, but wall B "
           "does not link back to wall A.");
    Bullet("Sector height inversion — floor height >= ceiling height, "
           "which produces a sector with no interior volume.");
    Bullet("Overlapping sectors — two sector polygons that fully overlap "
           "in the XZ plane (only the first will be rendered by the portal system).");

    ImGui::Spacing();
    ImGui::SeparatorText("Auto-Repair");
    ImGui::TextWrapped(
        "Map Doctor repairs some problems automatically (orphaned portal "
        "links are cleared, duplicate vertices are merged) and reports "
        "others as warnings that require manual correction.  "
        "Review the Output panel carefully after each run.");

    ImGui::Spacing();
    ImGui::SeparatorText("Good Habits");
    Bullet("Run Map Doctor after any large structural edit (adding rooms, "
           "splitting walls, moving many vertices).");
    Bullet("A clean Map Doctor run is a good sign before pressing \\ to "
           "launch the engine; unchecked errors can cause the engine to "
           "hang or produce invisible geometry.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset Browser
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawAssetBrowser()
{
    ImGui::SeparatorText("ASSET BROWSER");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "The Asset Browser panel (tab in the Properties column, alongside "
        "Properties, Object Browser, and Render Settings) is a material "
        "thumbnail browser.  It scans a folder tree you designate as the "
        "Asset Root and displays 64x64 thumbnails of every image file found.");

    ImGui::Spacing();
    ImGui::SeparatorText("Setting the Asset Root");
    Bullet("Open File > Set Asset Root\xe2\x80\xa6 and choose your textures folder.  "
           "The catalog rescans automatically whenever the root changes.");
    Bullet("The asset root is persisted in the .emap sidecar, so it is "
           "restored automatically when you reopen the map.");
    Tip("If the panel shows 'No asset root set', use File > Set Asset Root\xe2\x80\xa6 "
        "to point it at your project's textures directory.");

    ImGui::Spacing();
    ImGui::SeparatorText("Browsing");
    Bullet("Left pane — folder tree: click a subfolder name to filter the "
           "thumbnail grid.  Click 'All' to show the complete catalog.");
    Bullet("Right pane — thumbnail grid: 64x64 pixel previews of all "
           "image files in the active folder filter.");
    Bullet("Hover any thumbnail to see the full file path in a tooltip.");

    ImGui::Spacing();
    ImGui::SeparatorText("Assigning Materials by Drag-and-Drop");
    Bullet("Drag a thumbnail from the Asset Browser into the 3D viewport "
           "to assign that material to the surface under the cursor.");
    Tip("The drag payload carries a 128-bit UUID derived from the file path — "
        "the same UUID used in sector and wall material slots.");

    ImGui::Spacing();
    ImGui::SeparatorText("Picker Mode");
    Bullet("Click a material slot button in the Properties panel "
           "(Floor Material, Wall Front, Decal Normal, etc.) to activate "
           "Picker mode.  The Asset Browser tab comes to the front "
           "automatically.");
    Bullet("A yellow 'Picking: <slot name>' banner appears at the top of "
           "the panel.  Click any thumbnail to assign it and close the picker.");
    Bullet("Press Escape or click Cancel in the banner to dismiss without "
           "making a change.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Advanced Geometry
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawAdvancedGeometry()
{
    ImGui::SeparatorText("ADVANCED GEOMETRY");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Advanced geometry extends sectors beyond simple rectilinear rooms.  "
        "These features can be combined freely: a sector can have a sloped "
        "heightfield floor, curved walls, detail brush columns, and a vertical "
        "portal to a sector above it simultaneously.");

    // ─ Per-vertex heights / slopes ─────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Sloped Floors & Ceilings");
    ImGui::TextWrapped(
        "Any wall vertex can independently override the sector's flat floor or "
        "ceiling height.  Setting one corner higher than the rest produces a "
        "sloped surface across the whole polygon.");
    Step(1, "Select the sector with the Select tool (S), then press V to "
            "activate the Vertex tool.");
    Step(2, "Click a vertex in the 2D viewport to select it.");
    Step(3, "In the 3D viewport, hover one of the cyan (floor) or magenta "
            "(ceiling) height handles and scroll the mouse wheel to raise or "
            "lower it.  The floor/ceiling updates in real time.");
    Step(4, "Alternatively, enable the Floor Override or Ceiling Override "
            "checkbox in Properties > Height Overrides and drag the slider.");
    Tip("Adjacent portal strip heights update automatically when a vertex "
        "override changes, keeping steps and openings geometrically correct.");
    Tip("To reset a vertex to the sector scalar, uncheck the override "
        "checkbox.  The sector's Floor Height / Ceiling Height then applies.");

    // ─ Visual stairs ────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Visual Stairs");
    ImGui::TextWrapped(
        "Switching a sector's Floor Shape to Visual Stairs generates a "
        "stair-step mesh for visual fidelity while keeping physics as a "
        "smooth linear ramp so the player does not jitter.");
    Bullet("Step Count — number of steps (1–64).");
    Bullet("Riser Height — vertical height of each step riser (metres).");
    Bullet("Tread Depth — horizontal depth of each step tread (metres).");
    Bullet("Direction Angle — which way the stair run goes in the XZ plane "
           "(degrees, 0 = along +X axis).");
    Tip("Best for monumental staircases, bleachers, and tiered seating.  "
        "For physically navigable stairs the player walks sector-to-sector through, "
        "use the Staircase Generator in Sector Chain mode instead.");

    // ─ Staircase Generator ──────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Staircase Generator  (Window > Staircase Generator)");
    ImGui::TextWrapped(
        "The Staircase Generator panel automates the creation of both visual "
        "and physical staircases from a set of parameters.  Select a sector "
        "first, adjust the parameters, then click Generate.");
    Bullet("Visual Stair mode — applies a Stair Profile to the selected sector's "
           "floor (sets Floor Shape to Visual Stairs with the specified parameters).");
    Bullet("Sector Chain mode — generates N new rectangular sectors linked by "
           "wall portals, each stepped up by Riser Height.  The corridor runs in "
           "the specified direction.  The full chain is a single undoable step.");

    // ─ Curved walls ───────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Curved Walls  (Bezier)");
    ImGui::TextWrapped(
        "Any wall can be bent into a smooth Bezier arc.  "
        "The tessellator subdivides it into straight-segment quads so the "
        "3D mesh is always smooth.");
    Bullet("Drag the cyan midpoint handle circle of a wall in the 2D viewport "
           "to create a quadratic Bezier curve (one control point).");
    Bullet("Enable \"Cubic (add B)\" in Properties > Bezier Curve for an "
           "S-curve with two independent control points.");
    Bullet("Subdivisions (4–64) controls arc resolution.  Default 12 is smooth "
           "enough for most curved walls.");
    Bullet("Uncheck \"Enable Curve\" to restore the wall to a straight segment.");
    Tip("Curved portal walls are supported.  The portal visibility system clips "
        "to the chord (straight line) of the curve, which is slightly conservative "
        "but never incorrectly reveals occluded content.");

    // ─ Sector-Over-Sector ──────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Sector-Over-Sector (SoS)");
    ImGui::TextWrapped(
        "Two sectors can share the same XZ footprint at different heights by "
        "opening a floor or ceiling portal between them.  The portal traversal "
        "system resolves vertical visibility the same way as horizontal portals.");
    Step(1, "Draw both sectors at their respective heights.");
    Step(2, "Select the upper sector.  In Properties > Floor / Ceiling Portals, "
            "set Floor portal ID to the index of the lower sector.");
    Step(3, "Optionally assign a portal material (glass, grate) or leave empty "
            "for an open gap.");
    Tip("Use the Floor Layers panel (Window > Floor Layers) to filter the 2D "
        "view while working on individual floors of a stacked map.");

    // ─ Heightfield terrain ─────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Terrain Heightfield Floor");
    ImGui::TextWrapped(
        "Setting a sector's Floor Shape to Heightfield replaces the flat floor "
        "with a regular grid of height samples.  The tessellator generates a "
        "smooth mesh with per-vertex normals; the physics engine uses a native "
        "heightfield collision shape.");
    Step(1, "Select a sector.  In Properties > Floor Shape, choose Heightfield.");
    Step(2, "Click \"Create 8\xc3\x97" "8 Heightfield\" to initialise a flat grid over the "
            "sector's bounding box at the current floor height.");
    Step(3, "In the Properties panel, adjust Grid Width and Grid Depth to set "
            "the terrain resolution (2\xc3\x97 2 to 256\xc3\x97" "256 samples).");
    Step(4, "Enter fly mode in the 3D viewport (Tab) while the sector is selected.  "
            "Left-mouse to raise, right-mouse to lower, Shift+LMB to smooth, "
            "Alt+LMB to flatten.  Scroll adjusts brush radius.");
    Tip("The entire paint stroke from first click to release is stored as a "
        "single undoable command.  Cmd+Z reverts the whole stroke at once.");
    Tip("Increasing resolution bilinearly interpolates existing samples.  "
        "Decreasing resolution averages them.  Use higher resolution only "
        "where terrain detail is needed.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Detail Geometry
// ─────────────────────────────────────────────────────────────────────────────

void HelpPanel::drawDetailGeometry()
{
    ImGui::SeparatorText("DETAIL GEOMETRY");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Detail brushes are Layer 2 static geometry shapes placed within sectors "
        "for architectural decoration and infill.  They are compiled into the "
        "sector's GPU mesh batch at level-compile time and have zero CPU runtime "
        "overhead.  They do not create portals or affect portal visibility.");

    ImGui::Spacing();
    ImGui::SeparatorText("Placing a Detail Brush");
    Step(1, "Select a sector in the 2D viewport.");
    Step(2, "Open the Object Browser panel and expand the Detail Geometry section.");
    Step(3, "Click Box, Wedge, Cylinder, or Arch Span.  The brush is placed at "
            "the 2D cursor position with default parameters.");
    Step(4, "The brush is immediately visible in the 3D viewport.  "
            "Its dashed XZ footprint is shown in the 2D viewport.");
    Step(5, "Select the sector and open Properties.  Scroll to find the detail "
            "brush list.  Adjust the transform (position, rotation, scale) and "
            "type-specific parameters.");
    Tip("Detail brushes are per-sector.  A brush placed in sector A is compiled "
        "into sector A's mesh and is culled with it by portal visibility.");

    ImGui::Spacing();
    ImGui::SeparatorText("Brush Types");
    Bullet("Box — an axis-aligned or rotated box.  "
           "Configure half-extents on each axis.  "
           "Good for ledges, plinths, raised floor patches, parapet walls.");
    Bullet("Wedge — a box with one face sloped (triangular prism).  "
           "Good for chamfered edges, angled sills, and ramp infill.");
    Bullet("Cylinder — faceted cylinder (4–64 faces) with radius and height.  "
           "Good for columns, pillars, bollards, and round pipes.");
    Bullet("Arch Span — a curved arch band placed across an opening.  "
           "Configure span width, arch height, band thickness, profile shape "
           "(Semicircular / Gothic / Segmental), and segment count.  "
           "Good for decorative doorway arches, alcove crowns, and window sills.");

    ImGui::Spacing();
    ImGui::SeparatorText("Brush Properties  (Properties panel, sector selected)");
    Bullet("Transform — world-space position, rotation (via the transform matrix), "
           "and scale are stored as a 4\xc3\x97" "4 matrix.  Edit position in the Properties "
           "panel; rotation and scale support is through the matrix directly.");
    Bullet("Material — a single PBR material UUID applied to all faces of the brush.");
    Bullet("Collidable — if enabled, a physics collision shape is registered for "
           "the brush as part of the sector's compound physics body.  "
           "Disable for purely decorative geometry.");
    Bullet("Cast Shadow — whether this brush contributes shadow geometry "
           "to shadow map passes.");

    ImGui::Spacing();
    ImGui::SeparatorText("Removing a Brush");
    Bullet("In Properties, find the brush in the sector's detail list and click "
           "Remove.  This is undoable.");

    ImGui::Spacing();
    ImGui::SeparatorText("Compile Behaviour");
    ImGui::TextWrapped(
        "At level-compile time (\\) all detail brushes in a sector are merged "
        "into the sector's material-batched GPU mesh.  They are not runtime ECS "
        "objects — no components, no scripts, no physics bodies at the entity level.  "
        "If Collidable is set, a static shape is added to the sector's compound "
        "Jolt physics body.");
}

} // namespace daedalus::editor
