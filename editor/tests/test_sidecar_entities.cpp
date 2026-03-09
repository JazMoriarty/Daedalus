// test_sidecar_entities.cpp
// Roundtrip tests for entity serialization in emap_sidecar.

#include "daedalus/editor/edit_map_document.h"
#include "daedalus/editor/entity_def.h"
#include "daedalus/world/map_data.h"
#include "daedalus/editor/prefab_def.h"
#include "document/emap_sidecar.h"

#include <gtest/gtest.h>
#include <filesystem>

using namespace daedalus::editor;

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// RAII temporary file that is deleted on scope exit.
struct TempFile
{
    std::filesystem::path path;
    explicit TempFile(const char* name)
        : path(std::filesystem::temp_directory_path() / name) {}
    ~TempFile() { std::filesystem::remove(path); }
};

// ─── Empty entity array ───────────────────────────────────────────────────────

TEST(EmapEntityRoundtrip, EmptyEntitiesRoundtrip)
{
    TempFile tmp("test_emap_empty_entities.emap");

    EditMapDocument docOut;
    ASSERT_EQ(docOut.entities().size(), 0u);

    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    EXPECT_EQ(docIn.entities().size(), 0u);
}

// ─── Single BillboardCutout entity ───────────────────────────────────────────

TEST(EmapEntityRoundtrip, BillboardCutoutRoundtrip)
{
    TempFile tmp("test_emap_billboard.emap");

    EditMapDocument docOut;
    {
        EntityDef ed;
        ed.visualType = EntityVisualType::BillboardCutout;
        ed.position   = {3.5f, 1.0f, -2.0f};
        ed.yaw        = 0.785f;  // 45 degrees
        ed.scale      = {2.0f, 2.0f, 2.0f};
        ed.assetPath  = "sprites/barrel.png";
        ed.tint       = {1.0f, 0.8f, 0.5f, 1.0f};
        ed.layerIndex = 1u;
        docOut.entities().push_back(ed);
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_EQ(docIn.entities().size(), 1u);
    const EntityDef& e = docIn.entities()[0];

    EXPECT_EQ(e.visualType, EntityVisualType::BillboardCutout);
    EXPECT_FLOAT_EQ(e.position.x,  3.5f);
    EXPECT_FLOAT_EQ(e.position.y,  1.0f);
    EXPECT_FLOAT_EQ(e.position.z, -2.0f);
    EXPECT_FLOAT_EQ(e.yaw,         0.785f);
    EXPECT_FLOAT_EQ(e.scale.x,     2.0f);
    EXPECT_EQ      (e.assetPath,  "sprites/barrel.png");
    EXPECT_FLOAT_EQ(e.tint.r,      1.0f);
    EXPECT_FLOAT_EQ(e.tint.g,      0.8f);
    EXPECT_FLOAT_EQ(e.tint.a,      1.0f);
    EXPECT_EQ      (e.layerIndex,  1u);
}

// ─── AnimatedBillboard with AnimSettings ─────────────────────────────────────

TEST(EmapEntityRoundtrip, AnimatedBillboardRoundtrip)
{
    TempFile tmp("test_emap_anim.emap");

    EditMapDocument docOut;
    {
        EntityDef ed;
        ed.visualType        = EntityVisualType::AnimatedBillboard;
        ed.assetPath         = "sprites/explosion.png";
        ed.anim.frameCount   = 16u;
        ed.anim.cols         = 4u;
        ed.anim.rows         = 4u;
        ed.anim.frameRate    = 24.0f;
        docOut.entities().push_back(ed);
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_EQ(docIn.entities().size(), 1u);
    const EntityDef& e = docIn.entities()[0];

    EXPECT_EQ(e.visualType,      EntityVisualType::AnimatedBillboard);
    EXPECT_EQ(e.anim.frameCount, 16u);
    EXPECT_EQ(e.anim.cols,        4u);
    EXPECT_EQ(e.anim.rows,        4u);
    EXPECT_FLOAT_EQ(e.anim.frameRate, 24.0f);
}

// ─── Decal with DecalMaterialParams ──────────────────────────────────────────

TEST(EmapEntityRoundtrip, DecalRoundtrip)
{
    TempFile tmp("test_emap_decal.emap");

    EditMapDocument docOut;
    {
        EntityDef ed;
        ed.visualType          = EntityVisualType::Decal;
        ed.assetPath           = "decals/blood.png";
        ed.decalMat.normalPath = "decals/blood_n.png";
        ed.decalMat.roughness  = 0.75f;
        ed.decalMat.metalness  = 0.1f;
        ed.decalMat.opacity    = 0.9f;
        docOut.entities().push_back(ed);
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_EQ(docIn.entities().size(), 1u);
    const EntityDef& e = docIn.entities()[0];

    EXPECT_EQ(e.visualType,          EntityVisualType::Decal);
    EXPECT_EQ(e.decalMat.normalPath, "decals/blood_n.png");
    EXPECT_FLOAT_EQ(e.decalMat.roughness, 0.75f);
    EXPECT_FLOAT_EQ(e.decalMat.metalness, 0.1f);
    EXPECT_FLOAT_EQ(e.decalMat.opacity,   0.9f);
}

// ─── ParticleEmitter with all params ─────────────────────────────────────────

TEST(EmapEntityRoundtrip, ParticleEmitterRoundtrip)
{
    TempFile tmp("test_emap_particle.emap");

    EditMapDocument docOut;
    {
        EntityDef ed;
        ed.visualType               = EntityVisualType::ParticleEmitter;
        ed.assetPath                = "particles/spark.png";
        ed.particle.emissionRate    = 50.0f;
        ed.particle.emitDir         = {0.0f, 1.0f, 0.0f};
        ed.particle.coneHalfAngle   = 0.4f;
        ed.particle.speedMin        = 2.0f;
        ed.particle.speedMax        = 5.0f;
        ed.particle.lifetimeMin     = 0.5f;
        ed.particle.lifetimeMax     = 1.5f;
        ed.particle.colorStart      = {1.0f, 0.8f, 0.3f, 1.0f};
        ed.particle.colorEnd        = {1.0f, 0.2f, 0.0f, 0.0f};
        ed.particle.sizeStart       = 0.2f;
        ed.particle.sizeEnd         = 0.02f;
        ed.particle.drag            = 0.3f;
        ed.particle.gravity         = {0.0f, -5.0f, 0.0f};
        docOut.entities().push_back(ed);
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_EQ(docIn.entities().size(), 1u);
    const EntityDef& e = docIn.entities()[0];

    EXPECT_EQ(e.visualType, EntityVisualType::ParticleEmitter);
    EXPECT_FLOAT_EQ(e.particle.emissionRate,   50.0f);
    EXPECT_FLOAT_EQ(e.particle.emitDir.y,       1.0f);
    EXPECT_FLOAT_EQ(e.particle.coneHalfAngle,   0.4f);
    EXPECT_FLOAT_EQ(e.particle.speedMin,         2.0f);
    EXPECT_FLOAT_EQ(e.particle.speedMax,         5.0f);
    EXPECT_FLOAT_EQ(e.particle.lifetimeMin,      0.5f);
    EXPECT_FLOAT_EQ(e.particle.lifetimeMax,      1.5f);
    EXPECT_FLOAT_EQ(e.particle.colorStart.r,     1.0f);
    EXPECT_FLOAT_EQ(e.particle.colorEnd.a,       0.0f);
    EXPECT_FLOAT_EQ(e.particle.sizeStart,        0.2f);
    EXPECT_FLOAT_EQ(e.particle.sizeEnd,          0.02f);
    EXPECT_FLOAT_EQ(e.particle.drag,             0.3f);
    EXPECT_FLOAT_EQ(e.particle.gravity.y,       -5.0f);
}

// ─── Multiple entity types survive roundtrip ─────────────────────────────────

TEST(EmapEntityRoundtrip, MultipleEntitiesPreserveOrder)
{
    TempFile tmp("test_emap_multi_entity.emap");

    EditMapDocument docOut;
    {
        EntityDef a; a.visualType = EntityVisualType::VoxelObject;   a.assetPath = "models/crate.vox";
        EntityDef b; b.visualType = EntityVisualType::StaticMesh;    b.assetPath = "models/pillar.gltf";
        EntityDef c; c.visualType = EntityVisualType::BillboardBlended; c.assetPath = "sprites/smoke.png";
        docOut.entities().push_back(a);
        docOut.entities().push_back(b);
        docOut.entities().push_back(c);
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_EQ(docIn.entities().size(), 3u);
    EXPECT_EQ(docIn.entities()[0].visualType, EntityVisualType::VoxelObject);
    EXPECT_EQ(docIn.entities()[0].assetPath,  "models/crate.vox");
    EXPECT_EQ(docIn.entities()[1].visualType, EntityVisualType::StaticMesh);
    EXPECT_EQ(docIn.entities()[1].assetPath,  "models/pillar.gltf");
    EXPECT_EQ(docIn.entities()[2].visualType, EntityVisualType::BillboardBlended);
    EXPECT_EQ(docIn.entities()[2].assetPath,  "sprites/smoke.png");
}

// ─── Full transform + identity fields roundtrip ──────────────────────────────

TEST(EmapEntityRoundtrip, FullTransformRoundtrip)
{
    TempFile tmp("test_emap_full_transform.emap");

    EditMapDocument docOut;
    {
        EntityDef ed;
        ed.visualType    = EntityVisualType::BillboardCutout;
        ed.assetPath     = "sprites/orc.png";
        ed.pitch         = 0.4f;
        ed.roll          = -0.2f;
        ed.entityName    = "orc_guard_01";
        ed.alignmentMode = EntityAlignment::Wall;
        docOut.entities().push_back(ed);
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_EQ(docIn.entities().size(), 1u);
    const EntityDef& e = docIn.entities()[0];

    EXPECT_FLOAT_EQ(e.pitch,         0.4f);
    EXPECT_FLOAT_EQ(e.roll,         -0.2f);
    EXPECT_EQ      (e.entityName,    std::string("orc_guard_01"));
    EXPECT_EQ      (e.alignmentMode, EntityAlignment::Wall);
}

// ─── Wall backMaterialId roundtrip (via prefab sector, which is emap-serialized)

TEST(EmapEntityRoundtrip, WallBackMaterialRoundtrip)
{
    TempFile tmp("test_emap_wall_backmat.emap");

    EditMapDocument docOut;
    {
        daedalus::world::Sector s;
        daedalus::world::Wall w;
        w.p0                = {0.0f, 0.0f};
        w.backMaterialId.hi = 0xDEADBEEF12345678ULL;
        w.backMaterialId.lo = 0xCAFEBABEABCDEF01ULL;
        s.walls.push_back(w);
        s.walls.push_back(daedalus::world::Wall{{4.0f, 0.0f}});
        s.walls.push_back(daedalus::world::Wall{{4.0f, 4.0f}});
        s.walls.push_back(daedalus::world::Wall{{0.0f, 4.0f}});
        PrefabDef pf;
        pf.name = "wall_backmat_prefab";
        pf.sectors.push_back(std::move(s));
        docOut.prefabs().push_back(std::move(pf));
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_GE(docIn.prefabs().size(), 1u);
    ASSERT_GE(docIn.prefabs()[0].sectors.size(), 1u);
    const auto& w = docIn.prefabs()[0].sectors[0].walls[0];
    EXPECT_EQ(w.backMaterialId.hi, 0xDEADBEEF12345678ULL);
    EXPECT_EQ(w.backMaterialId.lo, 0xCAFEBABEABCDEF01ULL);
}

// ─── Physics/Script/Audio stub field roundtrip

TEST(EmapEntityRoundtrip, StubPropsRoundtrip)
{
    TempFile tmp("test_emap_stubs.emap");

    EditMapDocument docOut;
    {
        EntityDef ed;
        ed.visualType             = EntityVisualType::StaticMesh;
        ed.assetPath              = "models/barrel.gltf";
        ed.physics.shape          = CollisionShape::Capsule;
        ed.physics.isStatic       = false;
        ed.physics.mass           = 75.0f;
        ed.script.scriptPath      = "scripts/patrol.lua";
        ed.audio.soundPath        = "sounds/ambient_hum.ogg";
        ed.audio.falloffRadius    = 25.0f;
        ed.audio.loop             = true;
        docOut.entities().push_back(ed);
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    ASSERT_EQ(docIn.entities().size(), 1u);
    const EntityDef& e = docIn.entities()[0];

    EXPECT_EQ  (e.physics.shape,       CollisionShape::Capsule);
    EXPECT_FALSE(e.physics.isStatic);
    EXPECT_FLOAT_EQ(e.physics.mass,    75.0f);
    EXPECT_EQ  (e.script.scriptPath,   std::string("scripts/patrol.lua"));
    EXPECT_EQ  (e.audio.soundPath,     std::string("sounds/ambient_hum.ogg"));
    EXPECT_FLOAT_EQ(e.audio.falloffRadius, 25.0f);
    EXPECT_TRUE(e.audio.loop);
}

// ─── Entities survive alongside existing sidecar data ───────────────────────

TEST(EmapEntityRoundtrip, EntitiesCoexistWithLightsAndLayers)
{
    TempFile tmp("test_emap_coexist.emap");

    EditMapDocument docOut;
    {
        EntityDef ed; ed.assetPath = "props/torch.png";
        docOut.entities().push_back(ed);
    }
    {
        LightDef ld; ld.position = {1.0f, 2.0f, 3.0f};
        docOut.lights().push_back(ld);
    }
    ASSERT_TRUE(saveEmap(docOut, tmp.path).has_value());

    EditMapDocument docIn;
    ASSERT_TRUE(loadEmap(docIn, tmp.path).has_value());

    EXPECT_EQ(docIn.entities().size(), 1u);
    EXPECT_EQ(docIn.lights().size(),   1u);
    EXPECT_EQ(docIn.entities()[0].assetPath, "props/torch.png");
    EXPECT_FLOAT_EQ(docIn.lights()[0].position.x, 1.0f);
}
