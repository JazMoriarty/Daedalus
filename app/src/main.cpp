// Daedalus Engine — Application Entry Point
// Phase 3: DaedalusWorld — .dmap format, sector graph, portal traversal.

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

// ─── Example components (ECS smoke test) ────────────────────────────────────────────

struct TransformComponent
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct VelocityComponent
{
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
};

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

    // ─── ECS smoke test ───────────────────────────────────────────────────────

    {
        World world;
        EntityId e1 = world.createEntity();
        EntityId e2 = world.createEntity();

        world.addComponent(e1, TransformComponent{ 1.0f, 2.0f, 3.0f });
        world.addComponent(e1, VelocityComponent{ 0.1f, 0.0f, 0.0f });
        world.addComponent(e2, TransformComponent{ 5.0f, 0.0f, 0.0f });

        world.each<TransformComponent, VelocityComponent>(
            [](EntityId, TransformComponent& t, VelocityComponent& v)
            {
                t.x += v.vx;
                t.y += v.vy;
                t.z += v.vz;
            });

        DAEDALUS_ASSERT(world.getComponent<TransformComponent>(e1).x > 1.0f,
                        "ECS transform update failed");
        std::printf("[Daedalus] ECS smoke test passed.\n");
    }

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

    // ─── Test block ───────────────────────────────────────────────────────────
    // 1.5 × 1.5 × 1.5 unit box sitting on the floor at the centre of sector 0.

    const render::MeshData boxMesh = makeBoxMesh(0.0f, 0.0f, 1.5f, 0.75f, 0.75f);

    std::unique_ptr<rhi::IBuffer> boxVBO;
    std::unique_ptr<rhi::IBuffer> boxIBO;
    render::MeshDraw               boxDraw;

    {
        rhi::BufferDescriptor d;
        d.size     = boxMesh.vertices.size() * sizeof(render::StaticMeshVertex);
        d.usage    = rhi::BufferUsage::Vertex;
        d.initData = boxMesh.vertices.data();
        d.debugName = "BoxVBO";
        boxVBO = device->createBuffer(d);
    }
    {
        rhi::BufferDescriptor d;
        d.size     = boxMesh.indices.size() * sizeof(u32);
        d.usage    = rhi::BufferUsage::Index;
        d.initData = boxMesh.indices.data();
        d.debugName = "BoxIBO";
        boxIBO = device->createBuffer(d);
    }

    boxDraw.vertexBuffer       = boxVBO.get();
    boxDraw.indexBuffer        = boxIBO.get();
    boxDraw.indexCount         = static_cast<u32>(boxMesh.indices.size());
    boxDraw.modelMatrix        = glm::mat4(1.0f);
    boxDraw.prevModel          = glm::mat4(1.0f);
    boxDraw.material.albedo    = wallAlbedo.get();
    boxDraw.material.normalMap = wallNormal.get();
    boxDraw.material.roughness = 0.75f;
    boxDraw.material.metalness = 0.0f;

    std::printf("[Daedalus] Test block created (%zu verts, %zu indices).\n",
                boxMesh.vertices.size(), boxMesh.indices.size());

    // Camera sector tracking — updated each frame.
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
        bool sector0Visible = false;
        for (const auto& vs : visibleSectors)
        {
            if (vs.sectorId < sectorDraws.size() &&
                sectorDraws[vs.sectorId].vertexBuffer != nullptr)
            {
                scene.meshDraws.push_back(sectorDraws[vs.sectorId]);
            }
            if (vs.sectorId == 0u) { sector0Visible = true; }
        }

        // Include the test block whenever sector 0 is visible.
        if (sector0Visible) { scene.meshDraws.push_back(boxDraw); }

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
