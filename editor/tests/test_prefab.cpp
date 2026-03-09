// test_prefab.cpp
// Unit tests for the Gate 6 prefab system:
//   - CompoundCommand execute / undo ordering
//   - CmdSavePrefab sector capture, pivot-relative positions, entity capture
//   - CmdPlacePrefab sector + entity placement with XZ offset
//   - Sidecar save/load roundtrip for the prefab library

#include "daedalus/editor/compound_command.h"
#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/entity_def.h"
#include "daedalus/editor/prefab_def.h"
#include "daedalus/editor/selection_state.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"
#include "document/commands/cmd_save_prefab.h"
#include "document/commands/cmd_place_prefab.h"
#include "document/emap_sidecar.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <memory>

using namespace daedalus;
using namespace daedalus::editor;
using namespace daedalus::world;

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// Builds and pushes an axis-aligned square sector; selects it.
static SectorId addAndSelectSquare(EditMapDocument& doc,
                                    float size     = 4.0f,
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

static void selectSectors(EditMapDocument& doc, std::initializer_list<SectorId> ids)
{
    auto& sel = doc.selection();
    sel.clear();
    sel.type = SelectionType::Sector;
    for (SectorId id : ids)
        sel.sectors.push_back(id);
}

/// RAII temporary file deleted on scope exit.
struct TempFile
{
    std::filesystem::path path;
    explicit TempFile(const char* name)
        : path(std::filesystem::temp_directory_path() / name) {}
    ~TempFile() { std::filesystem::remove(path); }
};

// ─── CompoundCommand ─────────────────────────────────────────────────────────

namespace
{
/// Simple counter command for testing CompoundCommand ordering.
struct CounterCmd : ICommand
{
    int& counter;
    explicit CounterCmd(int& c) : counter(c) {}
    void execute() override { ++counter; }
    void undo()    override { --counter; }
    [[nodiscard]] std::string description() const override { return "Count"; }
};
} // anonymous namespace

TEST(CompoundCommand, ExecuteRunsStepsInOrder)
{
    std::vector<int> log;

    // Build steps that record their order.
    struct LogCmd : ICommand {
        std::vector<int>& log;
        int value;
        LogCmd(std::vector<int>& l, int v) : log(l), value(v) {}
        void execute() override { log.push_back(value); }
        void undo()    override { log.push_back(-value); }
        [[nodiscard]] std::string description() const override { return "Log"; }
    };

    std::vector<std::unique_ptr<ICommand>> steps;
    steps.push_back(std::make_unique<LogCmd>(log, 1));
    steps.push_back(std::make_unique<LogCmd>(log, 2));
    steps.push_back(std::make_unique<LogCmd>(log, 3));

    CompoundCommand cc("test", std::move(steps));
    cc.execute();

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], 1);
    EXPECT_EQ(log[1], 2);
    EXPECT_EQ(log[2], 3);
}

TEST(CompoundCommand, UndoRunsStepsInReverse)
{
    std::vector<int> log;
    struct LogCmd : ICommand {
        std::vector<int>& log;
        int value;
        LogCmd(std::vector<int>& l, int v) : log(l), value(v) {}
        void execute() override { log.push_back(value); }
        void undo()    override { log.push_back(-value); }
        [[nodiscard]] std::string description() const override { return "Log"; }
    };

    std::vector<std::unique_ptr<ICommand>> steps;
    steps.push_back(std::make_unique<LogCmd>(log, 1));
    steps.push_back(std::make_unique<LogCmd>(log, 2));
    steps.push_back(std::make_unique<LogCmd>(log, 3));

    CompoundCommand cc("test", std::move(steps));
    cc.execute();
    log.clear();

    cc.undo();

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], -3);
    EXPECT_EQ(log[1], -2);
    EXPECT_EQ(log[2], -1);
}

TEST(CompoundCommand, EmptyCompoundIsNoop)
{
    int counter = 5;
    CompoundCommand cc("empty", {});
    cc.execute();
    cc.undo();
    EXPECT_EQ(counter, 5);
}

TEST(CompoundCommand, DescriptionReturnsGivenString)
{
    CompoundCommand cc("My Compound", {});
    EXPECT_EQ(cc.description(), "My Compound");
}

