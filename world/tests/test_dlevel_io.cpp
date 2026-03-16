// test_dlevel_io.cpp
// Unit tests for .dlevel binary serialisation round-trip.
// Covers v3 entity fields (script + audio), v4 visual descriptor fields,
// and v5 extended particle fields.

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

// ─── v4 visual descriptor round-trip ─────────────────────────────────────────
// Exercises every LevelEntityVisualType and verifies all visual fields survive
// a save/load cycle through the v4 binary format.

TEST_F(DlevelIOTest, EntityV4VisualRoundTrip)
{
    LevelPackData original;

    // ── Entity 0: BillboardCutout ─────────────────────────────────────────────
    {
        LevelEntity e;
        e.name        = "BillboardCutoutEnt";
        e.position    = {1.0f, 0.0f, 0.0f};
        e.visualType  = LevelEntityVisualType::BillboardCutout;
        e.assetPath   = "textures/crate.png";
        e.tint        = {0.9f, 0.8f, 0.7f, 1.0f};
        e.visualScale = {1.5f, 2.0f, 1.0f};
        e.visualPitch = 0.1f;
        e.visualRoll  = 0.05f;
        original.entities.push_back(e);
    }

    // ── Entity 1: BillboardBlended ────────────────────────────────────────────
    {
        LevelEntity e;
        e.name        = "BillboardBlendedEnt";
        e.position    = {2.0f, 0.0f, 0.0f};
        e.visualType  = LevelEntityVisualType::BillboardBlended;
        e.assetPath   = "textures/flame.png";
        e.tint        = {1.0f, 0.5f, 0.2f, 0.8f};
        e.visualScale = {0.5f, 0.5f, 1.0f};
        original.entities.push_back(e);
    }

    // ── Entity 2: AnimatedBillboard ───────────────────────────────────────────
    {
        LevelEntity e;
        e.name           = "AnimatedBillboardEnt";
        e.position       = {3.0f, 0.0f, 0.0f};
        e.visualType     = LevelEntityVisualType::AnimatedBillboard;
        e.assetPath      = "textures/explosion_sheet.png";
        e.tint           = {1.0f, 1.0f, 1.0f, 1.0f};
        e.visualScale    = {1.0f, 1.0f, 1.0f};
        e.animFrameCount = 8u;
        e.animCols       = 8u;
        e.animRows       = 2u;
        e.animFrameRate  = 12.0f;
        original.entities.push_back(e);
    }

    // ── Entity 3: RotatedSpriteSet ────────────────────────────────────────────
    {
        LevelEntity e;
        e.name                  = "RotatedSpriteSetEnt";
        e.position              = {4.0f, 0.0f, 0.0f};
        e.visualType            = LevelEntityVisualType::RotatedSpriteSet;
        e.assetPath             = "textures/soldier_sheet.png";
        e.tint                  = {1.0f, 0.9f, 0.9f, 1.0f};
        e.visualScale           = {1.0f, 2.0f, 1.0f};
        e.animFrameCount        = 16u;
        e.animCols              = 16u;
        e.animRows              = 8u;
        e.animFrameRate         = 10.0f;
        e.rotatedSpriteDirCount = 16u;
        original.entities.push_back(e);
    }

    // ── Entity 4: StaticMesh ──────────────────────────────────────────────────
    {
        LevelEntity e;
        e.name        = "StaticMeshEnt";
        e.position    = {5.0f, 0.0f, 0.0f};
        e.visualType  = LevelEntityVisualType::StaticMesh;
        e.assetPath   = "meshes/barrel.gltf";
        e.tint        = {1.0f, 1.0f, 1.0f, 1.0f};
        e.visualScale = {2.0f, 2.0f, 2.0f};
        e.visualPitch = 0.0f;
        e.visualRoll  = 0.0f;
        original.entities.push_back(e);
    }

    // ── Entity 5: VoxelObject ─────────────────────────────────────────────────
    {
        LevelEntity e;
        e.name        = "VoxelObjectEnt";
        e.position    = {6.0f, 0.0f, 0.0f};
        e.visualType  = LevelEntityVisualType::VoxelObject;
        e.assetPath   = "voxels/tree.vox";
        e.tint        = {0.6f, 0.8f, 0.5f, 1.0f};
        e.visualScale = {0.25f, 0.25f, 0.25f};
        original.entities.push_back(e);
    }

    // ── Entity 6: Decal ───────────────────────────────────────────────────────
    {
        LevelEntity e;
        e.name            = "DecalEnt";
        e.position        = {7.0f, 0.0f, 0.0f};
        e.visualType      = LevelEntityVisualType::Decal;
        e.assetPath       = "textures/blood_splat.png";
        e.tint            = {1.0f, 0.2f, 0.1f, 0.9f};
        e.visualScale     = {1.2f, 1.2f, 0.05f};
        e.visualPitch     = 0.3f;
        e.visualRoll      = -0.1f;
        e.decalNormalPath = "textures/blood_splat_n.png";
        e.decalRoughness  = 0.7f;
        e.decalMetalness  = 0.1f;
        e.decalOpacity    = 0.85f;
        original.entities.push_back(e);
    }

    // ── Entity 7: ParticleEmitter ─────────────────────────────────
    {
        LevelEntity e;
        e.name                     = "ParticleEmitterEnt";
        e.position                 = {8.0f, 0.0f, 0.0f};
        e.visualType               = LevelEntityVisualType::ParticleEmitter;
        e.assetPath                = "textures/spark.png";
        e.tint                     = {1.0f, 0.8f, 0.4f, 1.0f};
        e.particleEmissionRate     = 50.0f;
        e.particleEmitDir          = {0.1f, 1.0f, 0.0f};
        e.particleConeHalfAngle    = 0.3f;
        e.particleSpeedMin         = 2.0f;
        e.particleSpeedMax         = 5.0f;
        e.particleLifetimeMin      = 0.5f;
        e.particleLifetimeMax      = 2.0f;
        e.particleColorStart       = {1.0f, 0.9f, 0.5f, 1.0f};
        e.particleColorEnd         = {0.8f, 0.2f, 0.0f, 0.0f};
        e.particleSizeStart        = 0.2f;
        e.particleSizeEnd          = 0.02f;
        e.particleDrag             = 0.5f;
        e.particleGravity          = {0.0f, -4.9f, 0.0f};
        // v5 extended particle fields
        e.particleEmissiveScale    = 4.5f;
        e.particleTurbulenceScale  = 1.2f;
        e.particleVelocityStretch  = 0.15f;
        e.particleSoftRange        = 0.4f;
        e.particleEmissiveStart    = 2.0f;
        e.particleEmissiveEnd      = 0.5f;
        e.particleAtlasCols        = 4u;
        e.particleAtlasRows        = 2u;
        e.particleAtlasFrameRate   = 12.0f;
        e.particleEmitsLight       = true;
        e.particleShadowDensity    = 0.6f;
        original.entities.push_back(e);
    }

    // ── Entity 8: None (visual-less — physics/script-only) ────────────────────
    {
        LevelEntity e;
        e.name        = "NoVisualEnt";
        e.position    = {9.0f, 0.0f, 0.0f};
        e.visualType  = LevelEntityVisualType::None;
        e.shape       = LevelCollisionShape::Box;
        e.halfExtents = {0.5f, 0.5f, 0.5f};
        original.entities.push_back(e);
    }

    // ─── Save + Load ──────────────────────────────────────────────────────────
    const auto savePath = m_tmpDir / "visual.dlevel";
    ASSERT_TRUE(saveDlevel(original, savePath).has_value()) << "saveDlevel failed";

    const auto loadResult = loadDlevel(savePath);
    ASSERT_TRUE(loadResult.has_value()) << "loadDlevel failed";
    const LevelPackData& loaded = *loadResult;
    ASSERT_EQ(loaded.entities.size(), original.entities.size());

    auto eqV3 = [](const glm::vec3& a, const glm::vec3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    auto eqV4 = [](const glm::vec4& a, const glm::vec4& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    };

    // ── 0: BillboardCutout ────────────────────────────────────────────────────
    {
        const auto& o = original.entities[0];
        const auto& b = loaded.entities[0];
        EXPECT_EQ(b.visualType, o.visualType);
        EXPECT_EQ(b.assetPath,  o.assetPath);
        EXPECT_TRUE(eqV4(b.tint,        o.tint));
        EXPECT_TRUE(eqV3(b.visualScale, o.visualScale));
        EXPECT_FLOAT_EQ(b.visualPitch,  o.visualPitch);
        EXPECT_FLOAT_EQ(b.visualRoll,   o.visualRoll);
    }

    // ── 1: BillboardBlended ───────────────────────────────────────────────────
    {
        const auto& o = original.entities[1];
        const auto& b = loaded.entities[1];
        EXPECT_EQ(b.visualType, o.visualType);
        EXPECT_EQ(b.assetPath,  o.assetPath);
        EXPECT_TRUE(eqV4(b.tint,        o.tint));
        EXPECT_TRUE(eqV3(b.visualScale, o.visualScale));
    }

    // ── 2: AnimatedBillboard ──────────────────────────────────────────────────
    {
        const auto& o = original.entities[2];
        const auto& b = loaded.entities[2];
        EXPECT_EQ(b.visualType,     o.visualType);
        EXPECT_EQ(b.assetPath,      o.assetPath);
        EXPECT_EQ(b.animFrameCount, o.animFrameCount);
        EXPECT_EQ(b.animCols,       o.animCols);
        EXPECT_EQ(b.animRows,       o.animRows);
        EXPECT_FLOAT_EQ(b.animFrameRate, o.animFrameRate);
    }

    // ── 3: RotatedSpriteSet ───────────────────────────────────────────────────
    {
        const auto& o = original.entities[3];
        const auto& b = loaded.entities[3];
        EXPECT_EQ(b.visualType,            o.visualType);
        EXPECT_EQ(b.assetPath,             o.assetPath);
        EXPECT_EQ(b.animFrameCount,        o.animFrameCount);
        EXPECT_EQ(b.animCols,              o.animCols);
        EXPECT_EQ(b.animRows,              o.animRows);
        EXPECT_FLOAT_EQ(b.animFrameRate,   o.animFrameRate);
        EXPECT_EQ(b.rotatedSpriteDirCount, o.rotatedSpriteDirCount);
    }

    // ── 4: StaticMesh ─────────────────────────────────────────────────────────
    {
        const auto& o = original.entities[4];
        const auto& b = loaded.entities[4];
        EXPECT_EQ(b.visualType, o.visualType);
        EXPECT_EQ(b.assetPath,  o.assetPath);
        EXPECT_TRUE(eqV3(b.visualScale, o.visualScale));
    }

    // ── 5: VoxelObject ────────────────────────────────────────────────────────
    {
        const auto& o = original.entities[5];
        const auto& b = loaded.entities[5];
        EXPECT_EQ(b.visualType, o.visualType);
        EXPECT_EQ(b.assetPath,  o.assetPath);
        EXPECT_TRUE(eqV4(b.tint,        o.tint));
        EXPECT_TRUE(eqV3(b.visualScale, o.visualScale));
    }

    // ── 6: Decal ──────────────────────────────────────────────────────────────
    {
        const auto& o = original.entities[6];
        const auto& b = loaded.entities[6];
        EXPECT_EQ(b.visualType,     o.visualType);
        EXPECT_EQ(b.assetPath,      o.assetPath);
        EXPECT_TRUE(eqV4(b.tint,        o.tint));
        EXPECT_TRUE(eqV3(b.visualScale, o.visualScale));
        EXPECT_FLOAT_EQ(b.visualPitch,    o.visualPitch);
        EXPECT_FLOAT_EQ(b.visualRoll,     o.visualRoll);
        EXPECT_EQ(b.decalNormalPath,      o.decalNormalPath);
        EXPECT_FLOAT_EQ(b.decalRoughness, o.decalRoughness);
        EXPECT_FLOAT_EQ(b.decalMetalness, o.decalMetalness);
        EXPECT_FLOAT_EQ(b.decalOpacity,   o.decalOpacity);
    }

    // ── 7: ParticleEmitter ────────────────────────────────────
    {
        const auto& o = original.entities[7];
        const auto& b = loaded.entities[7];
        EXPECT_EQ(b.visualType, o.visualType);
        EXPECT_EQ(b.assetPath,  o.assetPath);
        EXPECT_TRUE(eqV4(b.tint, o.tint));
        EXPECT_FLOAT_EQ(b.particleEmissionRate,  o.particleEmissionRate);
        EXPECT_TRUE(eqV3(b.particleEmitDir,      o.particleEmitDir));
        EXPECT_FLOAT_EQ(b.particleConeHalfAngle, o.particleConeHalfAngle);
        EXPECT_FLOAT_EQ(b.particleSpeedMin,      o.particleSpeedMin);
        EXPECT_FLOAT_EQ(b.particleSpeedMax,      o.particleSpeedMax);
        EXPECT_FLOAT_EQ(b.particleLifetimeMin,   o.particleLifetimeMin);
        EXPECT_FLOAT_EQ(b.particleLifetimeMax,   o.particleLifetimeMax);
        EXPECT_TRUE(eqV4(b.particleColorStart, o.particleColorStart));
        EXPECT_TRUE(eqV4(b.particleColorEnd,   o.particleColorEnd));
        EXPECT_FLOAT_EQ(b.particleSizeStart,     o.particleSizeStart);
        EXPECT_FLOAT_EQ(b.particleSizeEnd,       o.particleSizeEnd);
        EXPECT_FLOAT_EQ(b.particleDrag,          o.particleDrag);
        EXPECT_TRUE(eqV3(b.particleGravity, o.particleGravity));
        // v5 extended particle fields
        EXPECT_FLOAT_EQ(b.particleEmissiveScale,   o.particleEmissiveScale);
        EXPECT_FLOAT_EQ(b.particleTurbulenceScale, o.particleTurbulenceScale);
        EXPECT_FLOAT_EQ(b.particleVelocityStretch, o.particleVelocityStretch);
        EXPECT_FLOAT_EQ(b.particleSoftRange,       o.particleSoftRange);
        EXPECT_FLOAT_EQ(b.particleEmissiveStart,   o.particleEmissiveStart);
        EXPECT_FLOAT_EQ(b.particleEmissiveEnd,     o.particleEmissiveEnd);
        EXPECT_EQ(b.particleAtlasCols,             o.particleAtlasCols);
        EXPECT_EQ(b.particleAtlasRows,             o.particleAtlasRows);
        EXPECT_FLOAT_EQ(b.particleAtlasFrameRate,  o.particleAtlasFrameRate);
        EXPECT_EQ(b.particleEmitsLight,            o.particleEmitsLight);
        EXPECT_FLOAT_EQ(b.particleShadowDensity,   o.particleShadowDensity);
    }

    // ── 8: None (visual-less) ─────────────────────────────────────────────────
    {
        const auto& o = original.entities[8];
        const auto& b = loaded.entities[8];
        EXPECT_EQ(b.name,       o.name);
        EXPECT_EQ(b.visualType, LevelEntityVisualType::None);
        EXPECT_EQ(b.shape,      o.shape);
        EXPECT_TRUE(b.assetPath.empty());
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
