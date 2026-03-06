// Daedalus Engine — Application Entry Point
// Phase 1D: ECS static mesh + billboard sprite rendering.

// stb_image_write implementation (compiled once in this TU).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include "stb_image_write.h"
#pragma clang diagnostic pop

#include "daedalus/core/assert.h"
#include "daedalus/core/create_platform.h"
#include "daedalus/core/ecs/world.h"
#include "daedalus/core/events/event_bus.h"
#include "daedalus/core/threading/job_system.h"
#include "daedalus/render/create_render_device.h"
#include "daedalus/render/rhi/i_command_queue.h"
#include "daedalus/render/rhi/i_command_buffer.h"
#include "daedalus/render/rhi/i_swapchain.h"
#include "daedalus/render/frame_renderer.h"
#include "daedalus/render/scene_view.h"
#include "daedalus/render/i_asset_loader.h"
#include "daedalus/world/map_data.h"
#include "daedalus/world/i_world_map.h"
#include "daedalus/world/i_portal_traversal.h"
#include "daedalus/world/sector_tessellator.h"
#include "daedalus/world/dmap_io.h"

#include "daedalus/core/components/transform_component.h"
#include "daedalus/render/components/static_mesh_component.h"
#include "daedalus/render/components/billboard_sprite_component.h"
#include "daedalus/render/systems/mesh_render_system.h"
#include "daedalus/render/systems/billboard_render_system.h"
#include "daedalus/render/components/animation_state_component.h"
#include "daedalus/render/systems/sprite_animation_system.h"
#include "daedalus/render/vox_types.h"
#include "daedalus/render/vox_mesher.h"
#include "daedalus/render/components/voxel_object_component.h"
#include "daedalus/render/systems/voxel_render_system.h"
#include "daedalus/render/components/decal_component.h"
#include "daedalus/render/systems/decal_render_system.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

using namespace daedalus;
using namespace daedalus::rhi;

// ─── Test map factory ─────────────────────────────────────────────────────────
// Builds a 2-sector corridor map exercising the portal traversal system.
//
// Sector 0 (main room): 10 × 10 units, x ∈ [-5, 5], z ∈ [-5, 5], floor=0, ceil=4.
// Sector 1 (side room):  8 × 6 units, x ∈ [5, 13],  z ∈ [-3, 3], floor=0, ceil=3.
// Portal: sector-0 east wall x=5, z ∈ [-3,3] opens into sector 1.
//
// Wall order is CCW when viewed from above (Y+).

static world::WorldMapData buildTestMap()
{
    using namespace world;

    WorldMapData map;
    map.name   = "TestMap_Phase3";
    map.author = "Daedalus Engine";

    // ── Sector 0: main room ───────────────────────────────────────────────────
    // CCW winding: SW → SE_bottom → SE_portal_bot → SE_portal_top → SE_top → NE → NW
    // We model the east wall as three segments to create the portal opening:
    //   solid lower segment (z -5 → -3), portal segment (z -3 → +3), solid upper (z +3 → +5).
    // Actually simpler: use a full east wall as a portal. Both sectors share the full
    // wall opening; upper/lower strips are generated automatically by the tessellator
    // based on height differences (ceil 4 vs 3 → upper strip on the sector-0 side).
    Sector s0;
    s0.floorHeight = 0.0f;
    s0.ceilHeight  = 4.0f;
    s0.ambientColor     = {0.05f, 0.05f, 0.10f};
    s0.ambientIntensity = 1.0f;

    //       p0          portal      notes
    Wall s0w0; s0w0.p0 = {-5.0f, -5.0f};                          // south wall start (SW)
    Wall s0w1; s0w1.p0 = { 5.0f, -5.0f}; s0w1.portalSectorId = 1u; // east wall → sector 1
    Wall s0w2; s0w2.p0 = { 5.0f,  5.0f};                          // north wall start (SE→NE)
    Wall s0w3; s0w3.p0 = {-5.0f,  5.0f};                          // west wall start  (NW)
    s0.walls = { s0w0, s0w1, s0w2, s0w3 };

    // ── Sector 1: side room ───────────────────────────────────────────────────
    // x ∈ [5,13], z ∈ [-5,5] (same span as the portal wall for simplicity).
    Sector s1;
    s1.floorHeight = 0.0f;
    s1.ceilHeight  = 3.0f;  // lower ceiling → upper strip generated on sector-0 side
    s1.ambientColor     = {0.08f, 0.05f, 0.05f};  // slightly warm tint
    s1.ambientIntensity = 1.0f;

    Wall s1w0; s1w0.p0 = { 5.0f, -5.0f};                           // south wall (west end)
    Wall s1w1; s1w1.p0 = {13.0f, -5.0f};                           // east wall start
    Wall s1w2; s1w2.p0 = {13.0f,  5.0f};                           // north wall start
    Wall s1w3; s1w3.p0 = { 5.0f,  5.0f}; s1w3.portalSectorId = 0u; // west wall → sector 0
    s1.walls = { s1w0, s1w1, s1w2, s1w3 };

    map.sectors = { std::move(s0), std::move(s1) };
    return map;
}

// ─── Box mesh factory ────────────────────────────────────────────────────────
// Builds a solid box (top + 4 vertical faces, no bottom) sitting on the floor
// at world-space centre (cx, 0, cz) with the given half-extents and height.
//
// Winding follows the Metal LH + viewport-Y-flip convention:
//   vertical faces → (0,2,1),(0,3,2) — same as fixed wall tessellator
//   top face       → (0,1,2),(0,2,3) — same as floor tessellator
//
// Stored normals point toward the camera (outward from box surface).
// Tangents are chosen so cross(N, T) * 1 == (0, 1, 0) for all vertical faces.

