#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/light_def.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#include "document/commands/cmd_place_light.h"
#include "document/commands/cmd_delete_light.h"
#include "document/commands/cmd_move_light.h"
#include "document/commands/cmd_set_light_props.h"
#include "document/commands/cmd_set_wall_uv.h"
#include "document/commands/cmd_set_sector_ambient.h"
#include "document/commands/cmd_move_sector.h"
#include "document/commands/cmd_split_wall.h"
#include "document/commands/cmd_duplicate_sector.h"
#include "document/emap_sidecar.h"
#include "document/commands/cmd_place_entity.h"
#include "document/commands/cmd_delete_entity.h"
#include "document/commands/cmd_move_entity.h"
#include "document/commands/cmd_rotate_entity.h"
#include "document/commands/cmd_scale_entity.h"
#include "document/commands/cmd_set_entity_props.h"
#include "document/commands/cmd_rotate_sector.h"
#include "document/commands/cmd_set_player_start.h"
#include "document/commands/cmd_set_wall_material.h"
#include "document/commands/cmd_set_sector_material.h"
#include "document/commands/cmd_set_map_meta.h"
#include "document/commands/cmd_set_map_defaults.h"
#include "document/commands/cmd_set_global_ambient.h"
#include "daedalus/editor/editor_layer.h"  // PlayerStart
#include "document/commands/cmd_set_vertex_height.h"
#include "document/commands/cmd_set_sector_floor_shape.h"
#include "document/commands/cmd_set_floor_portal.h"
#include "document/commands/cmd_set_wall_curve.h"
#include "document/commands/cmd_add_detail_brush.h"
#include "document/commands/cmd_remove_detail_brush.h"
#include "document/commands/cmd_set_detail_brush.h"
#include "document/commands/cmd_set_heightfield.h"
#include "document/commands/cmd_move_vertex.h"
#include "daedalus/editor/compound_command.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <memory>

using namespace daedalus::editor;
using namespace daedalus::world;

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// Builds and adds an axis-aligned square sector to the document.
static SectorId addSquare(EditMapDocument& doc,
                           float size   = 4.0f,
                           glm::vec2 origin = {0.0f, 0.0f})
{
    Sector s;
    s.walls.push_back(Wall{.p0 = origin + glm::vec2{0,    0}});
    s.walls.push_back(Wall{.p0 = origin + glm::vec2{size, 0}});
    s.walls.push_back(Wall{.p0 = origin + glm::vec2{size, size}});
    s.walls.push_back(Wall{.p0 = origin + glm::vec2{0,    size}});
    const SectorId id = static_cast<SectorId>(doc.mapData().sectors.size());
    doc.mapData().sectors.push_back(std::move(s));
    return id;
}

// ─── CmdPlaceLight ─────────────────────────────────────────────────────────

TEST(CmdPlaceLight, PlaceAddsLight)
{
    EditMapDocument doc;
    ASSERT_EQ(doc.lights().size(), 0u);

    LightDef ld; ld.position = {1.0f, 2.0f, 3.0f};
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));

    ASSERT_EQ(doc.lights().size(), 1u);
    EXPECT_FLOAT_EQ(doc.lights()[0].position.x, 1.0f);
    EXPECT_FLOAT_EQ(doc.lights()[0].position.y, 2.0f);
    EXPECT_FLOAT_EQ(doc.lights()[0].position.z, 3.0f);
}

TEST(CmdPlaceLight, PlaceSelectsLight)
{
    EditMapDocument doc;
    LightDef ld;
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));

    EXPECT_TRUE(doc.selection().hasSingleOf(SelectionType::Light));
    EXPECT_EQ(doc.selection().items[0].index, 0u);
}

TEST(CmdPlaceLight, UndoRemovesLight)
{
    EditMapDocument doc;
    LightDef ld;
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
    ASSERT_EQ(doc.lights().size(), 1u);

    doc.undo();

    EXPECT_EQ(doc.lights().size(), 0u);
}

TEST(CmdPlaceLight, RedoRestoresLight)
{
    EditMapDocument doc;
    LightDef ld; ld.position = {5.0f, 0.0f, 5.0f};
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
    doc.undo();
    ASSERT_EQ(doc.lights().size(), 0u);

    doc.redo();

    ASSERT_EQ(doc.lights().size(), 1u);
    EXPECT_FLOAT_EQ(doc.lights()[0].position.x, 5.0f);
}

TEST(CmdPlaceLight, PlaceSpotLightPreservesType)
{
    EditMapDocument doc;
    LightDef ld;
    ld.position = {0.0f, 4.0f, 0.0f};
    ld.type     = LightType::Spot;
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));

    ASSERT_EQ(doc.lights().size(), 1u);
    EXPECT_EQ(doc.lights()[0].type, LightType::Spot);
    EXPECT_FLOAT_EQ(doc.lights()[0].position.y, 4.0f);
}

// ─── CmdDeleteLight ──────────────────────────────────────────────────────────

TEST(CmdDeleteLight, DeleteRemovesLight)
{
    EditMapDocument doc;
    LightDef ld; ld.position = {1.0f, 2.0f, 3.0f};
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
    ASSERT_EQ(doc.lights().size(), 1u);

    doc.pushCommand(std::make_unique<CmdDeleteLight>(doc, 0u));

    EXPECT_EQ(doc.lights().size(), 0u);
}

TEST(CmdDeleteLight, UndoRestoresLight)
{
    EditMapDocument doc;
    LightDef ld; ld.position = {7.0f, 0.0f, 7.0f};
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
    doc.pushCommand(std::make_unique<CmdDeleteLight>(doc, 0u));
    ASSERT_EQ(doc.lights().size(), 0u);

    doc.undo();

    ASSERT_EQ(doc.lights().size(), 1u);
    EXPECT_FLOAT_EQ(doc.lights()[0].position.x, 7.0f);
}

TEST(CmdDeleteLight, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    EXPECT_NO_THROW(doc.pushCommand(
        std::make_unique<CmdDeleteLight>(doc, 99u)));
    EXPECT_EQ(doc.lights().size(), 0u);
}

// ─── CmdMoveLight ────────────────────────────────────────────────────────────

TEST(CmdMoveLight, MoveChangesPosition)
{
    EditMapDocument doc;
    LightDef ld;
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));

    const glm::vec3 newPos{10, 5, 10};
    doc.pushCommand(std::make_unique<CmdMoveLight>(doc, 0u,
                                                   glm::vec3{0, 0, 0}, newPos));

    EXPECT_FLOAT_EQ(doc.lights()[0].position.x, 10.0f);
    EXPECT_FLOAT_EQ(doc.lights()[0].position.y, 5.0f);
    EXPECT_FLOAT_EQ(doc.lights()[0].position.z, 10.0f);
}

TEST(CmdMoveLight, UndoRestoresPosition)
{
    EditMapDocument doc;
    const glm::vec3 orig{1, 2, 3};
    LightDef ld; ld.position = orig;
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
    doc.pushCommand(std::make_unique<CmdMoveLight>(doc, 0u, orig,
                                                   glm::vec3{9, 9, 9}));

    doc.undo();

    EXPECT_FLOAT_EQ(doc.lights()[0].position.x, 1.0f);
    EXPECT_FLOAT_EQ(doc.lights()[0].position.y, 2.0f);
    EXPECT_FLOAT_EQ(doc.lights()[0].position.z, 3.0f);
}

// ─── CmdSetLightProps ───────────────────────────────────────────────────────────────

TEST(CmdSetLightProps, SetsColorRadiusIntensity)
{
    EditMapDocument doc;
    LightDef ld;
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));

    const LightDef  oldDef   = doc.lights()[0];
    const glm::vec3 newColor{0.5f, 0.0f, 1.0f};
    LightDef newDef    = oldDef;
    newDef.color     = newColor;
    newDef.radius    = 25.0f;
    newDef.intensity = 4.0f;
    doc.pushCommand(std::make_unique<CmdSetLightProps>(doc, 0u, oldDef, newDef));

    EXPECT_FLOAT_EQ(doc.lights()[0].color.r,   0.5f);
    EXPECT_FLOAT_EQ(doc.lights()[0].radius,    25.0f);
    EXPECT_FLOAT_EQ(doc.lights()[0].intensity, 4.0f);
}

TEST(CmdSetLightProps, UndoRestoresProps)
{
    EditMapDocument doc;
    LightDef ld;
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));

    const LightDef origDef = doc.lights()[0];
    LightDef newDef    = origDef;
    newDef.color     = glm::vec3{0.0f, 0.0f, 0.0f};
    newDef.radius    = 1.0f;
    newDef.intensity = 1.0f;
    doc.pushCommand(std::make_unique<CmdSetLightProps>(doc, 0u, origDef, newDef));
    doc.undo();

    EXPECT_FLOAT_EQ(doc.lights()[0].color.r,   origDef.color.r);
    EXPECT_FLOAT_EQ(doc.lights()[0].radius,    origDef.radius);
    EXPECT_FLOAT_EQ(doc.lights()[0].intensity, origDef.intensity);
}

