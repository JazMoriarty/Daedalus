// test_dlevel_io.cpp
// Unit tests for .dlevel binary serialisation round-trip.
// Focuses on v3 entity fields: script descriptor + audio descriptor.

#include "daedalus/world/dlevel_io.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace daedalus;
using namespace daedalus::world;

// ─── Test fixture ─────────────────────────────────────────────────────────────

class DlevelIOTest : public ::testing::Test
{
protected:
    /// Build a minimal LevelPackData exercising all v3 entity fields.
    static LevelPackData makeSamplePack()
    {
        LevelPackData pack;

        // Entity 0: physics + script + audio (all v3 fields populated).
        LevelEntity e0;
        e0.name        = "ScriptedBarrel";
        e0.position    = {1.0f, 0.0f, 2.0f};
        e0.yaw         = 0.785f;  // ~45 degrees
        e0.sectorId    = 0u;
        e0.shape       = LevelCollisionShape::Box;
        e0.dynamic     = true;
        e0.mass        = 5.0f;
        e0.halfExtents = {0.3f, 0.4f, 0.3f};
        e0.scriptPath  = "scripts/barrel.lua";
        e0.exposedVars = {{"health", "100"}, {"explosive", "true"}};
        e0.soundPath          = "sounds/creak.ogg";
        e0.soundFalloffRadius = 8.0f;
        e0.soundVolume        = 0.75f;
        e0.soundLoop          = true;
        e0.soundAutoPlay      = false;
        pack.entities.push_back(e0);

        // Entity 1: script-only (no physics shape, no audio).
        LevelEntity e1;
        e1.name        = "TriggerZone";
        e1.position    = {5.0f, 0.0f, 5.0f};
        e1.shape       = LevelCollisionShape::None;
        e1.scriptPath  = "scripts/trigger.lua";
        e1.exposedVars = {{"event_id", "42"}};
        pack.entities.push_back(e1);

        // Entity 2: audio-only (no physics shape, no script).
        LevelEntity e2;
        e2.name              = "AmbientSpeaker";
        e2.position          = {-3.0f, 2.0f, 0.0f};
        e2.shape             = LevelCollisionShape::None;
        e2.soundPath         = "sounds/ambient_hum.ogg";
        e2.soundFalloffRadius = 20.0f;
        e2.soundVolume        = 0.5f;
        e2.soundLoop          = true;
        e2.soundAutoPlay      = true;
        pack.entities.push_back(e2);

        return pack;
    }

    std::filesystem::path m_tmpDir =
        std::filesystem::temp_directory_path() / "daedalus_dlevel_test";

    void SetUp() override    { std::filesystem::create_directories(m_tmpDir); }
    void TearDown() override { std::filesystem::remove_all(m_tmpDir); }
};

// ─── v3 entity round-trip ─────────────────────────────────────────────────────

TEST_F(DlevelIOTest, EntityV3RoundTrip)
{
    const LevelPackData original = makeSamplePack();
    const auto savePath = m_tmpDir / "test.dlevel";

    const auto saveResult = saveDlevel(original, savePath);
    ASSERT_TRUE(saveResult.has_value()) << "saveDlevel failed";
    ASSERT_TRUE(std::filesystem::exists(savePath));

    const auto loadResult = loadDlevel(savePath);
    ASSERT_TRUE(loadResult.has_value()) << "loadDlevel failed";

    const LevelPackData& loaded = *loadResult;
    ASSERT_EQ(loaded.entities.size(), original.entities.size());

    // ── Entity 0: physics + script + audio ────────────────────────────────────
    {
        const LevelEntity& orig = original.entities[0];
        const LevelEntity& back = loaded.entities[0];

        EXPECT_EQ(back.name,     orig.name);
        EXPECT_FLOAT_EQ(back.position.x, orig.position.x);
        EXPECT_FLOAT_EQ(back.position.y, orig.position.y);
        EXPECT_FLOAT_EQ(back.position.z, orig.position.z);
        EXPECT_FLOAT_EQ(back.yaw,        orig.yaw);
        EXPECT_EQ(back.sectorId, orig.sectorId);
        EXPECT_EQ(back.shape,    orig.shape);
        EXPECT_EQ(back.dynamic,  orig.dynamic);
        EXPECT_FLOAT_EQ(back.mass,          orig.mass);
        EXPECT_FLOAT_EQ(back.halfExtents.x, orig.halfExtents.x);
        EXPECT_FLOAT_EQ(back.halfExtents.y, orig.halfExtents.y);
        EXPECT_FLOAT_EQ(back.halfExtents.z, orig.halfExtents.z);
        // Script
        EXPECT_EQ(back.scriptPath,  orig.scriptPath);
        EXPECT_EQ(back.exposedVars, orig.exposedVars);
        // Audio
        EXPECT_EQ(back.soundPath, orig.soundPath);
        EXPECT_FLOAT_EQ(back.soundFalloffRadius, orig.soundFalloffRadius);
        EXPECT_FLOAT_EQ(back.soundVolume,        orig.soundVolume);
        EXPECT_EQ(back.soundLoop,     orig.soundLoop);
        EXPECT_EQ(back.soundAutoPlay, orig.soundAutoPlay);
    }

    // ── Entity 1: script-only ─────────────────────────────────────────────────
    {
        const LevelEntity& orig = original.entities[1];
        const LevelEntity& back = loaded.entities[1];
        EXPECT_EQ(back.name,        orig.name);
        EXPECT_EQ(back.shape,       orig.shape);
        EXPECT_EQ(back.scriptPath,  orig.scriptPath);
        EXPECT_EQ(back.exposedVars, orig.exposedVars);
        EXPECT_TRUE(back.soundPath.empty());
    }

    // ── Entity 2: audio-only ──────────────────────────────────────────────────
    {
        const LevelEntity& orig = original.entities[2];
        const LevelEntity& back = loaded.entities[2];
        EXPECT_EQ(back.name,  orig.name);
        EXPECT_EQ(back.shape, orig.shape);
        EXPECT_TRUE(back.scriptPath.empty());
        EXPECT_EQ(back.soundPath, orig.soundPath);
        EXPECT_FLOAT_EQ(back.soundFalloffRadius, orig.soundFalloffRadius);
        EXPECT_FLOAT_EQ(back.soundVolume,        orig.soundVolume);
        EXPECT_EQ(back.soundLoop,     orig.soundLoop);
        EXPECT_EQ(back.soundAutoPlay, orig.soundAutoPlay);
    }
}

// ─── Error cases ──────────────────────────────────────────────────────────────

TEST_F(DlevelIOTest, LoadMissingFile)
{
    const auto result = loadDlevel(m_tmpDir / "nonexistent.dlevel");
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), DlevelError::FileNotFound);
}

TEST_F(DlevelIOTest, LoadCorruptData)
{
    const auto badPath = m_tmpDir / "corrupt.dlevel";
    std::ofstream ofs(badPath, std::ios::binary);
    ofs.write("JUNK", 4);
    ofs.close();

    const auto result = loadDlevel(badPath);
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), DlevelError::ParseError);
}