static render::MeshData makeBoxMesh(
    float cx, float cz, float height, float halfEx, float halfEz)
{
    using render::StaticMeshVertex;
    render::MeshData mesh;
    auto& V = mesh.vertices;
    auto& I = mesh.indices;

    auto vtx = [](float px, float py, float pz,
                  float nx, float ny, float nz,
                  float u,  float v,
                  float tx, float ty, float tz) noexcept -> StaticMeshVertex
    {
        StaticMeshVertex s{};
        s.pos[0]=px;     s.pos[1]=py;     s.pos[2]=pz;
        s.normal[0]=nx;  s.normal[1]=ny;  s.normal[2]=nz;
        s.uv[0]=u;       s.uv[1]=v;
        s.tangent[0]=tx; s.tangent[1]=ty; s.tangent[2]=tz; s.tangent[3]=1.0f;
        return s;
    };

    // Vertical face quad: v0=BL, v1=BR, v2=TR, v3=TL.
    // Winding (0,2,1),(0,3,2): geometric cross-product points away from the
    // camera, which is the front-facing convention after Metal's viewport Y-flip.
    auto addVerticalFace = [&](
        float x0, float z0, float x1, float z1,
        float yFloor, float yCeil,
        float nx, float nz,
        float tx, float tz)
    {
        const float len = std::sqrt((x1-x0)*(x1-x0) + (z1-z0)*(z1-z0));
        const float h   = yCeil - yFloor;
        const u32 base  = static_cast<u32>(V.size());
        V.push_back(vtx(x0,yFloor,z0,  nx,0,nz,  0,  0,   tx,0,tz));
        V.push_back(vtx(x1,yFloor,z1,  nx,0,nz,  len,0,   tx,0,tz));
        V.push_back(vtx(x1,yCeil, z1,  nx,0,nz,  len,h,   tx,0,tz));
        V.push_back(vtx(x0,yCeil, z0,  nx,0,nz,  0,  h,   tx,0,tz));
        I.push_back(base+0); I.push_back(base+2); I.push_back(base+1);
        I.push_back(base+0); I.push_back(base+3); I.push_back(base+2);
    };

    const float x0 = cx - halfEx, x1 = cx + halfEx;
    const float z0 = cz - halfEz, z1 = cz + halfEz;

    // South (-Z): tangent=(-1,0) → bitangent=(0,1,0)
    addVerticalFace(x1,z0, x0,z0, 0,height,  0,-1,  -1, 0);
    // North (+Z): tangent=(+1,0) → bitangent=(0,1,0)
    addVerticalFace(x0,z1, x1,z1, 0,height,  0, 1,   1, 0);
    // Left  (-X): tangent=(0,+1) → bitangent=(0,1,0)
    addVerticalFace(x0,z0, x0,z1, 0,height, -1, 0,   0, 1);
    // Right (+X): tangent=(0,-1) → bitangent=(0,1,0)
    addVerticalFace(x1,z1, x1,z0, 0,height,  1, 0,   0,-1);

    // Top face: (0,1,2),(0,2,3) — floor winding, normal up
    {
        const u32 base = static_cast<u32>(V.size());
        V.push_back(vtx(x0,height,z0, 0,1,0, 0,         0,          1,0,0));
        V.push_back(vtx(x1,height,z0, 0,1,0, 2*halfEx,  0,          1,0,0));
        V.push_back(vtx(x1,height,z1, 0,1,0, 2*halfEx,  2*halfEz,   1,0,0));
        V.push_back(vtx(x0,height,z1, 0,1,0, 0,         2*halfEz,   1,0,0));
        I.push_back(base+0); I.push_back(base+1); I.push_back(base+2);
        I.push_back(base+0); I.push_back(base+2); I.push_back(base+3);
    }

    return mesh;
}

// ─── Entry point ──────────────────────────────────────────────────────────────