TEST(CmdSetLightProps, SetsSpotType)
{
    EditMapDocument doc;
    LightDef ld;  // default: Point
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));
    ASSERT_EQ(doc.lights()[0].type, LightType::Point);

    const LightDef oldDef = doc.lights()[0];
    LightDef newDef = oldDef;
    newDef.type = LightType::Spot;
    doc.pushCommand(std::make_unique<CmdSetLightProps>(doc, 0u, oldDef, newDef));

    EXPECT_EQ(doc.lights()[0].type, LightType::Spot);
}

TEST(CmdSetLightProps, SetsSpotConeFields)
{
    EditMapDocument doc;
    LightDef ld; ld.type = LightType::Spot;
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));

    const LightDef oldDef = doc.lights()[0];
    LightDef newDef            = oldDef;
    newDef.direction           = glm::vec3(0.0f, -1.0f, 0.0f);
    newDef.innerConeAngle      = 0.2f;
    newDef.outerConeAngle      = 0.4f;
    newDef.range               = 15.0f;
    doc.pushCommand(std::make_unique<CmdSetLightProps>(doc, 0u, oldDef, newDef));

    EXPECT_FLOAT_EQ(doc.lights()[0].innerConeAngle, 0.2f);
    EXPECT_FLOAT_EQ(doc.lights()[0].outerConeAngle, 0.4f);
    EXPECT_FLOAT_EQ(doc.lights()[0].range,          15.0f);
    EXPECT_FLOAT_EQ(doc.lights()[0].direction.y,    -1.0f);
}

TEST(CmdSetLightProps, UndoRestoresSpotConeFields)
{
    EditMapDocument doc;
    LightDef ld;
    ld.type           = LightType::Spot;
    ld.innerConeAngle = 0.3f;
    ld.outerConeAngle = 0.6f;
    ld.range          = 12.0f;
    doc.pushCommand(std::make_unique<CmdPlaceLight>(doc, ld));

    const LightDef origDef = doc.lights()[0];
    LightDef newDef            = origDef;
    newDef.innerConeAngle      = 0.1f;
    newDef.outerConeAngle      = 0.2f;
    newDef.range               = 5.0f;
    doc.pushCommand(std::make_unique<CmdSetLightProps>(doc, 0u, origDef, newDef));
    doc.undo();

    EXPECT_FLOAT_EQ(doc.lights()[0].innerConeAngle, 0.3f);
    EXPECT_FLOAT_EQ(doc.lights()[0].outerConeAngle, 0.6f);
    EXPECT_FLOAT_EQ(doc.lights()[0].range,          12.0f);
}

// ─── Emap sidecar — spot light roundtrip ─────────────────────────────────────

TEST(EmapSidecar, SpotLightRoundTrip)
{
    // Build a document with one spot light.
    EditMapDocument doc;
    LightDef sl;
    sl.type           = LightType::Spot;
    sl.position       = {1.0f, 4.0f, 2.0f};
    sl.color          = {1.0f, 0.5f, 0.0f};
    sl.intensity      = 3.5f;
    sl.direction      = {0.0f, -1.0f, 0.0f};
    sl.innerConeAngle = 0.25f;
    sl.outerConeAngle = 0.50f;
    sl.range          = 18.0f;
    doc.lights().push_back(sl);

    // Write to a temp file.
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "test_spot_roundtrip.emap";
    const auto saveResult = saveEmap(doc, tmp);
    ASSERT_TRUE(saveResult.has_value()) << "saveEmap failed";

    // Load into a fresh document.
    EditMapDocument loaded;
    const auto loadResult = loadEmap(loaded, tmp);
    ASSERT_TRUE(loadResult.has_value()) << "loadEmap failed";

    std::filesystem::remove(tmp);

    ASSERT_EQ(loaded.lights().size(), 1u);
    const LightDef& got = loaded.lights()[0];
    EXPECT_EQ  (got.type,           LightType::Spot);
    EXPECT_FLOAT_EQ(got.position.x,       1.0f);
    EXPECT_FLOAT_EQ(got.position.y,       4.0f);
    EXPECT_FLOAT_EQ(got.innerConeAngle,   0.25f);
    EXPECT_FLOAT_EQ(got.outerConeAngle,   0.50f);
    EXPECT_FLOAT_EQ(got.range,            18.0f);
    EXPECT_FLOAT_EQ(got.direction.y,      -1.0f);
}

// ─── CmdSetWallUV ────────────────────────────────────────────────────────────

TEST(CmdSetWallUV, SetsUVFields)
{
    EditMapDocument doc;
    addSquare(doc);

    doc.pushCommand(std::make_unique<CmdSetWallUV>(
        doc, 0u, 0u,
        glm::vec2{0.5f, 0.25f},
        glm::vec2{2.0f, 3.0f},
        1.57f));

    const Wall& w = doc.mapData().sectors[0].walls[0];
    EXPECT_FLOAT_EQ(w.uvOffset.x,  0.5f);
    EXPECT_FLOAT_EQ(w.uvOffset.y,  0.25f);
    EXPECT_FLOAT_EQ(w.uvScale.x,   2.0f);
    EXPECT_FLOAT_EQ(w.uvScale.y,   3.0f);
    EXPECT_FLOAT_EQ(w.uvRotation,  1.57f);
}

TEST(CmdSetWallUV, UndoRestoresUV)
{
    EditMapDocument doc;
    addSquare(doc);

    // Default values.
    const Wall& w = doc.mapData().sectors[0].walls[0];
    const glm::vec2 origOffset   = w.uvOffset;
    const glm::vec2 origScale    = w.uvScale;
    const float     origRotation = w.uvRotation;

    doc.pushCommand(std::make_unique<CmdSetWallUV>(
        doc, 0u, 0u,
        glm::vec2{9.0f, 9.0f}, glm::vec2{9.0f, 9.0f}, 9.0f));
    doc.undo();

    EXPECT_FLOAT_EQ(w.uvOffset.x,  origOffset.x);
    EXPECT_FLOAT_EQ(w.uvOffset.y,  origOffset.y);
    EXPECT_FLOAT_EQ(w.uvScale.x,   origScale.x);
    EXPECT_FLOAT_EQ(w.uvRotation,  origRotation);
}

// ─── CmdSetSectorAmbient ─────────────────────────────────────────────────────

TEST(CmdSetSectorAmbient, SetsColorAndIntensity)
{
    EditMapDocument doc;
    addSquare(doc);

    const glm::vec3 newColor{0.2f, 0.4f, 0.8f};
    doc.pushCommand(std::make_unique<CmdSetSectorAmbient>(
        doc, 0u, newColor, 3.5f));

    const Sector& s = doc.mapData().sectors[0];
    EXPECT_FLOAT_EQ(s.ambientColor.r,   0.2f);
    EXPECT_FLOAT_EQ(s.ambientColor.g,   0.4f);
    EXPECT_FLOAT_EQ(s.ambientColor.b,   0.8f);
    EXPECT_FLOAT_EQ(s.ambientIntensity, 3.5f);
}

TEST(CmdSetSectorAmbient, UndoRestoresAmbient)
{
    EditMapDocument doc;
    addSquare(doc);

    const Sector& s = doc.mapData().sectors[0];
    const glm::vec3 origColor = s.ambientColor;
    const float     origI     = s.ambientIntensity;

    doc.pushCommand(std::make_unique<CmdSetSectorAmbient>(
        doc, 0u, glm::vec3{0, 0, 0}, 0.0f));
    doc.undo();

    EXPECT_FLOAT_EQ(s.ambientColor.r,   origColor.r);
    EXPECT_FLOAT_EQ(s.ambientIntensity, origI);
}

// ─── CmdMoveSector ───────────────────────────────────────────────────────────

TEST(CmdMoveSector, UndoReversesDelta)
{
    EditMapDocument doc;
    addSquare(doc);

    // Simulate: live drag already applied delta, then command is pushed.
    const glm::vec2 delta{3.0f, 2.0f};
    for (auto& wall : doc.mapData().sectors[0].walls)
        wall.p0 += delta;

    doc.pushCommand(std::make_unique<CmdMoveSector>(doc, 0u, delta));

    // Undo must reverse the delta.
    const glm::vec2 p0Before = doc.mapData().sectors[0].walls[0].p0;
    doc.undo();
    const glm::vec2 p0After = doc.mapData().sectors[0].walls[0].p0;

    EXPECT_FLOAT_EQ(p0After.x, p0Before.x - delta.x);
    EXPECT_FLOAT_EQ(p0After.y, p0Before.y - delta.y);
}