TEST(CompoundCommand, CounterCommandExecuteUndo)
{
    int n = 0;
    std::vector<std::unique_ptr<ICommand>> steps;
    steps.push_back(std::make_unique<CounterCmd>(n));
    steps.push_back(std::make_unique<CounterCmd>(n));

    CompoundCommand cc("two counts", std::move(steps));
    cc.execute();
    EXPECT_EQ(n, 2);

    cc.undo();
    EXPECT_EQ(n, 0);
}

// ─── CmdSavePrefab ────────────────────────────────────────────────────────────

TEST(CmdSavePrefab, CaptureSingleSector)
{
    EditMapDocument doc;
    const SectorId id = addAndSelectSquare(doc, 4.0f, {0.0f, 0.0f});
    selectSectors(doc, {id});

    doc.pushCommand(std::make_unique<CmdSavePrefab>(doc, "room_a"));

    ASSERT_EQ(doc.prefabs().size(), 1u);
    EXPECT_EQ(doc.prefabs()[0].name, "room_a");
    EXPECT_EQ(doc.prefabs()[0].sectors.size(), 1u);
    EXPECT_EQ(doc.prefabs()[0].sectors[0].walls.size(), 4u);
}

TEST(CmdSavePrefab, WallPositionsArePivotRelative)
{
    EditMapDocument doc;
    // 4x4 square at (10, 20): vertices at (10,20), (14,20), (14,24), (10,24).
    // Pivot should be centroid = (12, 22).
    // Relative vertices: (-2,-2), (2,-2), (2,2), (-2,2).
    const SectorId id = addAndSelectSquare(doc, 4.0f, {10.0f, 20.0f});
    selectSectors(doc, {id});

    doc.pushCommand(std::make_unique<CmdSavePrefab>(doc, "offset_room"));

    ASSERT_EQ(doc.prefabs().size(), 1u);
    const auto& walls = doc.prefabs()[0].sectors[0].walls;
    ASSERT_EQ(walls.size(), 4u);

    // Wall 0 should be at (-2, -2) relative to pivot (12, 22).
    EXPECT_FLOAT_EQ(walls[0].p0.x, -2.0f);
    EXPECT_FLOAT_EQ(walls[0].p0.y, -2.0f);
}

TEST(CmdSavePrefab, PortalLinksAreStripped)
{
    EditMapDocument doc;
    Sector s;
    s.walls.push_back(Wall{.p0 = {0,0}, .portalSectorId = 0u});
    s.walls.push_back(Wall{.p0 = {4,0}});
    s.walls.push_back(Wall{.p0 = {4,4}});
    s.walls.push_back(Wall{.p0 = {0,4}});
    doc.mapData().sectors.push_back(s);
    selectSectors(doc, {0u});

    doc.pushCommand(std::make_unique<CmdSavePrefab>(doc, "portal_room"));

    ASSERT_EQ(doc.prefabs().size(), 1u);
    for (const Wall& w : doc.prefabs()[0].sectors[0].walls)
        EXPECT_EQ(w.portalSectorId, INVALID_SECTOR_ID);
}

TEST(CmdSavePrefab, EntitiesWithinAABBAreCaptured)
{
    EditMapDocument doc;
    // 4x4 sector at (0,0)...(4,4)
    const SectorId id = addAndSelectSquare(doc, 4.0f, {0.0f, 0.0f});
    selectSectors(doc, {id});

    // Entity inside AABB (XZ = 2, 2)
    EntityDef inside;
    inside.position = {2.0f, 0.0f, 2.0f};
    inside.assetPath = "inside.png";
    doc.entities().push_back(inside);

    // Entity outside AABB (XZ = 10, 10)
    EntityDef outside;
    outside.position = {10.0f, 0.0f, 10.0f};
    outside.assetPath = "outside.png";
    doc.entities().push_back(outside);

    doc.pushCommand(std::make_unique<CmdSavePrefab>(doc, "with_entity"));

    ASSERT_EQ(doc.prefabs().size(), 1u);
    ASSERT_EQ(doc.prefabs()[0].entities.size(), 1u);
    EXPECT_EQ(doc.prefabs()[0].entities[0].assetPath, "inside.png");
}