int main(int /*argc*/, char* /*argv*/[])
{
    // ─── Platform ─────────────────────────────────────────────────────────────

    auto platform = createPlatform();
    DAEDALUS_ASSERT(platform != nullptr, "createPlatform returned null");

    std::printf("[Daedalus] Executable dir: %s\n",
                platform->getExecutableDir().c_str());

    // ─── SDL3 initialisation ──────────────────────────────────────────────────

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "[Daedalus] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    constexpr u32 WINDOW_W = 1280;
    constexpr u32 WINDOW_H = 720;

    SDL_Window* window = SDL_CreateWindow("Daedalus Engine",
                                          WINDOW_W, WINDOW_H,
                                          SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE);
    DAEDALUS_ASSERT(window != nullptr, "Failed to create SDL window");

    // ─── ECS World ─────────────────────────────────────────────────────────────
    // Main ECS container — persists for the application’s lifetime.
    World world;

    // ─── EventBus smoke test ──────────────────────────────────────────────────

    {
        struct WindowResizeEvent { u32 width; u32 height; };
        EventBus<WindowResizeEvent> resizeBus;

        bool fired = false;
        auto handle = resizeBus.subscribe([&fired](const WindowResizeEvent& e)
        {
            std::printf("[Daedalus] WindowResize: %u x %u\n", e.width, e.height);
            fired = true;
        });

        resizeBus.publish({ 1280, 720 });
        DAEDALUS_ASSERT(fired, "EventBus did not fire subscriber");
        resizeBus.unsubscribe(handle);
        std::printf("[Daedalus] EventBus smoke test passed.\n");
    }

    // ─── JobSystem smoke test ─────────────────────────────────────────────────

    {
        JobSystem jobs;
        std::atomic<int> counter{ 0 };

        auto h1 = jobs.submit([&counter] { counter.fetch_add(1); });
        auto h2 = jobs.submit([&counter] { counter.fetch_add(1); }, h1);
        h2.wait();

        DAEDALUS_ASSERT(counter.load() == 2, "JobSystem dependency chain failed");
        std::printf("[Daedalus] JobSystem smoke test passed.\n");
    }

    // ─── RHI device + swapchain ───────────────────────────────────────────────

    auto device    = rhi::createRenderDevice();
    auto queue     = device->createCommandQueue("Main Queue");
    auto swapchain = device->createSwapchain(window, WINDOW_W, WINDOW_H);

    std::printf("[Daedalus] GPU: %s\n",
                std::string(device->deviceName()).c_str());

    // ─── FrameRenderer ────────────────────────────────────────────────────────

    const std::string metalLibPath =
        platform->getExecutableDir() + "/daedalus_shaders.metallib";
    std::printf("[Daedalus] Shader library: %s\n", metalLibPath.c_str());

    render::FrameRenderer renderer;
    renderer.initialize(*device, metalLibPath, WINDOW_W, WINDOW_H);

    // ─── Asset setup ──────────────────────────────────────────────────────────────────────

    const std::string assetsDir  = platform->getExecutableDir();
    const std::string albedoPath = assetsDir + "/wall_albedo.png";
    const std::string normalPath = assetsDir + "/wall_normal.png";
    const std::string dmapPath   = assetsDir + "/test_map.dmap";

    // Generate test textures if they don’t already exist.
    // Albedo: 256×256 warm brick pattern.
    // Normal: 256×256 near-flat tangent-space normal with subtle brick indents.
    {
        constexpr int TEX_W = 256, TEX_H = 256;
        constexpr int BRICK_W = 32, BRICK_H = 16;

        if (!std::filesystem::exists(albedoPath))
        {
            std::vector<u8> pixels(TEX_W * TEX_H * 4);
            for (int y = 0; y < TEX_H; ++y)
            {
                // Every other row is offset by half a brick width (running bond pattern).
                const int rowOffset = ((y / BRICK_H) & 1) ? BRICK_W / 2 : 0;
                for (int x = 0; x < TEX_W; ++x)
                {
                    const int brickX   = (x + rowOffset) % BRICK_W;
                    const int brickY   = y % BRICK_H;
                    // Mortar gap: 1 px on bottom and right of each brick.
                    const bool mortar  = (brickX == BRICK_W - 1) || (brickY == BRICK_H - 1);

                    u8 r, g, b;
                    if (mortar)
                    {
                        // Light warm mortar
                        r = 195; g = 185; b = 175;
                    }
                    else
                    {
                        // Warm terracotta brick with subtle variation
                        const int hash = ((x * 127 + y * 311) ^ (x / BRICK_W * 97)) & 0xFF;
                        r = static_cast<u8>(165 + (hash & 0x1F));        // 165–196
                        g = static_cast<u8>( 82 + ((hash >> 2) & 0x0F)); //  82–96
                        b = static_cast<u8>( 55 + ((hash >> 4) & 0x0F)); //  55–69
                    }

                    const int idx  = (y * TEX_W + x) * 4;
                    pixels[idx + 0] = r;
                    pixels[idx + 1] = g;
                    pixels[idx + 2] = b;
                    pixels[idx + 3] = 255;
                }
            }
            stbi_write_png(albedoPath.c_str(), TEX_W, TEX_H, 4, pixels.data(), TEX_W * 4);
            std::printf("[Daedalus] Generated %s\n", albedoPath.c_str());
        }

        if (!std::filesystem::exists(normalPath))
        {
            std::vector<u8> pixels(TEX_W * TEX_H * 4);
            for (int y = 0; y < TEX_H; ++y)
            {
                const int rowOffset = ((y / BRICK_H) & 1) ? BRICK_W / 2 : 0;
                for (int x = 0; x < TEX_W; ++x)
                {
                    const int brickX   = (x + rowOffset) % BRICK_W;
                    const int brickY   = y % BRICK_H;
                    const bool mortar  = (brickX == BRICK_W - 1) || (brickY == BRICK_H - 1);

                    // Mortar is slightly depressed → tilted normal; brick face is flat (0,0,1).
                    u8 nx = 128, ny = 128, nz = 255;
                    if (mortar)
                    {
                        // Tilt inward toward the mortar centre
                        const float tx = (brickX == BRICK_W - 1) ? -0.25f : 0.0f;
                        const float ty = (brickY == BRICK_H - 1) ? -0.25f : 0.0f;
                        nx = static_cast<u8>(128 + static_cast<int>(tx * 127.0f));
                        ny = static_cast<u8>(128 + static_cast<int>(ty * 127.0f));
                        nz = 200; // slightly less Z → appears recessed
                    }

                    const int idx  = (y * TEX_W + x) * 4;
                    pixels[idx + 0] = nx;
                    pixels[idx + 1] = ny;
                    pixels[idx + 2] = nz;
                    pixels[idx + 3] = 255;
                }
            }
            stbi_write_png(normalPath.c_str(), TEX_W, TEX_H, 4, pixels.data(), TEX_W * 4);
            std::printf("[Daedalus] Generated %s\n", normalPath.c_str());
        }
    }

    // Load textures via IAssetLoader (returns std::expected; assert on failure).
    auto assetLoader = render::makeAssetLoader();

    auto albedoResult = assetLoader->loadTexture(*device,
                                                  std::filesystem::path(albedoPath), true);
    DAEDALUS_ASSERT(albedoResult.has_value(), "Failed to load wall_albedo.png");
    auto wallAlbedo = std::move(*albedoResult);

    auto normalResult = assetLoader->loadTexture(*device,
                                                  std::filesystem::path(normalPath), false);
    DAEDALUS_ASSERT(normalResult.has_value(), "Failed to load wall_normal.png");
    auto wallNormal = std::move(*normalResult);

    // ─── World map ────────────────────────────────────────────────────────────
    // Build or load the test map. Persist it as a .dmap on first run so it can
    // be inspected / edited outside the app.
    {
        const auto testMapPath = std::filesystem::path(dmapPath);
        if (!std::filesystem::exists(testMapPath))
        {
            const auto saveResult = world::saveDmap(buildTestMap(), testMapPath);
            DAEDALUS_ASSERT(saveResult.has_value(), "Failed to save test_map.dmap");
            std::printf("[Daedalus] Saved %s\n", dmapPath.c_str());

            // Also emit the .dmap.json alongside it for inspection.
            const auto jsonPath = std::filesystem::path(assetsDir + "/test_map.dmap.json");
            world::saveDmapJson(buildTestMap(), jsonPath);  // best-effort
        }
    }

    auto loadResult = world::loadDmap(std::filesystem::path(dmapPath));
    DAEDALUS_ASSERT(loadResult.has_value(), "Failed to load test_map.dmap");

    auto worldMap      = world::makeWorldMap(std::move(*loadResult));
    auto portalTraversal = world::makePortalTraversal();

    // Tessellate all sectors into CPU-side MeshData, then upload to GPU.
    const std::vector<render::MeshData> sectorMeshes = world::tessellateMap(worldMap->data());
    const std::size_t numSectors = sectorMeshes.size();

    std::vector<std::unique_ptr<rhi::IBuffer>> sectorVBOs(numSectors);
    std::vector<std::unique_ptr<rhi::IBuffer>> sectorIBOs(numSectors);
    std::vector<render::MeshDraw>              sectorDraws(numSectors);

    for (std::size_t si = 0; si < numSectors; ++si)
    {
        const render::MeshData& mesh = sectorMeshes[si];
        if (mesh.vertices.empty()) { continue; }

        {
            rhi::BufferDescriptor d;
            d.size      = mesh.vertices.size() * sizeof(render::StaticMeshVertex);
            d.usage     = rhi::BufferUsage::Vertex;
            d.initData  = mesh.vertices.data();
            d.debugName = "SectorVBO_" + std::to_string(si);
            sectorVBOs[si] = device->createBuffer(d);
        }
        {
            rhi::BufferDescriptor d;
            d.size      = mesh.indices.size() * sizeof(u32);
            d.usage     = rhi::BufferUsage::Index;
            d.initData  = mesh.indices.data();
            d.debugName = "SectorIBO_" + std::to_string(si);
            sectorIBOs[si] = device->createBuffer(d);
        }

        render::MeshDraw& draw    = sectorDraws[si];
        draw.vertexBuffer         = sectorVBOs[si].get();
        draw.indexBuffer          = sectorIBOs[si].get();
        draw.indexCount           = static_cast<u32>(mesh.indices.size());
        draw.modelMatrix          = glm::mat4(1.0f);
        draw.prevModel            = glm::mat4(1.0f);
        draw.material.albedo      = wallAlbedo.get();
        draw.material.normalMap   = wallNormal.get();
        draw.material.roughness   = 0.85f;
        draw.material.metalness   = 0.0f;
    }

    std::printf("[Daedalus] World loaded: %zu sector(s)\n", numSectors);

    // ─── Test block → ECS static mesh entity ──────────────────────────────────────────────
    // 1.5 × 1.5 × 1.5 unit box at the centre of sector 0, driven by ECS.

    const render::MeshData boxMesh = makeBoxMesh(0.0f, 0.0f, 1.5f, 0.75f, 0.75f);

    std::unique_ptr<rhi::IBuffer> boxVBO;
    std::unique_ptr<rhi::IBuffer> boxIBO;

    {
        rhi::BufferDescriptor d;
        d.size      = boxMesh.vertices.size() * sizeof(render::StaticMeshVertex);
        d.usage     = rhi::BufferUsage::Vertex;
        d.initData  = boxMesh.vertices.data();
        d.debugName = "BoxVBO";
        boxVBO = device->createBuffer(d);
    }
    {
        rhi::BufferDescriptor d;
        d.size      = boxMesh.indices.size() * sizeof(u32);
        d.usage     = rhi::BufferUsage::Index;
        d.initData  = boxMesh.indices.data();
        d.debugName = "BoxIBO";
        boxIBO = device->createBuffer(d);
    }

    {
        render::StaticMeshComponent meshComp;
        meshComp.vertexBuffer       = boxVBO.get();
        meshComp.indexBuffer        = boxIBO.get();
        meshComp.indexCount         = static_cast<u32>(boxMesh.indices.size());
        meshComp.material.albedo    = wallAlbedo.get();
        meshComp.material.normalMap = wallNormal.get();
        meshComp.material.roughness = 0.75f;
        meshComp.material.metalness = 0.0f;
        // prevModelMatrix defaults to identity — correct for this static object.

        EntityId boxEntity = world.createEntity();
        world.addComponent(boxEntity, TransformComponent{});  // origin, identity, unit scale
        world.addComponent(boxEntity, std::move(meshComp));
    }

    std::printf("[Daedalus] Box entity created (%zu verts, %zu indices).\n",
                boxMesh.vertices.size(), boxMesh.indices.size());

    // ─── Shared unit quad (billboard sprites) ────────────────────────────────────────────
    // All billboard sprites share a single 1×1 quad.  The BillboardRenderSystem
    // applies a per-entity spherical billboard model matrix each frame.

    const render::MeshData unitQuadMesh = render::makeUnitQuadMesh();

    std::unique_ptr<rhi::IBuffer> unitQuadVBO;
    std::unique_ptr<rhi::IBuffer> unitQuadIBO;

    {
        rhi::BufferDescriptor d;
        d.size      = unitQuadMesh.vertices.size() * sizeof(render::StaticMeshVertex);
        d.usage     = rhi::BufferUsage::Vertex;
        d.initData  = unitQuadMesh.vertices.data();
        d.debugName = "UnitQuadVBO";
        unitQuadVBO = device->createBuffer(d);
    }
    {
        rhi::BufferDescriptor d;
        d.size      = unitQuadMesh.indices.size() * sizeof(u32);
        d.usage     = rhi::BufferUsage::Index;
        d.initData  = unitQuadMesh.indices.data();
        d.debugName = "UnitQuadIBO";
        unitQuadIBO = device->createBuffer(d);
    }

    // ─── Test billboard sprite entity ──────────────────────────────────────────────────
    // 64×64 RGBA texture: warm orange circle with a hard alpha-cutout boundary.
    // Placed at (−2, 1.5, 0) in sector 0 so it is lit by the ceiling spot light.

    std::unique_ptr<rhi::ITexture> spriteTexture;
    {
        constexpr int SPR_W = 64, SPR_H = 64;
        std::vector<u8> pixels(SPR_W * SPR_H * 4, 0u);

        const float cx = 31.5f, cy = 31.5f, radius = 27.0f;
        for (int y = 0; y < SPR_H; ++y)
        {
            for (int x = 0; x < SPR_W; ++x)
            {
                const float dx   = static_cast<float>(x) - cx;
                const float dy   = static_cast<float>(y) - cy;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const int   idx  = (y * SPR_W + x) * 4;

                if (dist < radius)
                {
                    // Sphere-shaded warm circle: bright yellow centre → deep orange edge.
                    const float t    = 1.0f - (dist / radius);
                    pixels[idx + 0]  = 255u;
                    pixels[idx + 1]  = static_cast<u8>(60 + static_cast<int>(t * 180));
                    pixels[idx + 2]  = 0u;
                    pixels[idx + 3]  = 255u;  // fully opaque
                }
                // else: all zeros — transparent outside the circle
            }
        }

        rhi::TextureDescriptor td;
        td.width    = static_cast<u32>(SPR_W);
        td.height   = static_cast<u32>(SPR_H);
        td.format   = rhi::TextureFormat::RGBA8Unorm_sRGB;
        td.usage    = rhi::TextureUsage::ShaderRead;
        td.initData = pixels.data();
        td.debugName = "sprite_test";
        spriteTexture = device->createTexture(td);
        DAEDALUS_ASSERT(spriteTexture != nullptr, "Failed to create sprite texture");
    }

    {
        TransformComponent spriteTransform;
        spriteTransform.position = glm::vec3(-2.0f, 1.5f, 0.0f);

        render::BillboardSpriteComponent spriteComp;
        spriteComp.texture = spriteTexture.get();
        spriteComp.size    = glm::vec2(1.2f);  // 1.2 × 1.2 world units

        EntityId spriteEntity = world.createEntity();
        world.addComponent(spriteEntity, spriteTransform);
        world.addComponent(spriteEntity, spriteComp);
    }

    std::printf("[Daedalus] Billboard sprite entity created.\n");

    // ─── Blended billboard sprite entity (transparency pass demo) ─────────────────────
    // 64×64 RGBA texture: filled white square (all pixels fully opaque).
    // AlphaMode::Blended + a semi-transparent cyan tint demonstrates the forward
    // transparency pass: the sprite composites over whatever is behind it with
    // alpha = texture.a * tint.a = 1.0 * 0.65 = 0.65.
    // Placed at (2, 1.5, 2) — in sector 0, slightly behind the box, so depth
    // ordering exercises both the depth-test and the back-to-front sort.

    std::unique_ptr<rhi::ITexture> blendedSpriteTexture;
    {
        constexpr int W = 64, H = 64;
        std::vector<u8> pixels(W * H * 4, 255u);  // all fully opaque white

        rhi::TextureDescriptor td;
        td.width     = static_cast<u32>(W);
        td.height    = static_cast<u32>(H);
        td.format    = rhi::TextureFormat::RGBA8Unorm;
        td.usage     = rhi::TextureUsage::ShaderRead;
        td.initData  = pixels.data();
        td.debugName = "sprite_blended_test";
        blendedSpriteTexture = device->createTexture(td);
        DAEDALUS_ASSERT(blendedSpriteTexture != nullptr, "Failed to create blended sprite texture");
    }

    {
        TransformComponent blendedTransform;
        blendedTransform.position = glm::vec3(2.0f, 1.5f, 2.0f);

        render::BillboardSpriteComponent blendedComp;
        blendedComp.texture   = blendedSpriteTexture.get();
        blendedComp.size      = glm::vec2(1.2f);
        blendedComp.alphaMode = render::AlphaMode::Blended;
        // Cyan tint at 65% opacity: demonstrates tint.rgb modulates albedo (white → cyan)
        // and tint.a controls the overall transparency.
        blendedComp.tint      = glm::vec4(0.20f, 0.82f, 1.00f, 0.65f);

        EntityId blendedEntity = world.createEntity();
        world.addComponent(blendedEntity, blendedTransform);
        world.addComponent(blendedEntity, blendedComp);
    }

    std::printf("[Daedalus] Blended billboard sprite entity created.\n");

    // ─── Animated sprite entity (sprite sheet animation demo) ─────────────────
    // 4-frame × 2-row sprite sheet (128×64): each frame cell is 32×32 pixels.
    //   Row 0: idle cycle  (4 frames, 2 fps)
    //   Row 1: walk cycle  (4 frames, 2 fps)
    // The entity starts on row 0 (idle) and will cycle automatically.
    // Orbits above the brick box (box top = y 1.5) so it acts as a spinning
    // pickup indicator — position is updated every frame in the main loop.

    std::unique_ptr<rhi::ITexture> animSpriteTexture;
    {
        constexpr int SHEET_W = 128, SHEET_H = 64;
        constexpr int CELL_W  =  32, CELL_H  = 32;
        constexpr int COLS = SHEET_W / CELL_W;  // 4
        constexpr int ROWS = SHEET_H / CELL_H;  // 2

        std::vector<u8> pixels(SHEET_W * SHEET_H * 4, 0u);

        // Draw each frame cell with a distinct hue so animated cycling is visible.
        // Row 0 (idle): warm reds/oranges; Row 1 (walk): cool blues/greens.
        const u8 cellColors[ROWS][COLS][3] =
        {
            { {220, 60,  40},  {240, 120, 30},  {200, 200, 40},  {180, 80,  60}  },  // row 0
            { {40,  80,  220}, {30,  160, 200},  {50,  200, 120}, {80,  120, 220} },  // row 1
        };

        for (int row = 0; row < ROWS; ++row)
        {
            for (int col = 0; col < COLS; ++col)
            {
                const int ox = col * CELL_W;
                const int oy = row * CELL_H;
                const float cx = ox + CELL_W  * 0.5f;
                const float cy = oy + CELL_H * 0.5f;
                const float radius = CELL_W * 0.4f;

                for (int y = oy; y < oy + CELL_H; ++y)
                {
                    for (int x = ox; x < ox + CELL_W; ++x)
                    {
                        const float dx   = static_cast<float>(x) - cx;
                        const float dy   = static_cast<float>(y) - cy;
                        const float dist = std::sqrt(dx * dx + dy * dy);
                        const int   idx  = (y * SHEET_W + x) * 4;

                        if (dist < radius)
                        {
                            pixels[idx + 0] = cellColors[row][col][0];
                            pixels[idx + 1] = cellColors[row][col][1];
                            pixels[idx + 2] = cellColors[row][col][2];
                            pixels[idx + 3] = 255u;  // fully opaque
                        }
                    }
                }
            }
        }

        rhi::TextureDescriptor td;
        td.width     = static_cast<u32>(SHEET_W);
        td.height    = static_cast<u32>(SHEET_H);
        td.format    = rhi::TextureFormat::RGBA8Unorm_sRGB;
        td.usage     = rhi::TextureUsage::ShaderRead;
        td.initData  = pixels.data();
        td.debugName = "sprite_sheet_test";
        animSpriteTexture = device->createTexture(td);
        DAEDALUS_ASSERT(animSpriteTexture != nullptr, "Failed to create animated sprite texture");
    }

    // animEntity is declared here so the main loop can update its position each frame.
    EntityId animEntity;
    {
        TransformComponent animTransform;
        animTransform.position = glm::vec3(0.3f, 2.0f, 0.0f);  // initial angle=0 of orbit

        render::BillboardSpriteComponent animSprite;
        animSprite.texture         = animSpriteTexture.get();
        animSprite.size            = glm::vec2(0.9f);  // fits neatly above the 1.5-unit box
        animSprite.alphaMode       = render::AlphaMode::Blended;  // transparent pass: CullMode::None
        animSprite.tint            = glm::vec4(1.0f);             // fully opaque circles, transparent bg
        animSprite.emissiveTexture = animSpriteTexture.get();      // self-illuminate: visible regardless of NdotL
        // uvOffset / uvScale will be set by spriteAnimationSystem each frame.

        render::AnimationStateComponent animState;
        animState.frameCount  = 4;
        animState.rowCount    = 2;
        animState.currentRow  = 0;    // start on idle row
        animState.fps         = 2.0f; // one frame every 0.5 s — easily readable
        animState.loop        = true;

        animEntity = world.createEntity();
        world.addComponent(animEntity, animTransform);
        world.addComponent(animEntity, std::move(animSprite));
        world.addComponent(animEntity, std::move(animState));
    }

    std::printf("[Daedalus] Animated sprite-sheet entity created (4 frames × 2 rows, 2 fps).\n");

    // ─── Voxel object entity (voxel sprite demo) ──────────────────────────────
    // Programmatic 4×4×4 coloured volume: each column (x,z) gets a distinct
    // palette colour so all 6 face directions and the greedy merge path are
    // exercised.  Placed at (3, 0, -2) in sector 0, slightly offset from the
    // box mesh so both objects are independently visible.

    std::unique_ptr<rhi::ITexture> voxPaletteTex;
    std::unique_ptr<rhi::IBuffer>  voxVBO;
    std::unique_ptr<rhi::IBuffer>  voxIBO;

    {
        using namespace render;

        VoxData testVox;
        testVox.sizeX = 4;
        testVox.sizeY = 4;
        testVox.sizeZ = 4;
        testVox.voxels.resize(4 * 4 * 4, 0u);

        // Solid 4×4×4 cube: each (x,z) column gets a distinct palette index
        // (1..16) so all 6 face directions and the greedy merge path are exercised.
        for (u32 x = 0; x < 4; ++x)
            for (u32 y = 0; y < 4; ++y)
                for (u32 z = 0; z < 4; ++z)
                {
                    const u8 ci = static_cast<u8>(1 + x + 4 * z);
                    testVox.voxels[x + 4 * (y + 4 * z)] = ci;
                }

        // Build a simple 16-colour palette (bright hues for indices 1-16;
        // the remaining 239 slots stay zero which is fine — they are not used).
        std::array<u8, 256 * 4> paletteBytes{};
        const u8 swatchRGB[16][3] =
        {
            {220, 60,  40},  {240,120, 30},  {200,200, 40},  {60, 180,  60},
            {40, 180, 220},  { 60, 80,220},  {160, 60,220},  {220, 60,160},
            {200,200,200},  {120,120,120},  {255,160,  0},  {  0,200,160},
            {160,240, 80},  { 80,160,240},  {240, 80,160},  {255,240,  80},
        };
        for (int i = 0; i < 16; ++i)
        {
            paletteBytes[(i + 1) * 4 + 0] = swatchRGB[i][0];
            paletteBytes[(i + 1) * 4 + 1] = swatchRGB[i][1];
            paletteBytes[(i + 1) * 4 + 2] = swatchRGB[i][2];
            paletteBytes[(i + 1) * 4 + 3] = 255u;
        }

        // Upload 256×1 palette texture (RGBA8Unorm — linear, not sRGB, so
        // the G-buffer shader sees exact palette values).
        {
            rhi::TextureDescriptor td;
            td.width     = 256u;
            td.height    = 1u;
            td.format    = rhi::TextureFormat::RGBA8Unorm;
            td.usage     = rhi::TextureUsage::ShaderRead;
            td.initData  = paletteBytes.data();
            td.debugName = "vox_palette";
            voxPaletteTex = device->createTexture(td);
            DAEDALUS_ASSERT(voxPaletteTex != nullptr, "Failed to create vox palette texture");
        }

        // Greedy-mesh the voxel volume on the CPU.
        const MeshData voxMesh = greedyMeshVoxels(testVox);
        std::printf("[Daedalus] Vox mesh: %zu verts, %zu indices.\n",
                    voxMesh.vertices.size(), voxMesh.indices.size());

        // Upload geometry.
        {
            rhi::BufferDescriptor d;
            d.size      = voxMesh.vertices.size() * sizeof(StaticMeshVertex);
            d.usage     = rhi::BufferUsage::Vertex;
            d.initData  = voxMesh.vertices.data();
            d.debugName = "VoxVBO";
            voxVBO = device->createBuffer(d);
        }
        {
            rhi::BufferDescriptor d;
            d.size      = voxMesh.indices.size() * sizeof(u32);
            d.usage     = rhi::BufferUsage::Index;
            d.initData  = voxMesh.indices.data();
            d.debugName = "VoxIBO";
            voxIBO = device->createBuffer(d);
        }

        // Create ECS entity.
        VoxelObjectComponent voxComp;
        voxComp.vertexBuffer    = voxVBO.get();
        voxComp.indexBuffer     = voxIBO.get();
        voxComp.indexCount      = static_cast<u32>(voxMesh.indices.size());
        voxComp.material.albedo = voxPaletteTex.get();
        voxComp.material.roughness = 1.0f;  // fully matte — eliminates view-dependent specular sweep on flat voxel faces
        voxComp.material.metalness = 0.0f;

        TransformComponent voxTransform;
        voxTransform.position = glm::vec3(3.0f, 0.1f, -2.0f);  // 0.1 lift clears floor z-fighting
        voxTransform.scale    = glm::vec3(0.3f);  // 4×4×4 voxels × 0.3 = 1.2 world units per side

        EntityId voxEntity = world.createEntity();
        world.addComponent(voxEntity, voxTransform);
        world.addComponent(voxEntity, std::move(voxComp));
    }

    std::printf("[Daedalus] Voxel object entity created (4\u00d74\u00d74 volume).\n");

    // ─── Deferred decal entities (Pass 2.5 demo) ─────────────────────────────────────
    // Procedural 128×128 "damage mark" texture: dark reddish-brown disc with
    // a quadratic alpha fall-off so the edges blend smoothly into the floor.
    // Three DecalComponent entities at floor level (y ≈0.08) demonstrate
    // G-buffer alpha blending in sector 0 and sector 1.

    std::unique_ptr<rhi::ITexture> decalTexture;
    {
        constexpr int DEC_W = 128, DEC_H = 128;
        std::vector<u8> pixels(DEC_W * DEC_H * 4, 0u);

        const float dcx    = (DEC_W - 1) * 0.5f;
        const float dcy    = (DEC_H - 1) * 0.5f;
        const float radius = DEC_W * 0.45f;

        for (int y = 0; y < DEC_H; ++y)
        {
            for (int x = 0; x < DEC_W; ++x)
            {
                const float ddx  = static_cast<float>(x) - dcx;
                const float ddy  = static_cast<float>(y) - dcy;
                const float dist = std::sqrt(ddx * ddx + ddy * ddy);
                const int   idx  = (y * DEC_W + x) * 4;

                if (dist < radius)
                {
                    // Quadratic fall-off: fully opaque centre, transparent edge.
                    const float t     = 1.0f - (dist / radius);
                    const u8    alpha = static_cast<u8>(t * t * 220.0f);  // 0–220
                    // Dark reddish-brown (damage / burn mark).
                    pixels[idx + 0] = static_cast<u8>(60  + static_cast<int>(t * 40));  // R 60–100
                    pixels[idx + 1] = static_cast<u8>(20  + static_cast<int>(t * 20));  // G 20–40
                    pixels[idx + 2] = static_cast<u8>(10  + static_cast<int>(t * 10));  // B 10–20
                    pixels[idx + 3] = alpha;
                }
            }
        }

        rhi::TextureDescriptor td;
        td.width     = static_cast<u32>(DEC_W);
        td.height    = static_cast<u32>(DEC_H);
        td.format    = rhi::TextureFormat::RGBA8Unorm;  // linear (not sRGB)
        td.usage     = rhi::TextureUsage::ShaderRead;
        td.initData  = pixels.data();
        td.debugName = "decal_damage_mark";
        decalTexture = device->createTexture(td);
        DAEDALUS_ASSERT(decalTexture != nullptr, "Failed to create decal texture");
    }

    // Three floor decals at different positions in sectors 0 and 1.
    // TransformComponent: position = OBB centre; scale = OBB full extents.
    // Decal centre must be ON the floor (y=0) so the thin Y extent [-0.075, +0.075]
    // straddles the floor plane and the fragment shader catches floor fragments.
    {
        // Decal 1 — centre of sector 0 (directly below the spotlight).
        TransformComponent t1;
        t1.position = glm::vec3( 0.0f, 0.0f,  0.0f);
        t1.scale    = glm::vec3( 2.0f, 0.15f,  2.0f);

        render::DecalComponent d1;
        d1.albedoTexture = decalTexture.get();
        d1.roughness     = 0.95f;
        d1.metalness     = 0.0f;
        d1.opacity       = 0.85f;

        EntityId dec1 = world.createEntity();
        world.addComponent(dec1, t1);
        world.addComponent(dec1, std::move(d1));
    }
    {
        // Decal 2 — near the box, slightly offset to show overlap.
        TransformComponent t2;
        t2.position = glm::vec3(-2.0f, 0.0f,  2.0f);
        t2.scale    = glm::vec3( 1.5f, 0.15f,  1.5f);

        render::DecalComponent d2;
        d2.albedoTexture = decalTexture.get();
        d2.roughness     = 0.9f;
        d2.metalness     = 0.0f;
        d2.opacity       = 0.70f;

        EntityId dec2 = world.createEntity();
        world.addComponent(dec2, t2);
        world.addComponent(dec2, std::move(d2));
    }
    {
        // Decal 3 — sector 1 floor (cross-sector decal projection).
        TransformComponent t3;
        t3.position = glm::vec3( 9.0f, 0.0f,  0.0f);
        t3.scale    = glm::vec3( 1.8f, 0.15f,  1.8f);

        render::DecalComponent d3;
        d3.albedoTexture = decalTexture.get();
        d3.roughness     = 0.9f;
        d3.metalness     = 0.0f;
        d3.opacity       = 0.80f;

        EntityId dec3 = world.createEntity();
        world.addComponent(dec3, t3);
        world.addComponent(dec3, std::move(d3));
    }

    std::printf("[Daedalus] Deferred decal entities created (3 floor decals, y=0 centred).\n");

    // Camera sector tracking
    world::SectorId cameraSector = 0u;

    // ─── Frame timing + camera state ─────────────────────────────────────────

    using Clock   = std::chrono::steady_clock;
    auto  startTP = Clock::now();
    auto  prevTP  = startTP;

    glm::mat4 prevView(1.0f);
    glm::mat4 prevProj(1.0f);
    u32       frameIdx = 0;
    u32       swapW    = WINDOW_W;
    u32       swapH    = WINDOW_H;

    // ─── Main loop ────────────────────────────────────────────────────────────

    bool running = true;
    while (running)
    {
        // Poll events
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED)
            {
                swapW = static_cast<u32>(event.window.data1);
                swapH = static_cast<u32>(event.window.data2);
                swapchain->resize(swapW, swapH);
                renderer.resize(*device, swapW, swapH);
            }
        }

        if (!running) { break; }

        // ── Timing ────────────────────────────────────────────────────────────
        const auto  nowTP   = Clock::now();
        const float time    = std::chrono::duration<float>(nowTP - startTP).count();
        const float dt      = std::chrono::duration<float>(nowTP - prevTP).count();
        prevTP = nowTP;

        // ── Camera: slow orbit across both rooms ──────────────────────────────
        // The orbit sweeps the camera through both sectors so portal traversal
        // is exercised: sector 0 (x ∈ [-5,5]) and sector 1 (x ∈ [5,13]).
        const float angle  = time * 0.15f; // one full sweep every ~42 s
        // Orbit around the portal axis — camera stays mostly in sector 0 then
        // crosses into sector 1 briefly, giving a natural portal reveal.
        // Centre at x=2 (sector 0), radius 6 → sweeps x ∈ [-4, 8] crossing portal.
        const glm::vec3 eye    = { 2.0f + std::cos(angle) * 6.0f,
                                   1.6f,
                                   std::sin(angle) * 4.0f };
        // Always look toward the portal opening so both rooms stay in frame.
        const glm::vec3 target = { 5.0f, 1.4f, 0.0f };
        const glm::vec3 up     = { 0.0f, 1.0f, 0.0f };

        const float fovY   = glm::radians(60.0f);
        const float aspect = static_cast<float>(swapW) / static_cast<float>(swapH);

        const glm::mat4 view = glm::lookAtLH(eye, target, up);
        const glm::mat4 proj = glm::perspectiveLH_ZO(fovY, aspect, 0.1f, 100.0f);

        // ── Portal traversal (spec Pass 1) ────────────────────────────────────
        // Update camera sector (point-in-polygon query in XZ plane).
        cameraSector = worldMap->findSector({eye.x, eye.z});
        if (cameraSector == world::INVALID_SECTOR_ID) { cameraSector = 0u; }

        // Run traversal to determine visible sectors for this frame.
        const glm::mat4 viewProj = proj * view;
        const auto visibleSectors = portalTraversal->traverse(*worldMap, cameraSector, viewProj);

        // ── Build SceneView ───────────────────────────────────────────────────────────────
        render::SceneView scene;

        // Submit only visible sectors from portal traversal.
        for (const auto& vs : visibleSectors)
        {
            if (vs.sectorId < sectorDraws.size() &&
                sectorDraws[vs.sectorId].vertexBuffer != nullptr)
            {
                scene.meshDraws.push_back(sectorDraws[vs.sectorId]);
            }
        }

        // Orbit the animated sprite above the box top (y=1.5).
        // Radius 0.3 at 1.5 rad/s ≈ one revolution every 4 s.
        {
            auto& t = world.getComponent<TransformComponent>(animEntity);
            t.position.x = std::cos(time * 1.5f) * 0.3f;
            t.position.y = 2.0f;
            t.position.z = std::sin(time * 1.5f) * 0.3f;
        }

        // Submit ECS entities: static meshes and billboard sprites.
        // spriteAnimationSystem advances frame indices and writes UV crop data
        // into each BillboardSpriteComponent before billboardRenderSystem reads them.
        render::spriteAnimationSystem(world, dt);
        render::meshRenderSystem(world, scene);
        render::voxelRenderSystem(world, scene);
        render::billboardRenderSystem(world, scene, view,
                                      unitQuadVBO.get(), unitQuadIBO.get());

        // Populate SceneView::decalDraws from ECS entities.
        render::decalRenderSystem(world, scene);

        // Sort transparent draws back-to-front before submission.
        // Explicit call per the spec's "Explicit over Implicit" principle.
        render::sortTransparentDraws(scene);

        scene.view      = view;
        scene.proj      = proj;
        // On frame 0 prevView/prevProj are identity, which produces enormous motion
        // vectors and lets stale GPU memory contaminate the TAA history.  Use the
        // current-frame matrices instead so the first frame has zero motion delta.
        scene.prevView  = (frameIdx == 0u) ? view : prevView;
        scene.prevProj  = (frameIdx == 0u) ? proj : prevProj;
        scene.cameraPos = eye;
        scene.cameraDir = glm::normalize(target - eye);
        scene.time      = time;
        scene.deltaTime = dt;
        scene.frameIndex = frameIdx;

        // Interior — sun does not penetrate a closed room
        scene.sunIntensity = 0.0f;
        // Dim ambient so the spotlight shadow reads clearly.
        scene.ambientColor = glm::vec3(0.12f, 0.11f, 0.10f);

        // Sector 0: low-intensity fill so walls stay readable without washing out.
        scene.pointLights.push_back({
            glm::vec3(0.0f, 3.5f, 0.0f),   // centre of sector 0
            14.0f,
            glm::vec3(1.0f, 0.94f, 0.82f),
            1.5f                            // reduced from 3.5 — let the spot dominate
        });

        // Sector 1: fill light so the adjacent room stays visible.
        scene.pointLights.push_back({
            glm::vec3(9.0f, 2.5f, 0.0f),
            10.0f,
            glm::vec3(0.88f, 0.92f, 1.00f),
            1.5f
        });

        // Box spotlight: mounted near the ceiling of sector 0, aimed straight
        // down at the test block.  Casts a hard shadow on the floor.
        scene.spotLights.push_back({
            glm::vec3(0.0f, 3.9f, 0.0f),        // just below sector-0 ceiling (y=4)
            glm::vec3(0.0f, -1.0f, 0.0f),        // straight down
            glm::radians(15.0f),                  // inner cone
            glm::radians(30.0f),                  // outer cone
            7.0f,                                 // range — reaches the floor
            glm::vec3(1.0f, 0.96f, 0.88f),        // warm white
            10.0f                                 // intensity
        });

        // Portal accent: highlights the doorway from the sector-0 side.
        scene.spotLights.push_back({
            glm::vec3(-3.0f, 3.8f, 0.0f),
            glm::normalize(glm::vec3(8.0f, -3.8f, 0.0f)),
            glm::radians(12.0f),
            glm::radians(28.0f),
            14.0f,
            glm::vec3(1.0f, 0.95f, 0.80f),
            4.0f                                  // reduced from 8.0
        });

        // ── Render ────────────────────────────────────────────────────────────
        renderer.renderFrame(*device, *queue, *swapchain, scene, swapW, swapH);

        // ── Save previous frame matrices ──────────────────────────────────────
        prevView = view;
        prevProj = proj;
        ++frameIdx;
    }

    // ─── Cleanup ──────────────────────────────────────────────────────────────

    swapchain.reset();
    queue.reset();
    device.reset();

    SDL_DestroyWindow(window);
    SDL_Quit();

    std::printf("[Daedalus] Clean shutdown.\n");
    return 0;
}