TEST(CmdMoveSector, RedoReappliesDelta)
{
    EditMapDocument doc;
    addSquare(doc);

    const glm::vec2 delta{1.0f, 1.0f};
    for (auto& wall : doc.mapData().sectors[0].walls)
        wall.p0 += delta;

    doc.pushCommand(std::make_unique<CmdMoveSector>(doc, 0u, delta));
    doc.undo();
    const glm::vec2 undonePos = doc.mapData().sectors[0].walls[0].p0;

    doc.redo();
    const glm::vec2 redonePos = doc.mapData().sectors[0].walls[0].p0;

    EXPECT_FLOAT_EQ(redonePos.x, undonePos.x + delta.x);
    EXPECT_FLOAT_EQ(redonePos.y, undonePos.y + delta.y);
}

// ─── CmdSplitWall ────────────────────────────────────────────────────────────

TEST(CmdSplitWall, SplitIncreasesWallCount)
{
    EditMapDocument doc;
    addSquare(doc);  // 4 walls initially.
    ASSERT_EQ(doc.mapData().sectors[0].walls.size(), 4u);

    doc.pushCommand(std::make_unique<CmdSplitWall>(doc, 0u, 0u));

    EXPECT_EQ(doc.mapData().sectors[0].walls.size(), 5u);
}

TEST(CmdSplitWall, MidpointIsCorrect)
{
    EditMapDocument doc;
    addSquare(doc);  // wall 0: (0,0)→(4,0); midpoint should be (2,0).

    doc.pushCommand(std::make_unique<CmdSplitWall>(doc, 0u, 0u));

    // The new wall is inserted at index 1 with p0 = midpoint.
    const glm::vec2 mid = doc.mapData().sectors[0].walls[1].p0;
    EXPECT_FLOAT_EQ(mid.x, 2.0f);
    EXPECT_FLOAT_EQ(mid.y, 0.0f);
}

TEST(CmdSplitWall, UndoRestoresWallCount)
{
    EditMapDocument doc;
    addSquare(doc);
    doc.pushCommand(std::make_unique<CmdSplitWall>(doc, 0u, 0u));
    ASSERT_EQ(doc.mapData().sectors[0].walls.size(), 5u);

    doc.undo();

    EXPECT_EQ(doc.mapData().sectors[0].walls.size(), 4u);
}

// ─── CmdDuplicateSector ──────────────────────────────────────────────────────

TEST(CmdDuplicateSector, DuplicateAddsSector)
{
    EditMapDocument doc;
    addSquare(doc);
    ASSERT_EQ(doc.mapData().sectors.size(), 1u);

    doc.pushCommand(std::make_unique<CmdDuplicateSector>(
        doc, 0u, glm::vec2{1.0f, 1.0f}));

    EXPECT_EQ(doc.mapData().sectors.size(), 2u);
}

TEST(CmdDuplicateSector, CopyHasOffset)
{
    EditMapDocument doc;
    addSquare(doc);  // wall 0 at (0,0).

    doc.pushCommand(std::make_unique<CmdDuplicateSector>(
        doc, 0u, glm::vec2{5.0f, 3.0f}));

    const glm::vec2 copyP0 = doc.mapData().sectors[1].walls[0].p0;
    EXPECT_FLOAT_EQ(copyP0.x, 5.0f);
    EXPECT_FLOAT_EQ(copyP0.y, 3.0f);
}

TEST(CmdDuplicateSector, CopyHasNoPortalLinks)
{
    EditMapDocument doc;
    Sector s;
    s.walls.push_back(Wall{.p0 = {0,0}, .portalSectorId = 0u});
    s.walls.push_back(Wall{.p0 = {4,0}});
    s.walls.push_back(Wall{.p0 = {4,4}});
    doc.mapData().sectors.push_back(s);

    doc.pushCommand(std::make_unique<CmdDuplicateSector>(
        doc, 0u, glm::vec2{1.0f, 0.0f}));

    for (const Wall& w : doc.mapData().sectors[1].walls)
        EXPECT_EQ(w.portalSectorId, INVALID_SECTOR_ID);
}

TEST(CmdDuplicateSector, UndoRemovesCopy)
{
    EditMapDocument doc;
    addSquare(doc);
    doc.pushCommand(std::make_unique<CmdDuplicateSector>(
        doc, 0u, glm::vec2{1.0f, 0.0f}));
    ASSERT_EQ(doc.mapData().sectors.size(), 2u);

    doc.undo();

    EXPECT_EQ(doc.mapData().sectors.size(), 1u);
}

TEST(CmdDuplicateSector, RedoRestoresCopy)
{
    EditMapDocument doc;
    addSquare(doc);
    doc.pushCommand(std::make_unique<CmdDuplicateSector>(
        doc, 0u, glm::vec2{1.0f, 0.0f}));
    doc.undo();
    ASSERT_EQ(doc.mapData().sectors.size(), 1u);

    doc.redo();

    EXPECT_EQ(doc.mapData().sectors.size(), 2u);
}

// ─── CmdPlaceEntity ───────────────────────────────────────────────────────────

TEST(CmdPlaceEntity, PlaceAddsEntity)
{
    EditMapDocument doc;
    ASSERT_EQ(doc.entities().size(), 0u);

    EntityDef e;
    e.position  = {3.0f, 0.0f, 5.0f};
    e.assetPath = "props/barrel.png";
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));

    ASSERT_EQ(doc.entities().size(), 1u);
    EXPECT_FLOAT_EQ(doc.entities()[0].position.x, 3.0f);
    EXPECT_EQ(doc.entities()[0].assetPath, "props/barrel.png");
}

TEST(CmdPlaceEntity, PlaceSelectsEntity)
{
    EditMapDocument doc;
    EntityDef e;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));

    EXPECT_TRUE(doc.selection().hasSingleOf(SelectionType::Entity));
    EXPECT_EQ(doc.selection().items[0].index, 0u);
}

TEST(CmdPlaceEntity, PlaceSetsEntityDirty)
{
    EditMapDocument doc;
    doc.clearEntityDirty();
    EntityDef e;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));

    EXPECT_TRUE(doc.isEntityDirty());
}

TEST(CmdPlaceEntity, UndoRemovesEntity)
{
    EditMapDocument doc;
    EntityDef e;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));
    ASSERT_EQ(doc.entities().size(), 1u);

    doc.undo();

    EXPECT_EQ(doc.entities().size(), 0u);
}

TEST(CmdPlaceEntity, RedoRestoresEntity)
{
    EditMapDocument doc;
    EntityDef e; e.position = {1.0f, 0.0f, 2.0f};
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));
    doc.undo();
    ASSERT_EQ(doc.entities().size(), 0u);

    doc.redo();

    ASSERT_EQ(doc.entities().size(), 1u);
    EXPECT_FLOAT_EQ(doc.entities()[0].position.x, 1.0f);
}

// ─── CmdDeleteEntity ──────────────────────────────────────────────────────────

TEST(CmdDeleteEntity, DeleteRemovesEntity)
{
    EditMapDocument doc;
    EntityDef e; e.assetPath = "props/crate.png";
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));
    ASSERT_EQ(doc.entities().size(), 1u);

    doc.pushCommand(std::make_unique<CmdDeleteEntity>(doc, 0u));

    EXPECT_EQ(doc.entities().size(), 0u);
}

TEST(CmdDeleteEntity, UndoRestoresEntity)
{
    EditMapDocument doc;
    EntityDef e; e.assetPath = "props/box.png";
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));
    doc.pushCommand(std::make_unique<CmdDeleteEntity>(doc, 0u));
    ASSERT_EQ(doc.entities().size(), 0u);

    doc.undo();

    ASSERT_EQ(doc.entities().size(), 1u);
    EXPECT_EQ(doc.entities()[0].assetPath, "props/box.png");
}

TEST(CmdDeleteEntity, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    EXPECT_NO_THROW(doc.pushCommand(
        std::make_unique<CmdDeleteEntity>(doc, 99u)));
    EXPECT_EQ(doc.entities().size(), 0u);
}

// ─── CmdMoveEntity ────────────────────────────────────────────────────────────

TEST(CmdMoveEntity, MoveChangesPosition)
{
    EditMapDocument doc;
    EntityDef e;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));

    const glm::vec3 newPos{7.0f, 0.0f, 3.0f};
    doc.pushCommand(std::make_unique<CmdMoveEntity>(
        doc, 0u, glm::vec3{0.0f}, newPos));

    EXPECT_FLOAT_EQ(doc.entities()[0].position.x, 7.0f);
    EXPECT_FLOAT_EQ(doc.entities()[0].position.z, 3.0f);
}

TEST(CmdMoveEntity, UndoRestoresPosition)
{
    EditMapDocument doc;
    const glm::vec3 orig{2.0f, 0.0f, 4.0f};
    EntityDef e; e.position = orig;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));
    doc.pushCommand(std::make_unique<CmdMoveEntity>(
        doc, 0u, orig, glm::vec3{99.0f}));

    doc.undo();

    EXPECT_FLOAT_EQ(doc.entities()[0].position.x, 2.0f);
    EXPECT_FLOAT_EQ(doc.entities()[0].position.z, 4.0f);
}

// ─── CmdSetEntityProps ────────────────────────────────────────────────────────