TEST(CmdSavePrefab, EntityPositionIsPivotRelative)
{
    EditMapDocument doc;
    // 4x4 at (0,0); pivot = (2,2)
    const SectorId id = addAndSelectSquare(doc, 4.0f, {0.0f, 0.0f});
    selectSectors(doc, {id});

    EntityDef ed;
    ed.position = {3.0f, 0.0f, 1.0f};  // XZ = (3,1), relative to pivot (2,2) = (1,-1)
    doc.entities().push_back(ed);

    doc.pushCommand(std::make_unique<CmdSavePrefab>(doc, "entity_pivot"));

    ASSERT_EQ(doc.prefabs()[0].entities.size(), 1u);
    EXPECT_FLOAT_EQ(doc.prefabs()[0].entities[0].position.x,  1.0f);
    EXPECT_FLOAT_EQ(doc.prefabs()[0].entities[0].position.z, -1.0f);
}

TEST(CmdSavePrefab, UndoRemovesPrefab)
{
    EditMapDocument doc;
    const SectorId id = addAndSelectSquare(doc, 4.0f);
    selectSectors(doc, {id});

    doc.pushCommand(std::make_unique<CmdSavePrefab>(doc, "temp"));
    ASSERT_EQ(doc.prefabs().size(), 1u);

    doc.undo();

    EXPECT_EQ(doc.prefabs().size(), 0u);
}

TEST(CmdSavePrefab, RedoRestoresPrefab)
{
    EditMapDocument doc;
    const SectorId id = addAndSelectSquare(doc, 4.0f);
    selectSectors(doc, {id});

    doc.pushCommand(std::make_unique<CmdSavePrefab>(doc, "redo_test"));
    doc.undo();
    ASSERT_EQ(doc.prefabs().size(), 0u);

    doc.redo();
    ASSERT_EQ(doc.prefabs().size(), 1u);
    EXPECT_EQ(doc.prefabs()[0].name, "redo_test");
}

TEST(CmdSavePrefab, NoSelectionIsNoop)
{
    EditMapDocument doc;
    doc.selection().clear();  // SelectionType::None

    doc.pushCommand(std::make_unique<CmdSavePrefab>(doc, "empty_sel"));

    // An entry is added but it holds no content (graceful no-op capture).
    ASSERT_EQ(doc.prefabs().size(), 1u);
    EXPECT_EQ(doc.prefabs()[0].sectors.size(), 0u);
    EXPECT_EQ(doc.prefabs()[0].entities.size(), 0u);
}

// ─── CmdPlacePrefab ──────────────────────────────────────────────────────────

TEST(CmdPlacePrefab, PlacesSectorsAtOffset)
{
    EditMapDocument doc;
    ASSERT_EQ(doc.mapData().sectors.size(), 0u);

    // Build a prefab with a single unit square (pivot-relative: (0,0)..(1,1)).
    PrefabDef pf;
    pf.name = "unit_square";
    {
        Sector s;
        s.walls.push_back(Wall{.p0 = {0.0f, 0.0f}});
        s.walls.push_back(Wall{.p0 = {1.0f, 0.0f}});
        s.walls.push_back(Wall{.p0 = {1.0f, 1.0f}});
        s.walls.push_back(Wall{.p0 = {0.0f, 1.0f}});
        pf.sectors.push_back(std::move(s));
    }

    doc.pushCommand(std::make_unique<CmdPlacePrefab>(doc, pf, glm::vec2{10.0f, 5.0f}));

    ASSERT_EQ(doc.mapData().sectors.size(), 1u);
    const auto& walls = doc.mapData().sectors[0].walls;
    EXPECT_FLOAT_EQ(walls[0].p0.x, 10.0f);
    EXPECT_FLOAT_EQ(walls[0].p0.y,  5.0f);
    EXPECT_FLOAT_EQ(walls[1].p0.x, 11.0f);
    EXPECT_FLOAT_EQ(walls[1].p0.y,  5.0f);
}

