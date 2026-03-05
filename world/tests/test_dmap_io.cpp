// test_dmap_io.cpp
// Unit tests for .dmap binary and .dmap.json serialisation roundtrips.

#include "daedalus/world/dmap_io.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace daedalus;
using namespace daedalus::world;

// ─── Test fixture ─────────────────────────────────────────────────────────────

class DmapIOTest : public ::testing::Test
{
protected:
    // Build a minimal 2-sector map with known values for roundtrip verification.
    static WorldMapData makeSampleMap()
    {
        WorldMapData map;
        map.name   = "TestMap";
        map.author = "UnitTest";
        map.globalAmbientColor     = {0.1f, 0.2f, 0.3f};
        map.globalAmbientIntensity = 0.7f;

        // Sector 0: 4-wall box with one portal wall into sector 1.
        Sector s0;
        s0.floorHeight = 0.0f;
        s0.ceilHeight  = 3.5f;
        s0.ambientColor     = {0.05f, 0.05f, 0.1f};
        s0.ambientIntensity = 1.0f;

        // Walls: CCW from above.
        Wall w0; w0.p0 = {-5.0f,  5.0f}; s0.walls.push_back(w0);
        Wall w1; w1.p0 = { 5.0f,  5.0f}; s0.walls.push_back(w1);
        Wall w2; w2.p0 = { 5.0f, -5.0f}; w2.portalSectorId = 1u;
                 w2.uvScale = {2.0f, 2.0f}; s0.walls.push_back(w2);
        Wall w3; w3.p0 = {-5.0f, -5.0f}; s0.walls.push_back(w3);

        // Sector 1: smaller room adjacent to sector 0.
        Sector s1;
        s1.floorHeight = 0.0f;
        s1.ceilHeight  = 2.8f;

        Wall v0; v0.p0 = { 5.0f, -5.0f}; s1.walls.push_back(v0);
        Wall v1; v1.p0 = {13.0f, -5.0f}; s1.walls.push_back(v1);
        Wall v2; v2.p0 = {13.0f,  5.0f}; s1.walls.push_back(v2);
        Wall v3; v3.p0 = { 5.0f,  5.0f}; v3.portalSectorId = 0u; s1.walls.push_back(v3);

        map.sectors.push_back(std::move(s0));
        map.sectors.push_back(std::move(s1));
        return map;
    }

    // Compare two WorldMapData for field equality (floating-point exact).
    static void assertMapEqual(const WorldMapData& a, const WorldMapData& b)
    {
        ASSERT_EQ(a.name,   b.name);
        ASSERT_EQ(a.author, b.author);
        ASSERT_FLOAT_EQ(a.globalAmbientColor.r, b.globalAmbientColor.r);
        ASSERT_FLOAT_EQ(a.globalAmbientColor.g, b.globalAmbientColor.g);
        ASSERT_FLOAT_EQ(a.globalAmbientColor.b, b.globalAmbientColor.b);
        ASSERT_FLOAT_EQ(a.globalAmbientIntensity, b.globalAmbientIntensity);

        ASSERT_EQ(a.sectors.size(), b.sectors.size());
        for (std::size_t si = 0; si < a.sectors.size(); ++si)
        {
            const Sector& sa = a.sectors[si];
            const Sector& sb = b.sectors[si];

            ASSERT_FLOAT_EQ(sa.floorHeight, sb.floorHeight) << "sector " << si;
            ASSERT_FLOAT_EQ(sa.ceilHeight,  sb.ceilHeight)  << "sector " << si;
            ASSERT_FLOAT_EQ(sa.ambientIntensity, sb.ambientIntensity) << "sector " << si;
            ASSERT_EQ(static_cast<u32>(sa.flags), static_cast<u32>(sb.flags)) << "sector " << si;
            ASSERT_EQ(sa.walls.size(), sb.walls.size()) << "sector " << si;

            for (std::size_t wi = 0; wi < sa.walls.size(); ++wi)
            {
                const Wall& wa = sa.walls[wi];
                const Wall& wb = sb.walls[wi];

                ASSERT_FLOAT_EQ(wa.p0.x, wb.p0.x) << "sector " << si << " wall " << wi;
                ASSERT_FLOAT_EQ(wa.p0.y, wb.p0.y) << "sector " << si << " wall " << wi;
                ASSERT_EQ(wa.portalSectorId, wb.portalSectorId) << "sector " << si << " wall " << wi;
                ASSERT_FLOAT_EQ(wa.uvScale.x, wb.uvScale.x) << "sector " << si << " wall " << wi;
                ASSERT_FLOAT_EQ(wa.uvScale.y, wb.uvScale.y) << "sector " << si << " wall " << wi;
            }
        }
    }

    // Temp dir for test output.
    std::filesystem::path m_tmpDir = std::filesystem::temp_directory_path() / "daedalus_test";

    void SetUp() override
    {
        std::filesystem::create_directories(m_tmpDir);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(m_tmpDir);
    }
};

// ─── Binary roundtrip ─────────────────────────────────────────────────────────

TEST_F(DmapIOTest, BinaryRoundtrip)
{
    const WorldMapData original = makeSampleMap();
    const auto savePath = m_tmpDir / "test.dmap";

    auto saveResult = saveDmap(original, savePath);
    ASSERT_TRUE(saveResult.has_value()) << "saveDmap failed";
    ASSERT_TRUE(std::filesystem::exists(savePath));

    auto loadResult = loadDmap(savePath);
    ASSERT_TRUE(loadResult.has_value()) << "loadDmap failed";

    assertMapEqual(original, *loadResult);
}

TEST_F(DmapIOTest, BinaryLoadMissingFile)
{
    auto result = loadDmap(m_tmpDir / "nonexistent.dmap");
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), DmapError::FileNotFound);
}

TEST_F(DmapIOTest, BinaryLoadCorruptData)
{
    const auto badPath = m_tmpDir / "corrupt.dmap";
    // Write garbage bytes.
    std::ofstream ofs(badPath, std::ios::binary);
    ofs.write("JUNK", 4);
    ofs.close();

    auto result = loadDmap(badPath);
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), DmapError::ParseError);
}

// ─── JSON roundtrip ───────────────────────────────────────────────────────────

TEST_F(DmapIOTest, JsonRoundtrip)
{
    const WorldMapData original = makeSampleMap();
    const auto savePath = m_tmpDir / "test.dmap.json";

    auto saveResult = saveDmapJson(original, savePath);
    ASSERT_TRUE(saveResult.has_value()) << "saveDmapJson failed";
    ASSERT_TRUE(std::filesystem::exists(savePath));

    auto loadResult = loadDmapJson(savePath);
    ASSERT_TRUE(loadResult.has_value()) << "loadDmapJson failed";

    assertMapEqual(original, *loadResult);
}

TEST_F(DmapIOTest, JsonLoadMissingFile)
{
    auto result = loadDmapJson(m_tmpDir / "nonexistent.dmap.json");
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), DmapError::FileNotFound);
}