TEST(CmdSetEntityProps, SetsVisualType)
{
    EditMapDocument doc;
    EntityDef e;  // default: BillboardCutout
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));

    const EntityDef oldDef = doc.entities()[0];
    EntityDef newDef = oldDef;
    newDef.visualType = EntityVisualType::VoxelObject;
    doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, 0u, oldDef, newDef));

    EXPECT_EQ(doc.entities()[0].visualType, EntityVisualType::VoxelObject);
}

TEST(CmdSetEntityProps, SetsAssetPath)
{
    EditMapDocument doc;
    EntityDef e;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));

    const EntityDef oldDef = doc.entities()[0];
    EntityDef newDef    = oldDef;
    newDef.assetPath    = "entities/barrel.vox";
    doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, 0u, oldDef, newDef));

    EXPECT_EQ(doc.entities()[0].assetPath, "entities/barrel.vox");
}

TEST(CmdSetEntityProps, UndoRestoresProps)
{
    EditMapDocument doc;
    EntityDef e; e.assetPath = "original.png";
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));

    const EntityDef origDef = doc.entities()[0];
    EntityDef newDef    = origDef;
    newDef.assetPath    = "changed.png";
    newDef.visualType   = EntityVisualType::StaticMesh;
    doc.pushCommand(std::make_unique<CmdSetEntityProps>(doc, 0u, origDef, newDef));
    doc.undo();

    EXPECT_EQ(doc.entities()[0].assetPath,   "original.png");
    EXPECT_EQ(doc.entities()[0].visualType,  EntityVisualType::BillboardCutout);
}

// ─── CmdRotateSector ──────────────────────────────────────────────────────────────

TEST(CmdRotateSector, RotateChangesVertices)
{
    EditMapDocument doc;
    addSquare(doc);  // corners at (0,0), (4,0), (4,4), (0,4); centroid (2,2)

    doc.pushCommand(std::make_unique<CmdRotateSector>(doc, 0u, 90.0f));

    // After 90° CCW rotation around centroid (2,2),
    // (0,0) → d=(-2,-2), rotated d=(2,-2), new=(4,0).
    const glm::vec2 p = doc.mapData().sectors[0].walls[0].p0;
    EXPECT_NEAR(p.x, 4.0f, 1e-4f);
    EXPECT_NEAR(p.y, 0.0f, 1e-4f);
}

TEST(CmdRotateSector, UndoRestoresVertices)
{
    EditMapDocument doc;
    addSquare(doc);

    const glm::vec2 origP0 = doc.mapData().sectors[0].walls[0].p0;
    doc.pushCommand(std::make_unique<CmdRotateSector>(doc, 0u, 45.0f));
    doc.undo();

    const glm::vec2 restoredP0 = doc.mapData().sectors[0].walls[0].p0;
    EXPECT_FLOAT_EQ(restoredP0.x, origP0.x);
    EXPECT_FLOAT_EQ(restoredP0.y, origP0.y);
}

TEST(CmdRotateSector, FullRotationIsIdentity)
{
    EditMapDocument doc;
    addSquare(doc);

    const glm::vec2 before = doc.mapData().sectors[0].walls[2].p0;
    doc.pushCommand(std::make_unique<CmdRotateSector>(doc, 0u, 360.0f));
    const glm::vec2 after  = doc.mapData().sectors[0].walls[2].p0;

    EXPECT_NEAR(after.x, before.x, 1e-4f);
    EXPECT_NEAR(after.y, before.y, 1e-4f);
}

// ─── CmdSetPlayerStart ─────────────────────────────────────────────────────────

TEST(CmdSetPlayerStart, SetsPlayerStart)
{
    EditMapDocument doc;
    ASSERT_FALSE(doc.playerStart().has_value());

    PlayerStart ps;
    ps.position = {3.0f, 0.0f, 5.0f};
    ps.yaw      = 1.5f;
    doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
        doc, std::nullopt, ps));

    ASSERT_TRUE(doc.playerStart().has_value());
    EXPECT_FLOAT_EQ(doc.playerStart()->position.x, 3.0f);
    EXPECT_FLOAT_EQ(doc.playerStart()->yaw,         1.5f);
}

TEST(CmdSetPlayerStart, SetsSelectionType)
{
    EditMapDocument doc;
    PlayerStart ps;
    doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
        doc, std::nullopt, ps));

    EXPECT_TRUE(doc.selection().hasSingleOf(SelectionType::PlayerStart));
}

TEST(CmdSetPlayerStart, UndoClearsPlayerStart)
{
    EditMapDocument doc;
    PlayerStart ps; ps.position = {1.0f, 0.0f, 2.0f};
    doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
        doc, std::nullopt, ps));
    ASSERT_TRUE(doc.playerStart().has_value());

    doc.undo();

    EXPECT_FALSE(doc.playerStart().has_value());
}

TEST(CmdSetPlayerStart, RedoRestoresPlayerStart)
{
    EditMapDocument doc;
    PlayerStart ps; ps.position = {7.0f, 0.0f, 7.0f}; ps.yaw = 0.5f;
    doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
        doc, std::nullopt, ps));
    doc.undo();
    ASSERT_FALSE(doc.playerStart().has_value());

    doc.redo();

    ASSERT_TRUE(doc.playerStart().has_value());
    EXPECT_FLOAT_EQ(doc.playerStart()->position.x, 7.0f);
    EXPECT_FLOAT_EQ(doc.playerStart()->yaw,         0.5f);
}

TEST(CmdSetPlayerStart, ClearPlayerStart)
{
    EditMapDocument doc;
    PlayerStart ps; ps.position = {1.0f, 0.0f, 1.0f};
    doc.setPlayerStart(ps);  // set directly (no undo stack needed for setup)

    doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
        doc, ps, std::nullopt));

    EXPECT_FALSE(doc.playerStart().has_value());
}

TEST(CmdSetPlayerStart, UndoClearRestoresStart)
{
    EditMapDocument doc;
    PlayerStart ps; ps.position = {4.0f, 0.0f, 4.0f}; ps.yaw = 1.0f;
    doc.setPlayerStart(ps);

    doc.pushCommand(std::make_unique<CmdSetPlayerStart>(
        doc, ps, std::nullopt));
    ASSERT_FALSE(doc.playerStart().has_value());

    doc.undo();

    ASSERT_TRUE(doc.playerStart().has_value());
    EXPECT_FLOAT_EQ(doc.playerStart()->position.x, 4.0f);
    EXPECT_FLOAT_EQ(doc.playerStart()->yaw,         1.0f);
}

TEST(CmdSetPlayerStart, UpdatePositionWithUndo)
{
    EditMapDocument doc;
    PlayerStart ps; ps.position = {0.0f, 0.0f, 0.0f}; ps.yaw = 0.0f;
    doc.setPlayerStart(ps);

    PlayerStart newPs; newPs.position = {10.0f, 0.0f, 20.0f}; newPs.yaw = 1.0f;
    doc.pushCommand(std::make_unique<CmdSetPlayerStart>(doc, ps, newPs));

    ASSERT_TRUE(doc.playerStart().has_value());
    EXPECT_FLOAT_EQ(doc.playerStart()->position.x, 10.0f);
    EXPECT_FLOAT_EQ(doc.playerStart()->yaw,         1.0f);

    doc.undo();

    ASSERT_TRUE(doc.playerStart().has_value());
    EXPECT_FLOAT_EQ(doc.playerStart()->position.x, 0.0f);
    EXPECT_FLOAT_EQ(doc.playerStart()->yaw,         0.0f);
}

// ─── CmdRotateEntity ──────────────────────────────────────────────────────────

TEST(CmdRotateEntity, RotateChangesYaw)
{
    EditMapDocument doc;
    EntityDef e; e.yaw = 0.0f;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));

    doc.pushCommand(std::make_unique<CmdRotateEntity>(
        doc, 0u, 0.0f, 1.5708f));

    EXPECT_FLOAT_EQ(doc.entities()[0].yaw, 1.5708f);
}

TEST(CmdRotateEntity, UndoRestoresYaw)
{
    EditMapDocument doc;
    EntityDef e; e.yaw = 0.5f;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));
    doc.pushCommand(std::make_unique<CmdRotateEntity>(
        doc, 0u, 0.5f, 3.0f));

    doc.undo();

    EXPECT_FLOAT_EQ(doc.entities()[0].yaw, 0.5f);
}

TEST(CmdRotateEntity, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    EXPECT_NO_THROW(doc.pushCommand(
        std::make_unique<CmdRotateEntity>(doc, 99u, 0.0f, 1.0f)));
}

TEST(CmdRotateEntity, MarksDirty)
{
    EditMapDocument doc;
    EntityDef e;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));
    doc.clearEntityDirty();

    doc.pushCommand(std::make_unique<CmdRotateEntity>(
        doc, 0u, 0.0f, 0.5f));

    EXPECT_TRUE(doc.isEntityDirty());
}

// ─── CmdScaleEntity ───────────────────────────────────────────────────────────