TEST(CmdPlacePrefab, PlacesEntitiesAtOffset)
{
    EditMapDocument doc;
    ASSERT_EQ(doc.entities().size(), 0u);

    PrefabDef pf;
    pf.name = "entity_prefab";
    {
        EntityDef ed;
        ed.position  = {1.0f, 0.0f, 2.0f};  // pivot-relative
        ed.assetPath = "props/barrel.png";
        pf.entities.push_back(ed);
    }

    doc.pushCommand(std::make_unique<CmdPlacePrefab>(doc, pf, glm::vec2{5.0f, 3.0f}));

    ASSERT_EQ(doc.entities().size(), 1u);
    EXPECT_FLOAT_EQ(doc.entities()[0].position.x, 6.0f);  // 1+5
    EXPECT_FLOAT_EQ(doc.entities()[0].position.z, 5.0f);  // 2+3
    EXPECT_EQ(doc.entities()[0].assetPath, "props/barrel.png");
}

TEST(CmdPlacePrefab, UndoRemovesAllPlacements)
{
    EditMapDocument doc;

    PrefabDef pf;
    pf.name = "multi";
    for (int i = 0; i < 3; ++i)
    {
        Sector s;
        s.walls.push_back(Wall{.p0 = {0.0f, 0.0f}});
        s.walls.push_back(Wall{.p0 = {1.0f, 0.0f}});
        s.walls.push_back(Wall{.p0 = {1.0f, 1.0f}});
        pf.sectors.push_back(std::move(s));
    }

    doc.pushCommand(std::make_unique<CmdPlacePrefab>(doc, pf, glm::vec2{}));
    ASSERT_EQ(doc.mapData().sectors.size(), 3u);

    doc.undo();

    EXPECT_EQ(doc.mapData().sectors.size(), 0u);
}

TEST(CmdPlacePrefab, RedoRestoresPlacements)
{
    EditMapDocument doc;

    PrefabDef pf;
    pf.name = "redo_place";
    {
        Sector s;
        s.walls.push_back(Wall{.p0 = {0,0}});
        s.walls.push_back(Wall{.p0 = {2,0}});
        s.walls.push_back(Wall{.p0 = {2,2}});
        pf.sectors.push_back(std::move(s));
    }

    doc.pushCommand(std::make_unique<CmdPlacePrefab>(doc, pf, glm::vec2{1.0f, 1.0f}));
    doc.undo();
    ASSERT_EQ(doc.mapData().sectors.size(), 0u);

    doc.redo();

    ASSERT_EQ(doc.mapData().sectors.size(), 1u);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[0].p0.x, 1.0f);
    EXPECT_FLOAT_EQ(doc.mapData().sectors[0].walls[0].p0.y, 1.0f);
}

TEST(CmdPlacePrefab, EmptyPrefabIsNoop)
{
    EditMapDocument doc;
    PrefabDef empty;
    empty.name = "nothing";

    EXPECT_NO_THROW(doc.pushCommand(
        std::make_unique<CmdPlacePrefab>(doc, empty, glm::vec2{})));
    EXPECT_EQ(doc.mapData().sectors.size(), 0u);
    EXPECT_EQ(doc.entities().size(),        0u);
}

// ─── Sidecar prefab roundtrip ─────────────────────────────────────────────────

TEST(EmapPrefabRoundtrip, EmptyPrefabLibraryRoundtrip)
{
    TempFile tmp("test_emap_prefab_empty.emap");
    EditMapDocument docOut;
    ASSERT_EQ(docOut.prefabs().size(), 0u);

    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());
    EXPECT_EQ(docIn.prefabs().size(), 0u);
}

TEST(EmapPrefabRoundtrip, PrefabNamePreserved)
{
    TempFile tmp("test_emap_prefab_name.emap");

    EditMapDocument docOut;
    {
        PrefabDef pf;
        pf.name = "entry_hall";
        docOut.prefabs().push_back(std::move(pf));
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());
    ASSERT_EQ(docIn.prefabs().size(), 1u);
    EXPECT_EQ(docIn.prefabs()[0].name, "entry_hall");
}

