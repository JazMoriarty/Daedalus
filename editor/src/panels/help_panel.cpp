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
    "Entities",
    "Lights",
    "Player Start",
    "Portals & Layers",
    "Prefabs",
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
    case  0: drawQuickStart();      break;
    case  1: drawKeyboard();        break;
    case  2: draw2DViewport();      break;
    case  3: draw3DViewport();      break;
    case  4: drawTools();           break;
    case  5: drawSectorsWalls();    break;
    case  6: drawEntities();        break;
    case  7: drawLights();          break;
    case  8: drawPlayerStart();     break;
    case  9: drawPortalsLayers();   break;
    case 10: drawPrefabs();         break;
    case 11: drawRenderSettings();  break;
    case 12: drawMapDoctor();       break;
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
    Step(2, "In the dialog that appears, enter a Map Name and Author.");
    Step(3, "Set the Default Floor Height (e.g. 0.0) and Default Ceiling "
            "Height (e.g. 4.0).  These become the defaults for every new "
            "sector you draw.");
    Step(4, "Click Create.");

    // ─ Step 2: Draw your first room ──────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Step 2  —  Draw Your First Room");
    Step(1, "Press");
    Key("D");
    ImGui::SameLine(0, 2);
    ImGui::Text("to activate the Draw Sector tool.  The cursor changes to a crosshair.");
    Step(2, "Click four points in the 2D viewport to trace the corners of a "
            "room — click clockwise when viewed from above, starting at e.g. "
            "(-5, -5), (5, -5), (5, 5), (-5, 5).");
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
    Key("F5");
    ImGui::SameLine(0, 2);
    ImGui::Text("to save and launch DaedalusApp with your map loaded automatically.");
    Tip("F5 saves to a temporary file if the map has not been saved yet.");

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
    KeyRow("P",            "Place point light at 2D cursor   /   snap 3D camera to player start (when 3D viewport hovered)");

    ImGui::Spacing();
    ImGui::SeparatorText("2D Viewport");
    KeyRow("G",            "Toggle grid visibility");
    KeyRow("Home",         "Fit entire map to 2D view");
    KeyRow("1 – 8",        "Set grid snap step (0.25, 0.5, 1, 2, 4, 8, 16, 32 units)");

    ImGui::Spacing();
    ImGui::SeparatorText("3D Viewport");
    KeyRow("Tab",          "Toggle fly mode (captures/releases mouse)");
    KeyRow("Escape",       "Release fly mode");
    KeyRow("W A S D",      "Move forward / left / backward / right  (fly mode only)");
    KeyRow("Q / E",        "Move down / up  (fly mode only)");
    KeyRow("Shift",        "Hold for 5× faster movement  (fly mode only)");
    KeyRow("F",            "Frame selected content (or whole map if nothing selected)");
    KeyRow("P",            "Snap camera to player start position");
    KeyRow("Alt+LMB drag", "Orbit around pivot");
    KeyRow("Scroll",       "Dolly forward / backward");

    ImGui::Spacing();
    ImGui::SeparatorText("3D Gizmos (entity must be selected)");
    KeyRow("T",            "Translate gizmo — drag coloured arrows to move");
    KeyRow("Y",            "Scale gizmo — drag coloured squares to scale");
    KeyRow("R",            "Rotate gizmo — drag yaw ring to rotate");

    ImGui::Spacing();
    ImGui::SeparatorText("Map Tools");
    KeyRow("Cmd+Shift+M",  "Run Map Doctor");
    KeyRow("F5",           "Save + launch map in DaedalusApp (play test)");
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
    Bullet("Right-click drag — pan the view.");
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
    ImGui::SeparatorText("Toolbar Buttons");
    Bullet("The toolbar at the top of the panel shows the active tool and provides "
           "quick access to Select, Draw Sector, and Vertex modes.");
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
    // ─ Wall operations ───────────────────────────────────────────────
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
           "The 3D viewport renders a reflected view into a 512×512 RT each frame.");

    ImGui::Spacing();
    ImGui::SeparatorText("Wall UV Mapping  (Properties panel, wall selected)");
    Bullet("UV Offset — shifts the texture in U (horizontal) and V (vertical).");
    Bullet("UV Scale — tiles the texture.  Values > 1 tile more, < 1 stretch.");
    Bullet("UV Rotation — rotates the texture on the wall face (in radians).");

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
    ImGui::SeparatorText("Script & Audio  (Properties panel, collapsible headers)");
    Bullet("Script Path — path to the Lua/script file that drives this entity.");
    Bullet("Sound Path — ambient / one-shot audio file path.");
    Bullet("Falloff Radius — distance at which the sound reaches silence.");
    Bullet("Loop — if checked, the sound plays on repeat.");

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
        "Prefabs are named snapshots of one or more selected sectors.  "
        "Save a prefab once, then stamp it anywhere in the map with a single click.  "
        "All wall flags, materials, heights, and portal links within the saved "
        "selection are preserved; links to sectors outside the selection are cleared.");

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
        "Prefabs are stored in the map's sidecar file (*.dmap.json) alongside "
        "the main binary map data.  They persist between sessions and are "
        "included when the map is saved.");
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
    Bullet("A clean Map Doctor run is a good sign before pressing F5 to "
           "launch the engine; unchecked errors can cause the engine to "
           "hang or produce invisible geometry.");
}

} // namespace daedalus::editor