TEST(CmdScaleEntity, ScaleChangesScale)
{
    EditMapDocument doc;
    EntityDef e; e.scale = glm::vec3(1.0f);
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));

    const glm::vec3 newScale{2.0f, 3.0f, 4.0f};
    doc.pushCommand(std::make_unique<CmdScaleEntity>(
        doc, 0u, glm::vec3(1.0f), newScale));

    EXPECT_FLOAT_EQ(doc.entities()[0].scale.x, 2.0f);
    EXPECT_FLOAT_EQ(doc.entities()[0].scale.y, 3.0f);
    EXPECT_FLOAT_EQ(doc.entities()[0].scale.z, 4.0f);
}

TEST(CmdScaleEntity, UndoRestoresScale)
{
    EditMapDocument doc;
    const glm::vec3 orig{0.5f, 0.5f, 0.5f};
    EntityDef e; e.scale = orig;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));
    doc.pushCommand(std::make_unique<CmdScaleEntity>(
        doc, 0u, orig, glm::vec3(10.0f)));

    doc.undo();

    EXPECT_FLOAT_EQ(doc.entities()[0].scale.x, 0.5f);
    EXPECT_FLOAT_EQ(doc.entities()[0].scale.y, 0.5f);
    EXPECT_FLOAT_EQ(doc.entities()[0].scale.z, 0.5f);
}

TEST(CmdScaleEntity, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    EXPECT_NO_THROW(doc.pushCommand(
        std::make_unique<CmdScaleEntity>(
            doc, 99u, glm::vec3(1.0f), glm::vec3(2.0f))));
}

TEST(CmdScaleEntity, MarksDirty)
{
    EditMapDocument doc;
    EntityDef e;
    doc.pushCommand(std::make_unique<CmdPlaceEntity>(doc, e));
    doc.clearEntityDirty();

    doc.pushCommand(std::make_unique<CmdScaleEntity>(
        doc, 0u, glm::vec3(1.0f), glm::vec3(2.0f)));

    EXPECT_TRUE(doc.isEntityDirty());
}

// ─── CmdSetWallMaterial ──────────────────────────────────────────────────────────────

TEST(CmdSetWallMaterial, SetsFrontMaterial)
{
    EditMapDocument doc;
    addSquare(doc);

    daedalus::UUID id; id.lo = 0xABCD1234u;
    doc.pushCommand(std::make_unique<CmdSetWallMaterial>(
        doc, 0u, 0u, WallSurface::Front, id));

    EXPECT_EQ(doc.mapData().sectors[0].walls[0].frontMaterialId.lo, 0xABCD1234u);
}

TEST(CmdSetWallMaterial, SetsBackMaterial)
{
    EditMapDocument doc;
    addSquare(doc);

    daedalus::UUID id; id.lo = 0xDEADBEEFu;
    doc.pushCommand(std::make_unique<CmdSetWallMaterial>(
        doc, 0u, 1u, WallSurface::Back, id));

    EXPECT_EQ(doc.mapData().sectors[0].walls[1].backMaterialId.lo, 0xDEADBEEFu);
}

TEST(CmdSetWallMaterial, UndoRestoresMaterial)
{
    EditMapDocument doc;
    addSquare(doc);

    const daedalus::UUID original = doc.mapData().sectors[0].walls[0].frontMaterialId;
    daedalus::UUID newId; newId.lo = 0x999u;
    doc.pushCommand(std::make_unique<CmdSetWallMaterial>(
        doc, 0u, 0u, WallSurface::Front, newId));
    doc.undo();

    EXPECT_EQ(doc.mapData().sectors[0].walls[0].frontMaterialId.lo, original.lo);
}

TEST(CmdSetWallMaterial, RedoReappliesMaterial)
{
    EditMapDocument doc;
    addSquare(doc);

    daedalus::UUID id; id.lo = 0xCAFEu;
    doc.pushCommand(std::make_unique<CmdSetWallMaterial>(
        doc, 0u, 0u, WallSurface::Front, id));
    doc.undo();
    doc.redo();

    EXPECT_EQ(doc.mapData().sectors[0].walls[0].frontMaterialId.lo, 0xCAFEu);
}

TEST(CmdSetWallMaterial, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    daedalus::UUID id; id.lo = 1u;
    EXPECT_NO_THROW(doc.pushCommand(std::make_unique<CmdSetWallMaterial>(
        doc, 99u, 0u, WallSurface::Front, id)));
}

// ─── CmdSetSectorMaterial ──────────────────────────────────────────────────────────

TEST(CmdSetSectorMaterial, SetsFloorMaterial)
{
    EditMapDocument doc;
    addSquare(doc);

    daedalus::UUID id; id.lo = 0x1111u;
    doc.pushCommand(std::make_unique<CmdSetSectorMaterial>(
        doc, 0u, SectorSurface::Floor, id));

    EXPECT_EQ(doc.mapData().sectors[0].floorMaterialId.lo, 0x1111u);
}

TEST(CmdSetSectorMaterial, SetsCeilMaterial)
{
    EditMapDocument doc;
    addSquare(doc);

    daedalus::UUID id; id.lo = 0x2222u;
    doc.pushCommand(std::make_unique<CmdSetSectorMaterial>(
        doc, 0u, SectorSurface::Ceil, id));

    EXPECT_EQ(doc.mapData().sectors[0].ceilMaterialId.lo, 0x2222u);
}

TEST(CmdSetSectorMaterial, UndoRestoresMaterial)
{
    EditMapDocument doc;
    addSquare(doc);

    const daedalus::UUID original = doc.mapData().sectors[0].floorMaterialId;
    daedalus::UUID newId; newId.lo = 0xFFFFu;
    doc.pushCommand(std::make_unique<CmdSetSectorMaterial>(
        doc, 0u, SectorSurface::Floor, newId));
    doc.undo();

    EXPECT_EQ(doc.mapData().sectors[0].floorMaterialId.lo, original.lo);
}

// ─── CmdSetMapMeta ──────────────────────────────────────────────────────────────

TEST(CmdSetMapMeta, SetsNameAndAuthor)
{
    EditMapDocument doc;
    doc.pushCommand(std::make_unique<CmdSetMapMeta>(
        doc, "", "My Level", "", "Joe"));

    EXPECT_EQ(doc.mapData().name,   std::string("My Level"));
    EXPECT_EQ(doc.mapData().author, std::string("Joe"));
}

TEST(CmdSetMapMeta, UndoRestoresNameAndAuthor)
{
    EditMapDocument doc;
    doc.mapData().name   = "Original";
    doc.mapData().author = "Alice";
    doc.pushCommand(std::make_unique<CmdSetMapMeta>(
        doc, "Original", "New Name", "Alice", "Bob"));
    doc.undo();

    EXPECT_EQ(doc.mapData().name,   std::string("Original"));
    EXPECT_EQ(doc.mapData().author, std::string("Alice"));
}

// ─── CmdSetMapDefaults ──────────────────────────────────────────────────────────

TEST(CmdSetMapDefaults, SetsGravityAndSkyPath)
{
    EditMapDocument doc;
    doc.pushCommand(std::make_unique<CmdSetMapDefaults>(
        doc,
        "", "skies/sunny.hdr",
        9.81f, 20.0f,
        0.0f, -1.0f,
        4.0f, 6.0f));

    EXPECT_EQ      (doc.skyPath(),            std::string("skies/sunny.hdr"));
    EXPECT_FLOAT_EQ(doc.gravity(),            20.0f);
    EXPECT_FLOAT_EQ(doc.defaultFloorHeight(), -1.0f);
    EXPECT_FLOAT_EQ(doc.defaultCeilHeight(),   6.0f);
}

TEST(CmdSetMapDefaults, UndoRestoresValues)
{
    EditMapDocument doc;
    doc.setSkyPath("old_sky.hdr");
    doc.setGravity(9.81f);
    doc.setDefaultFloorHeight(0.0f);
    doc.setDefaultCeilHeight(4.0f);

    doc.pushCommand(std::make_unique<CmdSetMapDefaults>(
        doc,
        "old_sky.hdr", "new_sky.hdr",
        9.81f, 5.0f,
        0.0f, 1.0f,
        4.0f, 8.0f));
    doc.undo();

    EXPECT_EQ      (doc.skyPath(),  std::string("old_sky.hdr"));
    EXPECT_FLOAT_EQ(doc.gravity(),  9.81f);
    EXPECT_FLOAT_EQ(doc.defaultFloorHeight(), 0.0f);
    EXPECT_FLOAT_EQ(doc.defaultCeilHeight(),  4.0f);
}

// ─── CmdSetGlobalAmbient ─────────────────────────────────────────────────────────