TEST(EmapPrefabRoundtrip, PrefabSectorsRoundtrip)
{
    TempFile tmp("test_emap_prefab_sectors.emap");

    EditMapDocument docOut;
    {
        PrefabDef pf;
        pf.name = "room";
        Sector s;
        s.floorHeight = -1.0f;
        s.ceilHeight  =  5.0f;
        s.walls.push_back(Wall{.p0 = {-2.0f, -2.0f}});
        s.walls.push_back(Wall{.p0 = { 2.0f, -2.0f}});
        s.walls.push_back(Wall{.p0 = { 2.0f,  2.0f}});
        s.walls.push_back(Wall{.p0 = {-2.0f,  2.0f}});
        pf.sectors.push_back(std::move(s));
        docOut.prefabs().push_back(std::move(pf));
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_EQ(docIn.prefabs().size(), 1u);
    const PrefabDef& pf = docIn.prefabs()[0];
    EXPECT_EQ(pf.name, "room");
    ASSERT_EQ(pf.sectors.size(), 1u);
    EXPECT_FLOAT_EQ(pf.sectors[0].floorHeight, -1.0f);
    EXPECT_FLOAT_EQ(pf.sectors[0].ceilHeight,   5.0f);
    ASSERT_EQ(pf.sectors[0].walls.size(), 4u);
    EXPECT_FLOAT_EQ(pf.sectors[0].walls[0].p0.x, -2.0f);
    EXPECT_FLOAT_EQ(pf.sectors[0].walls[0].p0.y, -2.0f);
    EXPECT_FLOAT_EQ(pf.sectors[0].walls[2].p0.x,  2.0f);
    EXPECT_FLOAT_EQ(pf.sectors[0].walls[2].p0.y,  2.0f);
}

TEST(EmapPrefabRoundtrip, PrefabEntitiesRoundtrip)
{
    TempFile tmp("test_emap_prefab_entities.emap");

    EditMapDocument docOut;
    {
        PrefabDef pf;
        pf.name = "room_with_barrel";
        EntityDef ed;
        ed.visualType = EntityVisualType::VoxelObject;
        ed.position   = {1.5f, 0.0f, -0.5f};
        ed.assetPath  = "props/barrel.vox";
        ed.layerIndex = 2u;
        pf.entities.push_back(ed);
        docOut.prefabs().push_back(std::move(pf));
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_EQ(docIn.prefabs().size(), 1u);
    const PrefabDef& pf = docIn.prefabs()[0];
    ASSERT_EQ(pf.entities.size(), 1u);
    EXPECT_EQ(pf.entities[0].visualType, EntityVisualType::VoxelObject);
    EXPECT_FLOAT_EQ(pf.entities[0].position.x,  1.5f);
    EXPECT_FLOAT_EQ(pf.entities[0].position.z, -0.5f);
    EXPECT_EQ(pf.entities[0].assetPath, "props/barrel.vox");
    EXPECT_EQ(pf.entities[0].layerIndex, 2u);
}

TEST(EmapPrefabRoundtrip, MultiplePrefabsPreserveOrder)
{
    TempFile tmp("test_emap_prefab_multi.emap");

    EditMapDocument docOut;
    for (const char* name : {"alpha", "beta", "gamma"})
    {
        PrefabDef pf;
        pf.name = name;
        docOut.prefabs().push_back(std::move(pf));
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_EQ(docIn.prefabs().size(), 3u);
    EXPECT_EQ(docIn.prefabs()[0].name, "alpha");
    EXPECT_EQ(docIn.prefabs()[1].name, "beta");
    EXPECT_EQ(docIn.prefabs()[2].name, "gamma");
}

TEST(EmapPrefabRoundtrip, PrefabsCoexistWithEntitiesAndLights)
{
    TempFile tmp("test_emap_prefab_coexist.emap");

    EditMapDocument docOut;
    {
        EntityDef ed; ed.assetPath = "props/torch.png";
        docOut.entities().push_back(ed);
    }
    {
        PrefabDef pf; pf.name = "coexist";
        docOut.prefabs().push_back(std::move(pf));
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    EXPECT_EQ(docIn.entities().size(), 1u);
    EXPECT_EQ(docIn.entities()[0].assetPath, "props/torch.png");
    EXPECT_EQ(docIn.prefabs().size(), 1u);
    EXPECT_EQ(docIn.prefabs()[0].name, "coexist");
}