TEST(CmdSetGlobalAmbient, SetsColorAndIntensity)
{
    EditMapDocument doc;
    doc.pushCommand(std::make_unique<CmdSetGlobalAmbient>(
        doc,
        glm::vec3{0.05f, 0.05f, 0.08f}, glm::vec3{0.5f, 0.3f, 0.1f},
        1.0f, 3.5f));

    EXPECT_FLOAT_EQ(doc.mapData().globalAmbientColor.r,     0.5f);
    EXPECT_FLOAT_EQ(doc.mapData().globalAmbientColor.b,     0.1f);
    EXPECT_FLOAT_EQ(doc.mapData().globalAmbientIntensity,   3.5f);
}

TEST(CmdSetGlobalAmbient, UndoRestores)
{
    EditMapDocument doc;
    const auto origColor = doc.mapData().globalAmbientColor;
    const float origI    = doc.mapData().globalAmbientIntensity;

    doc.pushCommand(std::make_unique<CmdSetGlobalAmbient>(
        doc,
        origColor, glm::vec3{1.0f},
        origI, 99.0f));
    doc.undo();

    EXPECT_FLOAT_EQ(doc.mapData().globalAmbientColor.r,   origColor.r);
    EXPECT_FLOAT_EQ(doc.mapData().globalAmbientIntensity, origI);
}

// ─── CmdSetVertexHeight ──────────────────────────────────────────────────────

TEST(CmdSetVertexHeight, SetsFloorOverride)
{
    EditMapDocument doc;
    addSquare(doc);

    doc.pushCommand(std::make_unique<CmdSetVertexHeight>(
        doc, 0u, 1u, 2.5f, std::nullopt));

    ASSERT_TRUE(doc.mapData().sectors[0].walls[1].floorHeightOverride.has_value());
    EXPECT_FLOAT_EQ(*doc.mapData().sectors[0].walls[1].floorHeightOverride, 2.5f);
    EXPECT_FALSE(doc.mapData().sectors[0].walls[1].ceilHeightOverride.has_value());
}

TEST(CmdSetVertexHeight, UndoRestoresOverride)
{
    EditMapDocument doc;
    addSquare(doc);

    doc.pushCommand(std::make_unique<CmdSetVertexHeight>(
        doc, 0u, 0u, 1.0f, 5.0f));
    ASSERT_TRUE(doc.mapData().sectors[0].walls[0].floorHeightOverride.has_value());

    doc.undo();

    EXPECT_FALSE(doc.mapData().sectors[0].walls[0].floorHeightOverride.has_value());
    EXPECT_FALSE(doc.mapData().sectors[0].walls[0].ceilHeightOverride.has_value());
}

TEST(CmdSetVertexHeight, ClearOverrideWithNullopt)
{
    EditMapDocument doc;
    addSquare(doc);
    // Pre-set an override directly.
    doc.mapData().sectors[0].walls[2].floorHeightOverride = 3.0f;

    doc.pushCommand(std::make_unique<CmdSetVertexHeight>(
        doc, 0u, 2u, std::nullopt, std::nullopt));

    EXPECT_FALSE(doc.mapData().sectors[0].walls[2].floorHeightOverride.has_value());
}

TEST(CmdSetVertexHeight, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    EXPECT_NO_THROW(doc.pushCommand(std::make_unique<CmdSetVertexHeight>(
        doc, 99u, 0u, 1.0f, 1.0f)));
}

// ─── CmdSetSectorFloorShape ──────────────────────────────────────────────────

TEST(CmdSetSectorFloorShape, SetsFloorShapeFlat)
{
    EditMapDocument doc;
    addSquare(doc);

    doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
        doc, 0u, FloorShape::Heightfield, std::nullopt));
    ASSERT_EQ(doc.mapData().sectors[0].floorShape, FloorShape::Heightfield);

    doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
        doc, 0u, FloorShape::Flat, std::nullopt));

    EXPECT_EQ(doc.mapData().sectors[0].floorShape, FloorShape::Flat);
}

TEST(CmdSetSectorFloorShape, SetsStairProfile)
{
    EditMapDocument doc;
    addSquare(doc);

    StairProfile p;
    p.stepCount   = 8u;
    p.riserHeight = 0.3f;
    p.treadDepth  = 0.6f;
    doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
        doc, 0u, FloorShape::VisualStairs, p));

    ASSERT_EQ(doc.mapData().sectors[0].floorShape, FloorShape::VisualStairs);
    ASSERT_TRUE(doc.mapData().sectors[0].stairProfile.has_value());
    EXPECT_EQ  (doc.mapData().sectors[0].stairProfile->stepCount,   8u);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].stairProfile->riserHeight, 0.3f);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].stairProfile->treadDepth,  0.6f);
}

TEST(CmdSetSectorFloorShape, UndoRestoresFloorShape)
{
    EditMapDocument doc;
    addSquare(doc);
    ASSERT_EQ(doc.mapData().sectors[0].floorShape, FloorShape::Flat);

    StairProfile p; p.stepCount = 4u;
    doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
        doc, 0u, FloorShape::VisualStairs, p));
    doc.undo();

    EXPECT_EQ(doc.mapData().sectors[0].floorShape, FloorShape::Flat);
    EXPECT_FALSE(doc.mapData().sectors[0].stairProfile.has_value());
}

TEST(CmdSetSectorFloorShape, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    EXPECT_NO_THROW(doc.pushCommand(std::make_unique<CmdSetSectorFloorShape>(
        doc, 99u, FloorShape::Flat, std::nullopt)));
}

// ─── CmdSetFloorPortal ───────────────────────────────────────────────────────

TEST(CmdSetFloorPortal, SetsFloorPortal)
{
    EditMapDocument doc;
    addSquare(doc);
    addSquare(doc, 4.0f, {10.0f, 0.0f});

    doc.pushCommand(std::make_unique<CmdSetFloorPortal>(
        doc, 0u, HPortalSurface::Floor, 1u, daedalus::UUID{}));

    EXPECT_EQ(doc.mapData().sectors[0].floorPortalSectorId, 1u);
    EXPECT_EQ(doc.mapData().sectors[0].ceilPortalSectorId,  INVALID_SECTOR_ID);
}

TEST(CmdSetFloorPortal, SetsCeilingPortal)
{
    EditMapDocument doc;
    addSquare(doc);
    addSquare(doc, 4.0f, {10.0f, 0.0f});

    doc.pushCommand(std::make_unique<CmdSetFloorPortal>(
        doc, 0u, HPortalSurface::Ceiling, 1u, daedalus::UUID{}));

    EXPECT_EQ(doc.mapData().sectors[0].ceilPortalSectorId,  1u);
    EXPECT_EQ(doc.mapData().sectors[0].floorPortalSectorId, INVALID_SECTOR_ID);
}

TEST(CmdSetFloorPortal, UndoRestoresFloorPortal)
{
    EditMapDocument doc;
    addSquare(doc);
    addSquare(doc, 4.0f, {10.0f, 0.0f});

    doc.pushCommand(std::make_unique<CmdSetFloorPortal>(
        doc, 0u, HPortalSurface::Floor, 1u, daedalus::UUID{}));
    ASSERT_EQ(doc.mapData().sectors[0].floorPortalSectorId, 1u);

    doc.undo();

    EXPECT_EQ(doc.mapData().sectors[0].floorPortalSectorId, INVALID_SECTOR_ID);
}

TEST(CmdSetFloorPortal, ClearPortalWithInvalidId)
{
    EditMapDocument doc;
    addSquare(doc);
    doc.mapData().sectors[0].floorPortalSectorId = 0u;  // self-link for test.

    doc.pushCommand(std::make_unique<CmdSetFloorPortal>(
        doc, 0u, HPortalSurface::Floor, INVALID_SECTOR_ID, daedalus::UUID{}));

    EXPECT_EQ(doc.mapData().sectors[0].floorPortalSectorId, INVALID_SECTOR_ID);
}

// ─── CmdSetWallCurve ─────────────────────────────────────────────────────────

TEST(CmdSetWallCurve, EnablesQuadraticCurve)
{
    EditMapDocument doc;
    addSquare(doc);

    const glm::vec2 ctrl{2.0f, 3.0f};
    doc.pushCommand(std::make_unique<CmdSetWallCurve>(
        doc, 0u, 0u, ctrl, std::nullopt, 16u));

    const Wall& w = doc.mapData().sectors[0].walls[0];
    ASSERT_TRUE(w.curveControlA.has_value());
    EXPECT_FLOAT_EQ(w.curveControlA->x, 2.0f);
    EXPECT_FLOAT_EQ(w.curveControlA->y, 3.0f);
    EXPECT_FALSE(w.curveControlB.has_value());
    EXPECT_EQ(w.curveSubdivisions, 16u);
}

TEST(CmdSetWallCurve, EnablesCubicCurve)
{
    EditMapDocument doc;
    addSquare(doc);

    doc.pushCommand(std::make_unique<CmdSetWallCurve>(
        doc, 0u, 1u, glm::vec2{1.0f, 0.0f}, glm::vec2{3.0f, 0.0f}, 8u));

    const Wall& w = doc.mapData().sectors[0].walls[1];
    ASSERT_TRUE(w.curveControlA.has_value());
    ASSERT_TRUE(w.curveControlB.has_value());
    EXPECT_FLOAT_EQ(w.curveControlB->x, 3.0f);
}

TEST(CmdSetWallCurve, UndoRestoresCurve)
{
    EditMapDocument doc;
    addSquare(doc);

    doc.pushCommand(std::make_unique<CmdSetWallCurve>(
        doc, 0u, 0u, glm::vec2{1.0f, 1.0f}, std::nullopt, 12u));
    ASSERT_TRUE(doc.mapData().sectors[0].walls[0].curveControlA.has_value());

    doc.undo();

    EXPECT_FALSE(doc.mapData().sectors[0].walls[0].curveControlA.has_value());
    EXPECT_EQ(doc.mapData().sectors[0].walls[0].curveSubdivisions, 12u);  // subdivisions unchanged
}

TEST(CmdSetWallCurve, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    EXPECT_NO_THROW(doc.pushCommand(std::make_unique<CmdSetWallCurve>(
        doc, 99u, 0u, glm::vec2{}, std::nullopt, 12u)));
}

// ─── CmdAddDetailBrush ───────────────────────────────────────────────────────

TEST(CmdAddDetailBrush, AddsBrush)
{
    EditMapDocument doc;
    addSquare(doc);
    ASSERT_EQ(doc.mapData().sectors[0].details.size(), 0u);

    DetailBrush db; db.type = DetailBrushType::Box;
    doc.pushCommand(std::make_unique<CmdAddDetailBrush>(doc, 0u, db));

    ASSERT_EQ(doc.mapData().sectors[0].details.size(), 1u);
    EXPECT_EQ(doc.mapData().sectors[0].details[0].type, DetailBrushType::Box);
}

TEST(CmdAddDetailBrush, UndoRemovesBrush)
{
    EditMapDocument doc;
    addSquare(doc);

    DetailBrush db; db.type = DetailBrushType::Cylinder;
    doc.pushCommand(std::make_unique<CmdAddDetailBrush>(doc, 0u, db));
    ASSERT_EQ(doc.mapData().sectors[0].details.size(), 1u);

    doc.undo();

    EXPECT_EQ(doc.mapData().sectors[0].details.size(), 0u);
}

TEST(CmdAddDetailBrush, RedoRestoresBrush)
{
    EditMapDocument doc;
    addSquare(doc);

    DetailBrush db; db.type = DetailBrushType::Wedge;
    doc.pushCommand(std::make_unique<CmdAddDetailBrush>(doc, 0u, db));
    doc.undo();
    doc.redo();

    ASSERT_EQ(doc.mapData().sectors[0].details.size(), 1u);
    EXPECT_EQ(doc.mapData().sectors[0].details[0].type, DetailBrushType::Wedge);
}

TEST(CmdAddDetailBrush, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    DetailBrush db;
    EXPECT_NO_THROW(doc.pushCommand(
        std::make_unique<CmdAddDetailBrush>(doc, 99u, db)));
}

// ─── CmdRemoveDetailBrush ────────────────────────────────────────────────────

TEST(CmdRemoveDetailBrush, RemovesBrush)
{
    EditMapDocument doc;
    addSquare(doc);

    DetailBrush db; db.type = DetailBrushType::ArchSpan;
    doc.mapData().sectors[0].details.push_back(db);
    ASSERT_EQ(doc.mapData().sectors[0].details.size(), 1u);

    doc.pushCommand(std::make_unique<CmdRemoveDetailBrush>(doc, 0u, 0u));

    EXPECT_EQ(doc.mapData().sectors[0].details.size(), 0u);
}

TEST(CmdRemoveDetailBrush, UndoRestoresBrushAtSameIndex)
{
    EditMapDocument doc;
    addSquare(doc);

    DetailBrush db; db.type = DetailBrushType::ArchSpan;
    db.geom.archHeight = 2.5f;
    doc.mapData().sectors[0].details.push_back(db);

    doc.pushCommand(std::make_unique<CmdRemoveDetailBrush>(doc, 0u, 0u));
    ASSERT_EQ(doc.mapData().sectors[0].details.size(), 0u);

    doc.undo();

    ASSERT_EQ(doc.mapData().sectors[0].details.size(), 1u);
    EXPECT_EQ(doc.mapData().sectors[0].details[0].type, DetailBrushType::ArchSpan);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].details[0].geom.archHeight, 2.5f);
}

TEST(CmdRemoveDetailBrush, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    addSquare(doc);
    // No brushes in sector; removing index 0 should not crash.
    EXPECT_NO_THROW(doc.pushCommand(
        std::make_unique<CmdRemoveDetailBrush>(doc, 0u, 0u)));
}

// ─── CmdSetDetailBrush ───────────────────────────────────────────────────────

TEST(CmdSetDetailBrush, ReplacesBrushType)
{
    EditMapDocument doc;
    addSquare(doc);

    DetailBrush original; original.type = DetailBrushType::Box;
    doc.mapData().sectors[0].details.push_back(original);

    DetailBrush updated; updated.type = DetailBrushType::Cylinder;
    updated.geom.radius = 1.5f;
    doc.pushCommand(std::make_unique<CmdSetDetailBrush>(doc, 0u, 0u, updated));

    ASSERT_EQ(doc.mapData().sectors[0].details.size(), 1u);
    EXPECT_EQ(doc.mapData().sectors[0].details[0].type, DetailBrushType::Cylinder);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].details[0].geom.radius, 1.5f);
}

TEST(CmdSetDetailBrush, UndoRestoresOriginalBrush)
{
    EditMapDocument doc;
    addSquare(doc);

    DetailBrush original; original.type = DetailBrushType::Wedge;
    original.geom.halfExtents = {1.0f, 2.0f, 3.0f};
    doc.mapData().sectors[0].details.push_back(original);

    DetailBrush updated; updated.type = DetailBrushType::Box;
    doc.pushCommand(std::make_unique<CmdSetDetailBrush>(doc, 0u, 0u, updated));
    doc.undo();

    EXPECT_EQ(doc.mapData().sectors[0].details[0].type, DetailBrushType::Wedge);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].details[0].geom.halfExtents.y, 2.0f);
}

// ─── CmdSetHeightfield ───────────────────────────────────────────────────────

TEST(CmdSetHeightfield, SetsHeightfieldAndFlag)
{
    EditMapDocument doc;
    addSquare(doc);
    ASSERT_FALSE(doc.mapData().sectors[0].heightfield.has_value());
    ASSERT_FALSE(hasFlag(doc.mapData().sectors[0].flags, SectorFlags::HasHeightfield));

    HeightfieldFloor hf;
    hf.gridWidth = 4u; hf.gridDepth = 4u;
    hf.worldMin  = {0.0f, 0.0f}; hf.worldMax = {4.0f, 4.0f};
    hf.samples.assign(16u, 0.5f);
    doc.pushCommand(std::make_unique<CmdSetHeightfield>(doc, 0u, hf));

    ASSERT_TRUE(doc.mapData().sectors[0].heightfield.has_value());
    EXPECT_EQ(doc.mapData().sectors[0].heightfield->gridWidth, 4u);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].heightfield->samples[0], 0.5f);
    EXPECT_TRUE(hasFlag(doc.mapData().sectors[0].flags, SectorFlags::HasHeightfield));
}

TEST(CmdSetHeightfield, ClearHeightfieldRemovesFlag)
{
    EditMapDocument doc;
    addSquare(doc);
    HeightfieldFloor hf;
    hf.gridWidth = 2u; hf.gridDepth = 2u;
    hf.worldMin  = {0.0f, 0.0f}; hf.worldMax = {2.0f, 2.0f};
    hf.samples.assign(4u, 1.0f);
    doc.mapData().sectors[0].heightfield = hf;
    doc.mapData().sectors[0].flags =
        doc.mapData().sectors[0].flags | SectorFlags::HasHeightfield;

    doc.pushCommand(std::make_unique<CmdSetHeightfield>(doc, 0u, std::nullopt));

    EXPECT_FALSE(doc.mapData().sectors[0].heightfield.has_value());
    EXPECT_FALSE(hasFlag(doc.mapData().sectors[0].flags, SectorFlags::HasHeightfield));
}

TEST(CmdSetHeightfield, UndoRestoresHeightfield)
{
    EditMapDocument doc;
    addSquare(doc);

    HeightfieldFloor hf;
    hf.gridWidth = 3u; hf.gridDepth = 3u;
    hf.worldMin  = {0.0f, 0.0f}; hf.worldMax = {3.0f, 3.0f};
    hf.samples.assign(9u, 2.0f);
    doc.pushCommand(std::make_unique<CmdSetHeightfield>(doc, 0u, hf));
    ASSERT_TRUE(doc.mapData().sectors[0].heightfield.has_value());

    doc.undo();

    EXPECT_FALSE(doc.mapData().sectors[0].heightfield.has_value());
    EXPECT_FALSE(hasFlag(doc.mapData().sectors[0].flags, SectorFlags::HasHeightfield));
}

TEST(CmdSetHeightfield, RedoRestoresHeightfield)
{
    EditMapDocument doc;
    addSquare(doc);

    HeightfieldFloor hf;
    hf.gridWidth = 2u; hf.gridDepth = 2u;
    hf.worldMin  = {0.0f, 0.0f}; hf.worldMax = {2.0f, 2.0f};
    hf.samples   = {1.0f, 2.0f, 3.0f, 4.0f};
    doc.pushCommand(std::make_unique<CmdSetHeightfield>(doc, 0u, hf));
    doc.undo();
    doc.redo();

    ASSERT_TRUE(doc.mapData().sectors[0].heightfield.has_value());
    EXPECT_EQ(doc.mapData().sectors[0].heightfield->samples.size(), 4u);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].heightfield->samples[3], 4.0f);
}

TEST(CmdSetHeightfield, OutOfRangeIsNoop)
{
    EditMapDocument doc;
    HeightfieldFloor hf;
    hf.gridWidth = 2u; hf.gridDepth = 2u;
    hf.samples.assign(4u, 0.0f);
    EXPECT_NO_THROW(doc.pushCommand(
        std::make_unique<CmdSetHeightfield>(doc, 99u, hf)));
}

// ─── Multi-vertex drag (via CompoundCommand + CmdMoveVertex) ─────────────────
// Tests that moving multiple vertices as one compound command undoes as one
// step — verifying the grouped-drag undo contract required by the editor.

TEST(MultiVertexDrag, MovesTwoVertices)
{
    EditMapDocument doc;
    addSquare(doc);  // walls: (0,0),(4,0),(4,4),(0,4)

    // Simulate dragging walls 0 and 1 together by delta (1, 2).
    constexpr glm::vec2 delta{1.0f, 2.0f};
    const glm::vec2 origW0 = doc.mapData().sectors[0].walls[0].p0;
    const glm::vec2 origW1 = doc.mapData().sectors[0].walls[1].p0;

    // Emit the compound command as the drag would.
    std::vector<std::unique_ptr<ICommand>> steps;
    steps.push_back(std::make_unique<CmdMoveVertex>(
        doc, 0u, 0u, origW0, origW0 + delta));
    steps.push_back(std::make_unique<CmdMoveVertex>(
        doc, 0u, 1u, origW1, origW1 + delta));
    doc.pushCommand(std::make_unique<CompoundCommand>(
        "Move Vertices", std::move(steps)));

    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[0].p0.x, origW0.x + delta.x);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[0].p0.y, origW0.y + delta.y);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[1].p0.x, origW1.x + delta.x);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[1].p0.y, origW1.y + delta.y);
}

TEST(MultiVertexDrag, UndoRestoresBothVertices)
{
    EditMapDocument doc;
    addSquare(doc);

    const glm::vec2 origW0 = doc.mapData().sectors[0].walls[0].p0;
    const glm::vec2 origW2 = doc.mapData().sectors[0].walls[2].p0;

    std::vector<std::unique_ptr<ICommand>> steps;
    steps.push_back(std::make_unique<CmdMoveVertex>(
        doc, 0u, 0u, origW0, origW0 + glm::vec2{5.0f, 5.0f}));
    steps.push_back(std::make_unique<CmdMoveVertex>(
        doc, 0u, 2u, origW2, origW2 + glm::vec2{5.0f, 5.0f}));
    doc.pushCommand(std::make_unique<CompoundCommand>(
        "Move Vertices", std::move(steps)));

    doc.undo();

    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[0].p0.x, origW0.x);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[0].p0.y, origW0.y);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[2].p0.x, origW2.x);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[2].p0.y, origW2.y);
}

TEST(MultiVertexDrag, UndoIsSingleStep)
{
    EditMapDocument doc;
    addSquare(doc);

    const glm::vec2 w0 = doc.mapData().sectors[0].walls[0].p0;
    const glm::vec2 w1 = doc.mapData().sectors[0].walls[1].p0;

    std::vector<std::unique_ptr<ICommand>> steps;
    steps.push_back(std::make_unique<CmdMoveVertex>(
        doc, 0u, 0u, w0, w0 + glm::vec2{3.0f, 0.0f}));
    steps.push_back(std::make_unique<CmdMoveVertex>(
        doc, 0u, 1u, w1, w1 + glm::vec2{3.0f, 0.0f}));
    doc.pushCommand(std::make_unique<CompoundCommand>(
        "Move Vertices", std::move(steps)));

    EXPECT_TRUE(doc.undoStack().canUndo());
    doc.undo();
    // After one undo step, neither vertex should have moved.
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[0].p0.x, w0.x);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[1].p0.x, w1.x);
    // One undo exhausted the stack (single compound).
    EXPECT_FALSE(doc.undoStack().canUndo());
}

TEST(MultiVertexDrag, RedoReappliesBothVertices)
{
    EditMapDocument doc;
    addSquare(doc);

    const glm::vec2 w0 = doc.mapData().sectors[0].walls[0].p0;
    const glm::vec2 w3 = doc.mapData().sectors[0].walls[3].p0;

    std::vector<std::unique_ptr<ICommand>> steps;
    steps.push_back(std::make_unique<CmdMoveVertex>(
        doc, 0u, 0u, w0, w0 + glm::vec2{0.0f, 2.0f}));
    steps.push_back(std::make_unique<CmdMoveVertex>(
        doc, 0u, 3u, w3, w3 + glm::vec2{0.0f, 2.0f}));
    doc.pushCommand(std::make_unique<CompoundCommand>(
        "Move Vertices", std::move(steps)));
    doc.undo();

    ASSERT_FLOAT_EQ(doc.mapData().sectors[0].walls[0].p0.y, w0.y);

    doc.redo();

    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[0].p0.y, w0.y + 2.0f);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[3].p0.y, w3.y + 2.0f);
}

// ─── SelectionState helpers ──────────────────────────────────────────────────
// Unit tests for the new SelectionItem / SelectionState API.

TEST(SelectionState, ClearResetsItems)
{
    SelectionState sel;
    sel.items.push_back({SelectionType::Sector, 0u, 0});
    sel.items.push_back({SelectionType::Light, INVALID_SECTOR_ID, 2});
    sel.clear();
    EXPECT_TRUE(sel.items.empty());
    EXPECT_FALSE(sel.hasSelection());
}

TEST(SelectionState, UniformTypeSingleItem)
{
    SelectionState sel;
    sel.items.push_back({SelectionType::Wall, 1u, 3});
    EXPECT_EQ(sel.uniformType(), SelectionType::Wall);
}

TEST(SelectionState, UniformTypeMixedReturnsNone)
{
    SelectionState sel;
    sel.items.push_back({SelectionType::Sector, 0u, 0});
    sel.items.push_back({SelectionType::Light, INVALID_SECTOR_ID, 0});
    EXPECT_EQ(sel.uniformType(), SelectionType::None);
}

TEST(SelectionState, UniformTypeEmptyReturnsNone)
{
    SelectionState sel;
    EXPECT_EQ(sel.uniformType(), SelectionType::None);
}

TEST(SelectionState, HasSingleOf)
{
    SelectionState sel;
    sel.items.push_back({SelectionType::Entity, INVALID_SECTOR_ID, 7});
    EXPECT_TRUE(sel.hasSingleOf(SelectionType::Entity));
    EXPECT_FALSE(sel.hasSingleOf(SelectionType::Light));
    sel.items.push_back({SelectionType::Entity, INVALID_SECTOR_ID, 8});
    EXPECT_FALSE(sel.hasSingleOf(SelectionType::Entity));  // two items
}

TEST(SelectionState, IsSectorSelected)
{
    SelectionState sel;
    sel.items.push_back({SelectionType::Sector, 2u, 0});
    sel.items.push_back({SelectionType::Sector, 5u, 0});
    EXPECT_TRUE(sel.isSectorSelected(2u));
    EXPECT_TRUE(sel.isSectorSelected(5u));
    EXPECT_FALSE(sel.isSectorSelected(3u));
}

TEST(SelectionState, SelectedSectors)
{
    SelectionState sel;
    sel.items.push_back({SelectionType::Sector, 1u, 0});
    sel.items.push_back({SelectionType::Light,  INVALID_SECTOR_ID, 0});
    sel.items.push_back({SelectionType::Sector, 3u, 0});
    const auto secs = sel.selectedSectors();
    ASSERT_EQ(secs.size(), 2u);
    EXPECT_EQ(secs[0], 1u);
    EXPECT_EQ(secs[1], 3u);
}

TEST(SelectionState, SelectAllFillsSectors)
{
    SelectionState sel;
    sel.selectAll(4u);
    ASSERT_EQ(sel.items.size(), 4u);
    for (std::size_t i = 0; i < 4u; ++i)
    {
        EXPECT_EQ(sel.items[i].type, SelectionType::Sector);
        EXPECT_EQ(sel.items[i].sectorId, static_cast<SectorId>(i));
    }
}
