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
#include "daedalus/world/dlevel_io.h"

#include "daedalus/audio/audio_system.h"
#include "daedalus/audio/i_audio_engine.h"

#include "daedalus/script/i_script_engine.h"
#include "daedalus/script/components/script_component.h"
#include "daedalus/audio/components/sound_emitter_component.h"

#include "daedalus/physics/i_physics_world.h"
#include "daedalus/physics/physics_types.h"
#include "daedalus/core/components/transform_component.h"
#include "daedalus/core/components/character_controller_component.h"
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
#include "daedalus/render/components/particle_emitter_component.h"
#include "daedalus/render/systems/particle_render_system.h"
#include "daedalus/render/particle_pool.h"
#include "daedalus/render/mipmap_generator.h"

#include <SDL3/SDL.h>
#include <CoreGraphics/CoreGraphics.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>

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

// ─── Mirror quad mesh factory ────────────────────────────────────────────────
// Single inward-facing vertical quad for the south-wall planar mirror demo:
//   Position : z = -5, x ∈ [-3, 3], y ∈ [0, 3]  (south wall of sector 0)
//   Normal   : (0, 0, +1) — faces inward toward the sector interior.
//   Winding  : (0,2,1),(0,3,2) — matches the sector-wall tessellator convention.
//   UV       : [0,1] maps the full mirror render target across the surface.

static render::MeshData makeMirrorQuadMesh()
{
    using render::StaticMeshVertex;
    render::MeshData mesh;
    auto& V = mesh.vertices;
    auto& I = mesh.indices;

    auto v = [](float px, float py, float pz,
                float u, float vv) noexcept -> StaticMeshVertex
    {
        StaticMeshVertex s{};
        s.pos[0]=px;      s.pos[1]=py;      s.pos[2]=pz;
        s.normal[0]=0.f;  s.normal[1]=0.f;  s.normal[2]=1.f;  // inward +Z
        s.uv[0]=u;        s.uv[1]=vv;
        s.tangent[0]=1.f; s.tangent[1]=0.f; s.tangent[2]=0.f; s.tangent[3]=1.f;
        return s;
    };

    // v0=BL, v1=BR, v2=TR, v3=TL; winding (0,2,1),(0,3,2)
    // Offset 0.01 units toward +Z (into the room) so the quad sits fractionally
    // in front of the south wall geometry and avoids z-fighting.
    //
    // UV convention: Metal texture v=0 is the TOP of the image (NDC y=+1).
    // The reflected RT stores the top of the mirror scene at v=0 and the bottom
    // at v=1.  Bottom corners (world y=0) must sample v=1; top corners (y=3) v=0.
    V.push_back(v(-3.f, 0.f, -4.99f,  0.f, 1.f));  // BL — bottom → v=1
    V.push_back(v( 3.f, 0.f, -4.99f,  1.f, 1.f));  // BR — bottom → v=1
    V.push_back(v( 3.f, 3.f, -4.99f,  1.f, 0.f));  // TR — top    → v=0
    V.push_back(v(-3.f, 3.f, -4.99f,  0.f, 0.f));  // TL — top    → v=0
    I.push_back(0u); I.push_back(2u); I.push_back(1u);
    I.push_back(0u); I.push_back(3u); I.push_back(2u);

    return mesh;
}

// ─── Entry point ──────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
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

    // ─── .dlevel game loop ────────────────────────────────────────────────────
    // When DaedalusApp is launched with a .dlevel path (e.g. from F5 in
    // DaedalusEdit) we enter a self-contained first-person runtime that never
    // falls through to the demo/test-map code below.

    const bool isDlevel = (argc >= 2 && argv[1] != nullptr &&
        std::filesystem::path(argv[1]).extension() == ".dlevel");

    // Render settings passed by the editor at launch time so the app starts with
    // exactly the same configuration as the editor's Render Settings panel.
    //
    // Flags recognised:
    //   --rt                 Start in ray-traced mode (F6 toggles at runtime)
    //   --rt-bounces=N       GI bounce count (default: 2)
    //   --rt-spp=N           Samples per pixel (default: 1)
    //   --rt-nodenoise       Disable SVGF denoiser
    //   --fog                Enable volumetric fog
    //   --ssr                Enable screen-space reflections
    //   --dof                Enable depth of field
    //   --nomotionblur       Disable motion blur (enabled by default in-app)
    //   --nocolorgrade       Disable colour grading (enabled by default in-app)
    //   --nooptfx            Disable vignette/grain/CA (enabled by default in-app)
    //   --nofxaa             Disable FXAA (enabled by default in-app)
    bool     startInRTMode   = false;
    u32      startRTBounces  = 2u;
    u32      startRTSPP      = 1u;
    bool     startRTDenoise  = true;
    bool     startFog        = false;
    bool     startSSR        = false;
    bool     startDoF        = false;
    bool     startMotionBlur = true;   // good game default
    bool     startColorGrade = true;   // uses identity LUT if no path provided
    bool     startOptFx      = true;   // mild vignette + grain
    bool     startFXAA       = true;

    // Helper: check if a string_view has a given prefix (C++17 compatible).
    auto hasPrefix = [](std::string_view s, std::string_view p) -> bool {
        return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
    };

    for (int i = 2; i < argc; ++i)
    {
        if (!argv[i]) continue;
        const std::string_view arg(argv[i]);

        if (arg == "--rt")                { startInRTMode   = true; }
        else if (hasPrefix(arg, "--rt-bounces="))
            startRTBounces = static_cast<u32>(std::atoi(argv[i] + 13));
        else if (hasPrefix(arg, "--rt-spp="))
            startRTSPP     = static_cast<u32>(std::atoi(argv[i] + 9));
        else if (arg == "--rt-nodenoise") { startRTDenoise  = false; }
        else if (arg == "--fog")          { startFog        = true;  }
        else if (arg == "--ssr")          { startSSR        = true;  }
        else if (arg == "--dof")          { startDoF        = true;  }
        else if (arg == "--nomotionblur") { startMotionBlur = false; }
        else if (arg == "--nocolorgrade") { startColorGrade = false; }
        else if (arg == "--nooptfx")      { startOptFx      = false; }
        else if (arg == "--nofxaa")       { startFXAA       = false; }
    }

    if (isDlevel)
    {
        // ── 1. Load .dlevel ───────────────────────────────────────────────────
        auto packResult = world::loadDlevel(std::filesystem::path(argv[1]));
        DAEDALUS_ASSERT(packResult.has_value(), "Failed to load .dlevel file");
        world::LevelPackData pack = std::move(*packResult);

        std::printf("[DLevel] Loaded '%s' — %zu sector(s), %zu light(s), %zu texture(s)\n",
                    argv[1],
                    pack.map.sectors.size(),
                    pack.lights.size(),
                    pack.textures.size());

        // ── 2. Upload textures to GPU ─────────────────────────────────────────
        // UUID → GPU texture. Missing UUIDs fall back to nullptr (engine default white).
        // Detect normal maps by UUID pattern (XORed with 0xDEADBEEF) and upload as linear.
        std::unordered_map<daedalus::UUID, std::unique_ptr<rhi::ITexture>, daedalus::UUIDHash>
            packTextures;

        for (auto& [uuid, ltex] : pack.textures)
        {
            if (ltex.pixels.empty()) { continue; }

            // Detect if this is a normal map by checking the UUID.
            // Normal map UUIDs are derived: originalUUID.lo XOR 0xDEADBEEF.
            // We check all possible material UUIDs to see if un-XORing gives a valid match.
            bool isNormalMap = false;
            UUID testAlbedoUuid = uuid;
            testAlbedoUuid.lo ^= 0xDEADBEEFull;
            // If un-XORing gives a UUID that's also in pack.textures, this is a normal map.
            if (pack.textures.find(testAlbedoUuid) != pack.textures.end())
                isNormalMap = true;

            // Generate mipmaps with proper filtering for both albedo and normal maps.
            // Normal maps use renormalized filtering to preserve vector magnitude.
            const render::MipmapChain mipChain = render::generateMipmapChain(
                ltex.pixels.data(), ltex.width, ltex.height, isNormalMap);

            rhi::TextureDescriptor td;
            td.width     = mipChain.width;
            td.height    = mipChain.height;
            td.mipLevels = mipChain.mipCount;
            td.format    = isNormalMap ? rhi::TextureFormat::RGBA8Unorm           // linear
                                       : rhi::TextureFormat::RGBA8Unorm_sRGB;     // sRGB
            td.usage     = rhi::TextureUsage::ShaderRead;
            td.initData  = mipChain.data.data();
            td.debugName = isNormalMap ? "dlevel_normal" : "dlevel_albedo";
            packTextures[uuid] = device->createTexture(td);

            if (isNormalMap)
                std::printf("[DLevel] Loaded normal map texture (%ux%u, %u mips, linear, renormalized)\n",
                            ltex.width, ltex.height, mipChain.mipCount);
        }

        std::printf("[DLevel] Uploaded %zu material texture(s) to GPU.\n",
                    packTextures.size());

        // ── 3+4. Tessellate map (tagged) and upload VBOs/IBOs ─────────────────
        // Returns: outer = per-sector, inner = per-material-batch.
        const std::vector<std::vector<world::TaggedMeshBatch>> sectorBatches =
            world::tessellateMapTagged(pack.map);
        const std::size_t numSectors = sectorBatches.size();

        // GPU buffer ownership: one vector-of-vectors mirroring sectorBatches.
        std::vector<std::vector<std::unique_ptr<rhi::IBuffer>>> sectorVBOs(numSectors);
        std::vector<std::vector<std::unique_ptr<rhi::IBuffer>>> sectorIBOs(numSectors);

        // Draw lists: one inner draw per batch, ready for portal submission.
        std::vector<std::vector<render::MeshDraw>> sectorDraws(numSectors);

        for (std::size_t si = 0; si < numSectors; ++si)
        {
            const auto& batches = sectorBatches[si];
            sectorVBOs[si].resize(batches.size());
            sectorIBOs[si].resize(batches.size());
            sectorDraws[si].resize(batches.size());

            for (std::size_t bi = 0; bi < batches.size(); ++bi)
            {
                const world::TaggedMeshBatch& batch = batches[bi];
                if (batch.mesh.vertices.empty()) { continue; }

                // VBO
                {
                    rhi::BufferDescriptor d;
                    d.size      = batch.mesh.vertices.size() * sizeof(render::StaticMeshVertex);
                    d.usage     = rhi::BufferUsage::Vertex;
                    d.initData  = batch.mesh.vertices.data();
                    d.debugName = "DL_VBO_" + std::to_string(si) + "_" + std::to_string(bi);
                    sectorVBOs[si][bi] = device->createBuffer(d);
                }
                // IBO
                {
                    rhi::BufferDescriptor d;
                    d.size      = batch.mesh.indices.size() * sizeof(u32);
                    d.usage     = rhi::BufferUsage::Index;
                    d.initData  = batch.mesh.indices.data();
                    d.debugName = "DL_IBO_" + std::to_string(si) + "_" + std::to_string(bi);
                    sectorIBOs[si][bi] = device->createBuffer(d);
                }

                // MeshDraw — bind textures if the UUIDs are in packTextures
                render::MeshDraw& draw = sectorDraws[si][bi];
                draw.vertexBuffer  = sectorVBOs[si][bi].get();
                draw.indexBuffer   = sectorIBOs[si][bi].get();
                draw.indexCount    = static_cast<u32>(batch.mesh.indices.size());
                draw.modelMatrix   = glm::mat4(1.0f);
                draw.prevModel     = glm::mat4(1.0f);
                draw.material.roughness = 0.85f;
                draw.material.metalness = 0.0f;

                // Bake per-sector ambient (matches editor viewport_3d retessellate).
                // Global scene ambientColor is zeroed below to avoid double-counting.
                if (si < pack.map.sectors.size())
                {
                    const auto& sec = pack.map.sectors[si];
                    draw.material.sectorAmbient = sec.ambientColor * sec.ambientIntensity;
                    draw.material.isOutdoor =
                        world::hasFlag(sec.flags, world::SectorFlags::Outdoors);
                }

                // Look up albedo texture for this material UUID.
                const auto albedoIt = packTextures.find(batch.materialId);
                if (albedoIt != packTextures.end())
                    draw.material.albedo = albedoIt->second.get();
                // nullptr → engine-default white (no fallback needed here)

                // Look up normal map using derived UUID (same XOR pattern as editor packing).
                UUID normalUuid = batch.materialId;
                normalUuid.lo ^= 0xDEADBEEFull;
                const auto normalIt = packTextures.find(normalUuid);
                if (normalIt != packTextures.end())
                {
                    draw.material.normalMap = normalIt->second.get();
                    if (si == 0 && bi == 0)  // Log first batch only
                        std::printf("[DLevel] Bound normal map to sector %zu batch %zu\n", si, bi);
                }
            }
        }

        std::printf("[DLevel] Tessellated + uploaded %zu sector(s).\n", numSectors);

        // ── 5. Build world map + portal traversal ─────────────────────────────
        auto dlWorldMap       = world::makeWorldMap(std::move(pack.map));
        auto dlPortalTraversal = world::makePortalTraversal();

        // ── 6. Seed first-person camera from player start (if present) ─────────
        glm::vec3 fpCamPos  = glm::vec3(0.0f, 1.65f, 0.0f);  // standing eye height
        float     fpCamYaw   = 0.0f;
        float     fpCamPitch = 0.0f;

        if (dlWorldMap->data().sectors.empty() == false)
        {
            // If the loaded map has sectors, at least clamp y to standing height.
        }
        if (pack.playerStart.has_value())
        {
            const auto& ps = *pack.playerStart;
            fpCamPos  = glm::vec3(ps.position.x, ps.position.y + 1.65f, ps.position.z);
            fpCamYaw  = ps.yaw;
        }

        // ── 5b. Physics world — load level geometry ──────────────────────────
        auto dlPhysicsWorld = physics::makePhysicsWorld();
        {
            auto physResult = dlPhysicsWorld->loadLevel(dlWorldMap->data());
            DAEDALUS_ASSERT(physResult.has_value(), "[DLevel] Physics loadLevel failed");
        }
        std::printf("[DLevel] Physics world loaded.\n");

        // ── 5c. ECS World + player character entity ───────────────────────────
        daedalus::World dlWorld;

        const physics::CharacterDesc kPlayerDesc{};  // default capsule (0.35 r, 0.55 hh)

        // fpCamPos is the eye position; feet = eye − eyeHeight.
        const glm::vec3 playerFeetPos = glm::vec3(
            fpCamPos.x,
            fpCamPos.y - physics::kDefaultEyeHeight,
            fpCamPos.z
        );

        const EntityId playerEntity = dlWorld.createEntity();
        dlWorld.addComponent(playerEntity, daedalus::TransformComponent{
            .position = playerFeetPos
        });
        dlWorld.addComponent(playerEntity, daedalus::CharacterControllerComponent{
            .desc = kPlayerDesc
        });

        {
            auto charResult = dlPhysicsWorld->addCharacter(
                playerEntity, kPlayerDesc, playerFeetPos);
            DAEDALUS_ASSERT(charResult.has_value(), "[DLevel] addCharacter failed");
        }

        // ── 5d-setup. Asset loader + shared unit quad for billboard sprites ───────
        auto dlAssetLoader = render::makeAssetLoader();

        const render::MeshData dlUnitQuadData = render::makeUnitQuadMesh();
        std::unique_ptr<rhi::IBuffer> dlUnitQuadVBO;
        std::unique_ptr<rhi::IBuffer> dlUnitQuadIBO;
        {
            rhi::BufferDescriptor d;
            d.size      = dlUnitQuadData.vertices.size() * sizeof(render::StaticMeshVertex);
            d.usage     = rhi::BufferUsage::Vertex;
            d.initData  = dlUnitQuadData.vertices.data();
            d.debugName = "DL_UnitQuadVBO";
            dlUnitQuadVBO = device->createBuffer(d);
        }
        {
            rhi::BufferDescriptor d;
            d.size      = dlUnitQuadData.indices.size() * sizeof(u32);
            d.usage     = rhi::BufferUsage::Index;
            d.initData  = dlUnitQuadData.indices.data();
            d.debugName = "DL_UnitQuadIBO";
            dlUnitQuadIBO = device->createBuffer(d);
        }

        // GPU resource ownership for placed entity visuals.
        // Indexed in insertion order; owned for the lifetime of the dlevel session.
        std::vector<std::unique_ptr<rhi::ITexture>>         dlEntityTextures;
        std::vector<std::unique_ptr<rhi::IBuffer>>          dlEntityBuffers;
        std::vector<std::unique_ptr<render::ParticlePool>>  dlEntityPools;

        // ── 5d. Register placed entities (physics + script + audio + visual) ────
        {
            u32 registered = 0u;
            for (const world::LevelEntity& ent : pack.entities)
            {
                const EntityId entityId = dlWorld.createEntity();

                // Build transform: position + yaw+pitch+roll rotation + visual scale.
                // Yaw rotates around Y, pitch around X, roll around Z.
                const glm::quat dlRot =
                    glm::angleAxis(ent.yaw,         glm::vec3(0.f, 1.f, 0.f))
                    * glm::angleAxis(ent.visualPitch, glm::vec3(1.f, 0.f, 0.f))
                    * glm::angleAxis(ent.visualRoll,  glm::vec3(0.f, 0.f, 1.f));

                dlWorld.addComponent(entityId, daedalus::TransformComponent{
                    .position = ent.position,
                    .rotation = dlRot,
                    .scale    = ent.visualScale
                });

                // Physics body (optional; None = decorative / script-only)
                if (ent.shape != world::LevelCollisionShape::None)
                {
                    physics::RigidBodyDesc desc;
                    // LevelCollisionShape ordinals match physics::CollisionShape exactly.
                    desc.shape       = static_cast<physics::CollisionShape>(ent.shape);
                    desc.halfExtents = ent.halfExtents;
                    desc.radius      = ent.halfExtents.x;  // Capsule: x = radius
                    desc.halfHeight  = ent.halfExtents.y;  // Capsule: y = halfHeight
                    desc.mass        = ent.mass;
                    desc.isStatic    = !ent.dynamic;
                    desc.initialRot  = dlRot;

                    auto rbResult = dlPhysicsWorld->addRigidBody(entityId, desc, ent.position);
                    if (!rbResult.has_value())
                    {
                        std::fprintf(stderr,
                            "[DLevel] Warning: physics body creation failed for entity '%s'\n",
                            ent.name.c_str());
                    }
                    else
                    {
                        ++registered;
                    }
                }

                // Script component (optional)
                if (!ent.scriptPath.empty())
                {
                    dlWorld.addComponent(entityId, script::ScriptComponent{
                        .scriptPath  = ent.scriptPath,
                        .exposedVars = ent.exposedVars,
                    });
                }

                // Sound emitter component (optional)
                if (!ent.soundPath.empty())
                {
                    dlWorld.addComponent(entityId, audio::SoundEmitterComponent{
                        .soundPath     = ent.soundPath,
                        .volume        = ent.soundVolume,
                        .falloffRadius = ent.soundFalloffRadius,
                        .looping       = ent.soundLoop,
                        .autoPlay      = ent.soundAutoPlay,
                    });
                }

                // Visual component (optional; skip if None or asset path missing)
                if (ent.visualType != world::LevelEntityVisualType::None)
                {
                    // Helper: load a texture from path, push to dlEntityTextures, return raw ptr.
                    // Returns nullptr on failure (non-fatal).
                    auto loadEntityTex = [&](const std::string& path, bool sRGB) -> rhi::ITexture*
                    {
                        if (path.empty()) { return nullptr; }
                        auto res = dlAssetLoader->loadTexture(*device,
                                                             std::filesystem::path(path), sRGB);
                        if (!res.has_value())
                        {
                            std::fprintf(stderr,
                                "[DLevel] Warning: failed to load texture '%s'\n", path.c_str());
                            return nullptr;
                        }
                        dlEntityTextures.push_back(std::move(*res));
                        return dlEntityTextures.back().get();
                    };

                    switch (ent.visualType)
                    {
                    case world::LevelEntityVisualType::BillboardCutout:
                    case world::LevelEntityVisualType::BillboardBlended:
                    {
                        rhi::ITexture* tex = loadEntityTex(ent.assetPath, true);
                        render::BillboardSpriteComponent bc;
                        bc.texture   = tex;
                        bc.size      = glm::vec2(ent.visualScale.x, ent.visualScale.y);
                        bc.alphaMode = (ent.visualType == world::LevelEntityVisualType::BillboardBlended)
                                           ? render::AlphaMode::Blended
                                           : render::AlphaMode::Cutout;
                        bc.tint      = ent.tint;
                        dlWorld.addComponent(entityId, std::move(bc));
                        break;
                    }
                    case world::LevelEntityVisualType::AnimatedBillboard:
                    case world::LevelEntityVisualType::RotatedSpriteSet:
                    {
                        // RotatedSpriteSet uses the animated path with direction count as columns;
                        // per-frame angle-driven column selection is a future enhancement.
                        rhi::ITexture* tex = loadEntityTex(ent.assetPath, true);
                        render::BillboardSpriteComponent bc;
                        bc.texture   = tex;
                        bc.size      = glm::vec2(ent.visualScale.x, ent.visualScale.y);
                        bc.alphaMode = render::AlphaMode::Blended;
                        bc.tint      = ent.tint;
                        dlWorld.addComponent(entityId, std::move(bc));

                        const u32 cols = (ent.visualType == world::LevelEntityVisualType::RotatedSpriteSet)
                                             ? ent.rotatedSpriteDirCount
                                             : ent.animCols;
                        render::AnimationStateComponent asc;
                        asc.frameCount = (cols > 0u) ? cols : 1u;
                        asc.rowCount   = (ent.animRows > 0u) ? ent.animRows : 1u;
                        asc.fps        = ent.animFrameRate;
                        asc.loop       = true;
                        dlWorld.addComponent(entityId, std::move(asc));
                        break;
                    }
                    case world::LevelEntityVisualType::StaticMesh:
                    {
                        if (ent.assetPath.empty()) { break; }
                        auto meshRes = dlAssetLoader->loadMesh(
                            std::filesystem::path(ent.assetPath));
                        if (!meshRes.has_value())
                        {
                            std::fprintf(stderr,
                                "[DLevel] Warning: failed to load mesh '%s'\n",
                                ent.assetPath.c_str());
                            break;
                        }
                        const render::MeshData& md = *meshRes;
                        rhi::BufferDescriptor vbd;
                        vbd.size      = md.vertices.size() * sizeof(render::StaticMeshVertex);
                        vbd.usage     = rhi::BufferUsage::Vertex;
                        vbd.initData  = md.vertices.data();
                        vbd.debugName = "DL_Mesh_VBO";
                        dlEntityBuffers.push_back(device->createBuffer(vbd));
                        rhi::IBuffer* vbo = dlEntityBuffers.back().get();

                        rhi::BufferDescriptor ibd;
                        ibd.size      = md.indices.size() * sizeof(u32);
                        ibd.usage     = rhi::BufferUsage::Index;
                        ibd.initData  = md.indices.data();
                        ibd.debugName = "DL_Mesh_IBO";
                        dlEntityBuffers.push_back(device->createBuffer(ibd));
                        rhi::IBuffer* ibo = dlEntityBuffers.back().get();

                        // Load albedo texture if the glTF material provided a path.
                        rhi::ITexture* meshAlbedo = nullptr;
                        if (!md.albedoPath.empty())
                        {
                            const std::filesystem::path texPath =
                                std::filesystem::path(ent.assetPath).parent_path() / md.albedoPath;
                            meshAlbedo = loadEntityTex(texPath.string(), /*sRGB=*/true);
                        }

                        render::StaticMeshComponent smc;
                        smc.vertexBuffer       = vbo;
                        smc.indexBuffer        = ibo;
                        smc.indexCount         = static_cast<u32>(md.indices.size());
                        smc.material.albedo    = meshAlbedo;
                        smc.material.roughness = 0.85f;
                        smc.material.metalness = 0.0f;
                        dlWorld.addComponent(entityId, std::move(smc));
                        break;
                    }
                    case world::LevelEntityVisualType::VoxelObject:
                    {
                        if (ent.assetPath.empty()) { break; }
                        auto voxRes = dlAssetLoader->loadVox(
                            std::filesystem::path(ent.assetPath));
                        if (!voxRes.has_value())
                        {
                            std::fprintf(stderr,
                                "[DLevel] Warning: failed to load vox '%s'\n",
                                ent.assetPath.c_str());
                            break;
                        }
                        const render::MeshData& vmd = voxRes->mesh;

                        rhi::TextureDescriptor ptd;
                        ptd.width     = 256u;
                        ptd.height    = 1u;
                        ptd.format    = rhi::TextureFormat::RGBA8Unorm;
                        ptd.usage     = rhi::TextureUsage::ShaderRead;
                        ptd.initData  = voxRes->paletteRGBA.data();
                        ptd.debugName = "DL_VoxPalette";
                        dlEntityTextures.push_back(device->createTexture(ptd));
                        rhi::ITexture* paletteTex = dlEntityTextures.back().get();

                        rhi::BufferDescriptor vbd;
                        vbd.size      = vmd.vertices.size() * sizeof(render::StaticMeshVertex);
                        vbd.usage     = rhi::BufferUsage::Vertex;
                        vbd.initData  = vmd.vertices.data();
                        vbd.debugName = "DL_Vox_VBO";
                        dlEntityBuffers.push_back(device->createBuffer(vbd));
                        rhi::IBuffer* vbo = dlEntityBuffers.back().get();

                        rhi::BufferDescriptor ibd;
                        ibd.size      = vmd.indices.size() * sizeof(u32);
                        ibd.usage     = rhi::BufferUsage::Index;
                        ibd.initData  = vmd.indices.data();
                        ibd.debugName = "DL_Vox_IBO";
                        dlEntityBuffers.push_back(device->createBuffer(ibd));
                        rhi::IBuffer* ibo = dlEntityBuffers.back().get();

                        render::VoxelObjectComponent voc;
                        voc.vertexBuffer    = vbo;
                        voc.indexBuffer     = ibo;
                        voc.indexCount      = static_cast<u32>(vmd.indices.size());
                        voc.material.albedo = paletteTex;
                        voc.material.roughness = 1.0f;
                        voc.material.metalness = 0.0f;
                        dlWorld.addComponent(entityId, std::move(voc));
                        break;
                    }
                    case world::LevelEntityVisualType::Decal:
                    {
                        rhi::ITexture* albedo = loadEntityTex(ent.assetPath, false);
                        rhi::ITexture* normal = loadEntityTex(ent.decalNormalPath, false);
                        if (!albedo) { break; }

                        render::DecalComponent dc;
                        dc.albedoTexture = albedo;
                        dc.normalTexture = normal;
                        dc.roughness     = ent.decalRoughness;
                        dc.metalness     = ent.decalMetalness;
                        dc.opacity       = ent.decalOpacity;
                        dlWorld.addComponent(entityId, std::move(dc));
                        break;
                    }
                    case world::LevelEntityVisualType::ParticleEmitter:
                    {
                        rhi::ITexture* atlas = loadEntityTex(ent.assetPath, false);
                        constexpr u32 kDefaultPoolSize = 512u;
                        dlEntityPools.push_back(
                            render::createParticlePool(*device, kDefaultPoolSize));
                        render::ParticlePool* pool = dlEntityPools.back().get();

                        render::ParticleEmitterComponent pec;
                        pec.pool              = pool;
                        pec.atlasTexture      = atlas;
                        pec.emissionRate      = ent.particleEmissionRate;
                        pec.emitDir           = ent.particleEmitDir;
                        pec.coneHalfAngle     = ent.particleConeHalfAngle;
                        pec.speedMin          = ent.particleSpeedMin;
                        pec.speedMax          = ent.particleSpeedMax;
                        pec.lifetimeMin       = ent.particleLifetimeMin;
                        pec.lifetimeMax       = ent.particleLifetimeMax;
                        pec.colorStart        = ent.particleColorStart;
                        pec.colorEnd          = ent.particleColorEnd;
                        pec.sizeStart         = ent.particleSizeStart;
                        pec.sizeEnd           = ent.particleSizeEnd;
                        pec.drag              = ent.particleDrag;
                        pec.gravity           = ent.particleGravity;
                        pec.emissiveScale     = ent.particleEmissiveScale;
                        pec.emissiveStart     = ent.particleEmissiveStart;
                        pec.emissiveEnd       = ent.particleEmissiveEnd;
                        pec.turbulenceScale   = ent.particleTurbulenceScale;
                        pec.velocityStretch   = ent.particleVelocityStretch;
                        pec.softRange         = ent.particleSoftRange;
                        pec.atlasGridSize     = glm::vec2(static_cast<float>(ent.particleAtlasCols),
                                                          static_cast<float>(ent.particleAtlasRows));
                        pec.atlasFrameRate    = ent.particleAtlasFrameRate;
                        pec.emitsLight        = ent.particleEmitsLight;
                        pec.shadowDensity     = ent.particleShadowDensity;
                        dlWorld.addComponent(entityId, std::move(pec));
                        break;
                    }
                    default: break;
                    } // switch visualType
                } // if visualType != None
            }
            std::printf("[DLevel] Registered %u placed entity physics body/ies.\n", registered);
        }

        // ── 5e. Audio engine + AudioSystem ───────────────────────────────────
        std::unique_ptr<audio::IAudioEngine> dlAudioEngine;
        audio::AudioSystem                   dlAudioSystem;
        {
            auto audioResult = audio::makeAudioEngine();
            if (audioResult.has_value())
            {
                dlAudioEngine = std::move(*audioResult);
                std::printf("[DLevel] Audio engine initialised.\n");
            }
            else
            {
                // Non-fatal: run without audio (headless / no device).
                std::fprintf(stderr, "[DLevel] Warning: audio device unavailable "
                                     "(no sound will be played).\n");
            }
        }

        // ── 5f. Script engine ─────────────────────────────────────────────────
        auto dlScriptEngine = script::makeScriptEngine();
        std::printf("[DLevel] Script engine initialised (Lua 5.4).\n");

        // ── 5g. Load and bind scripts for all scripted entities ───────────────
        std::vector<script::ScriptHandle> dlScriptHandles;
        {
            u32 bound = 0u;
            dlWorld.each<script::ScriptComponent>(
                [&](daedalus::EntityId eid, const script::ScriptComponent& sc)
                {
                    auto result = dlScriptEngine->loadScript(sc.scriptPath);
                    if (!result.has_value())
                    {
                        std::fprintf(stderr,
                            "[DLevel] Warning: failed to load script '%s': %s\n",
                            sc.scriptPath.string().c_str(),
                            result.error().message.c_str());
                        return;
                    }
                    const script::ScriptHandle handle = *result;
                    dlScriptEngine->bindEntity(handle, eid);
                    for (const auto& [k, v] : sc.exposedVars)
                        dlScriptEngine->setGlobal(handle, k, v);
                    dlScriptHandles.push_back(handle);
                    ++bound;
                });
            std::printf("[DLevel] Bound %u script(s).\n", bound);
        }

        // ── 7. Capture mouse for mouselook ────────────────────────────────
        // dlSwapW/H are declared here so they can be referenced from event
        // handlers; they are updated in WINDOW_RESIZED events.
        u32 dlSwapW = WINDOW_W;
        u32 dlSwapH = WINDOW_H;

        // Strategy: bypass SDL's relative-mouse-mode entirely.
        // SDL's Cocoa relative-mode path contains a title-bar filter in
        // Cocoa_HandleMouseEvent (SDL_cocoamouse.m ~line 514) that drops
        // mouse-moved events whenever the hidden cursor drifts into the
        // window chrome.  Because CGAssociateMouseAndMouseCursorPosition(false)
        // silently fails for NSTask-launched processes the cursor is free,
        // hits the title bar, and mouselook stalls.
        //
        // Manual approach:
        //   1. Hide cursor with SDL_HideCursor().
        //   2. Retry CGAssociateMouseAndMouseCursorPosition(false) every frame.
        //   3. Each frame, auto-detect which mode is active:
        //      A. Cursor frozen → CGAssociate working → use CGGetLastMouseDelta
        //         (raw HID input, bypasses macOS trackpad gesture recognition).
        //      B. Cursor free → CGAssociate failed → use position delta from
        //         SDL_GetGlobalMouseState, warp to centre when >200 px away.
        float dlMouseDeltaX = 0.0f;  // SDL event accumulators (diagnostic only)
        float dlMouseDeltaY = 0.0f;
        bool  dlMouseCaptured = false;
        bool  dlCGAssociateActive = false;  // true once CGAssociate(false) sticks
        float dlPrevGX = 0.0f, dlPrevGY = 0.0f;  // previous global cursor pos

        auto dlCaptureMouse = [&]()
        {
            if (dlMouseCaptured) return;
            SDL_HideCursor();
            int wx = 0, wy = 0;
            SDL_GetWindowPosition(window, &wx, &wy);
            const float cx = static_cast<float>(wx) + static_cast<float>(dlSwapW) * 0.5f;
            const float cy = static_cast<float>(wy) + static_cast<float>(dlSwapH) * 0.5f;
            CGWarpMouseCursorPosition(CGPointMake(cx, cy));
            CGAssociateMouseAndMouseCursorPosition(false);
            dlPrevGX = cx;
            dlPrevGY = cy;
            dlMouseCaptured = true;
        };

        auto dlReleaseMouse = [&]()
        {
            if (!dlMouseCaptured) return;
            CGAssociateMouseAndMouseCursorPosition(true);
            SDL_ShowCursor();
            dlMouseCaptured = false;
        };

        SDL_RaiseWindow(window);
        dlCaptureMouse();

        // ── Diagnostic log ────────────────────────────────────────────────────
        std::ofstream dlLog("/tmp/daedalus_mouselook.log", std::ios::trunc);
        dlLog << "=== Daedalus Mouselook Diagnostic (manual capture) ===\n"
              << "Window flags: 0x" << std::hex << SDL_GetWindowFlags(window) << std::dec
              << "\n\n";
        dlLog.flush();
        u32 dlLogMouseEvents = 0;

        // ── 8. First-person game loop ─────────────────────────────────────────
        using FPClock = std::chrono::steady_clock;
        auto fpPrevTP = FPClock::now();

        u32 fpFrameIdx = 0;
        glm::mat4 fpPrevView(1.0f);
        glm::mat4 fpPrevProj(1.0f);

        bool fpRunning = true;
        bool fpRTMode  = startInRTMode;  // --rt flag sets initial mode; F6 toggles
        while (fpRunning)
        {
            // ── Events ────────────────────────────────────────────────────────
            // Retry CGAssociate + read CG delta BEFORE the event pump.
            int32_t cgDx = 0, cgDy = 0;
            if (dlMouseCaptured)
            {
                CGAssociateMouseAndMouseCursorPosition(false);
                CGGetLastMouseDelta(&cgDx, &cgDy);
            }

            float lookDx = 0.0f, lookDy = 0.0f;

            SDL_Event event;
            dlMouseDeltaX = 0.0f;
            dlMouseDeltaY = 0.0f;
            dlLogMouseEvents = 0;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    fpRunning = false;
                }
                else if (event.type == SDL_EVENT_KEY_DOWN &&
                         event.key.scancode == SDL_SCANCODE_ESCAPE)
                {
                    fpRunning = false;
                }
                else if (event.type == SDL_EVENT_KEY_DOWN &&
                         event.key.scancode == SDL_SCANCODE_F6)
                {
                    fpRTMode = !fpRTMode;
                    std::printf("[RT] Render mode: %s\n",
                                fpRTMode ? "RayTraced" : "Rasterized");
                }
                else if (event.type == SDL_EVENT_WINDOW_RESIZED)
                {
                    dlSwapW = static_cast<u32>(event.window.data1);
                    dlSwapH = static_cast<u32>(event.window.data2);
                    swapchain->resize(dlSwapW, dlSwapH);
                    renderer.resize(*device, dlSwapW, dlSwapH);
                }
                else if (event.type == SDL_EVENT_MOUSE_MOTION)
                {
                    dlMouseDeltaX += event.motion.xrel;
                    dlMouseDeltaY += event.motion.yrel;
                    ++dlLogMouseEvents;
                    if (fpFrameIdx < 600 && dlLog.is_open())
                    {
                        dlLog << "  evt xrel=" << event.motion.xrel
                              << " yrel=" << event.motion.yrel
                              << " x=" << event.motion.x
                              << " y=" << event.motion.y
                              << "\n";
                    }
                }
                else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                {
                    dlCaptureMouse();
                    if (fpFrameIdx < 600 && dlLog.is_open())
                        dlLog << "  FOCUS GAINED frame=" << fpFrameIdx << "\n";
                }
                else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                {
                    dlReleaseMouse();
                    if (fpFrameIdx < 600 && dlLog.is_open())
                        dlLog << "  FOCUS LOST frame=" << fpFrameIdx << "\n";
                }
            }
            if (!fpRunning) { break; }

            // ── Mouselook delta source ────────────────────────────────────────
            // A. CGAssociate(false) succeeded → cursor frozen → CG delta
            //    (raw HID, bypasses trackpad gesture recognition).
            // B. CGAssociate(false) failed → cursor free → position delta
            //    with infrequent warp to prevent hitting screen edges.
            if (dlMouseCaptured)
            {
                float gx = 0.0f, gy = 0.0f;
                SDL_GetGlobalMouseState(&gx, &gy);

                const float posDx = gx - dlPrevGX;
                const float posDy = gy - dlPrevGY;
                const bool cursorFrozen =
                    (std::abs(posDx) + std::abs(posDy) < 2.0f);

                if (cursorFrozen)
                {
                    // Mode A — cursor frozen, CGAssociate is working.
                    if (!dlCGAssociateActive)
                    {
                        // Transition frame: drain stale CG delta that may
                        // carry warp contamination from Mode B.
                        dlCGAssociateActive = true;
                        int32_t jx = 0, jy = 0;
                        CGGetLastMouseDelta(&jx, &jy);
                    }
                    else
                    {
                        lookDx = static_cast<float>(cgDx);
                        lookDy = static_cast<float>(cgDy);
                    }
                    // dlPrevGX/GY unchanged — cursor hasn't moved.
                }
                else
                {
                    // Mode B — cursor free, use position delta.
                    dlCGAssociateActive = false;
                    lookDx = posDx;
                    lookDy = posDy;
                    dlPrevGX = gx;
                    dlPrevGY = gy;

                    // Warp to window centre if cursor drifted too far.
                    int wx = 0, wy = 0;
                    SDL_GetWindowPosition(window, &wx, &wy);
                    const float ctrX = static_cast<float>(wx)
                                     + static_cast<float>(dlSwapW) * 0.5f;
                    const float ctrY = static_cast<float>(wy)
                                     + static_cast<float>(dlSwapH) * 0.5f;
                    const float offX = gx - ctrX;
                    const float offY = gy - ctrY;
                    if (offX * offX + offY * offY > 200.0f * 200.0f)
                    {
                        CGWarpMouseCursorPosition(CGPointMake(ctrX, ctrY));
                        dlPrevGX = ctrX;
                        dlPrevGY = ctrY;
                    }
                }
            }

            // ── Timing
            const auto fpNowTP = FPClock::now();
            const float fpDt   = std::chrono::duration<float>(fpNowTP - fpPrevTP).count();
            fpPrevTP = fpNowTP;

            // ── Mouselook ───────────────────────────────────────────────────
            {
                constexpr float kMouseSensitivity = 0.002f;
                constexpr float kMaxDelta = 200.0f;
                fpCamYaw   += std::clamp(lookDx, -kMaxDelta, kMaxDelta) * kMouseSensitivity;
                fpCamPitch -= std::clamp(lookDy, -kMaxDelta, kMaxDelta) * kMouseSensitivity;
                constexpr float kPitchMax = 1.3963f;
                if (fpCamPitch >  kPitchMax) { fpCamPitch =  kPitchMax; }
                if (fpCamPitch < -kPitchMax) { fpCamPitch = -kPitchMax; }
            }

            // ── Diagnostic: per-frame summary (first 600 frames) ─────────────
            if (fpFrameIdx < 600 && dlLog.is_open())
            {
                if (dlLogMouseEvents > 0 || lookDx != 0.0f || lookDy != 0.0f
                    || (fpFrameIdx % 60) == 0)
                {
                    dlLog << "F" << fpFrameIdx
                          << " dt=" << (fpDt * 1000.0f) << "ms"
                          << " evts=" << dlLogMouseEvents
                          << " lookDx=" << lookDx
                          << " lookDy=" << lookDy
                          << (dlCGAssociateActive ? " [CG]" : " [POS]")
                          << " cgDx=" << cgDx
                          << " cgDy=" << cgDy
                          << " gx=" << dlPrevGX
                          << " gy=" << dlPrevGY
                          << "\n";
                    dlLog.flush();
                }
            }

            // ── Camera direction ──────────────────────────────────────────────
            const glm::vec3 camDir = glm::normalize(glm::vec3(
                std::sin(fpCamYaw) * std::cos(fpCamPitch),
                std::sin(fpCamPitch),
                std::cos(fpCamYaw) * std::cos(fpCamPitch)
            ));
            const glm::vec3 up      = glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 right   = glm::normalize(glm::cross(up, camDir));

            // ── WASD movement → physics character controller ─────────────────────
            {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                constexpr float kMoveSpeed = 4.0f;  // m/s

                // Horizontal movement only; gravity is applied by the physics step.
                const glm::vec3 flatDir = glm::normalize(
                    glm::vec3(camDir.x, 0.0f, camDir.z));
                const glm::vec3 flatRight = glm::normalize(
                    glm::vec3(right.x, 0.0f, right.z));

                glm::vec3 desiredVel(0.0f);
                if (keys[SDL_SCANCODE_W]) { desiredVel += flatDir   * kMoveSpeed; }
                if (keys[SDL_SCANCODE_S]) { desiredVel -= flatDir   * kMoveSpeed; }
                if (keys[SDL_SCANCODE_A]) { desiredVel -= flatRight * kMoveSpeed; }
                if (keys[SDL_SCANCODE_D]) { desiredVel += flatRight * kMoveSpeed; }
                // Q/E vertical is removed: gravity handles Y movement.

                dlPhysicsWorld->setCharacterInput(playerEntity, desiredVel);
                dlPhysicsWorld->step(fpDt);
                dlPhysicsWorld->syncTransforms(dlWorld);

                // Camera eye position = character feet + standing eye height.
                fpCamPos = dlWorld.getComponent<daedalus::TransformComponent>(playerEntity).position
                         + glm::vec3(0.0f, physics::kDefaultEyeHeight, 0.0f);
            }

            // ── Audio ─────────────────────────────────────────────────────────
            if (dlAudioEngine)
            {
                dlAudioEngine->update();
                dlAudioSystem.update(*dlAudioEngine, dlWorld, fpCamPos, camDir, up);
            }

            // ── Scripts ───────────────────────────────────────────────────────
            dlScriptEngine->update(fpDt, dlWorld);

            // ── View + projection ─────────────────────────────────────────────
            const float fpAspect = static_cast<float>(dlSwapW) / static_cast<float>(dlSwapH);
            const glm::mat4 fpView = glm::lookAtLH(fpCamPos, fpCamPos + camDir, up);
            const glm::mat4 fpProj = glm::perspectiveLH_ZO(
                glm::radians(70.0f), fpAspect, 0.1f, 200.0f);

            // ── Portal traversal ──────────────────────────────────────────────
            world::SectorId dlCamSector =
                dlWorldMap->findSector({fpCamPos.x, fpCamPos.z});
            if (dlCamSector == world::INVALID_SECTOR_ID) { dlCamSector = 0u; }

            const glm::mat4 fpViewProj = fpProj * fpView;
            const auto dlVisibleSectors =
                dlPortalTraversal->traverse(*dlWorldMap, dlCamSector, fpViewProj);

            // ── Build SceneView ───────────────────────────────────────────────
            render::SceneView fpScene;

            // NDC → pixel helper (identical to viewport_3d.cpp scissor logic).
            const float fW = static_cast<float>(dlSwapW);
            const float fH = static_cast<float>(dlSwapH);

            for (const auto& vs : dlVisibleSectors)
            {
                if (vs.sectorId >= numSectors) { continue; }

                // Compute pixel scissor rect from NDC window min/max.
                const float px0f = (vs.windowMin.x + 1.0f) * 0.5f * fW;
                const float py0f = (1.0f - vs.windowMax.y) * 0.5f * fH;
                const float px1f = (vs.windowMax.x + 1.0f) * 0.5f * fW;
                const float py1f = (1.0f - vs.windowMin.y) * 0.5f * fH;

                rhi::ScissorRect sr;
                sr.x      = static_cast<u32>(std::max(0.0f, std::floor(px0f)));
                sr.y      = static_cast<u32>(std::max(0.0f, std::floor(py0f)));
                sr.width  = static_cast<u32>(std::min(fW, std::ceil(px1f)))
                            - sr.x;
                sr.height = static_cast<u32>(std::min(fH, std::ceil(py1f)))
                            - sr.y;

                const bool scissorValid = (sr.width > 0 && sr.height > 0);

                for (const auto& draw : sectorDraws[vs.sectorId])
                {
                    if (draw.vertexBuffer == nullptr) { continue; }

                    render::MeshDraw d = draw;
                    d.scissorValid = scissorValid;
                    d.scissorRect  = sr;
                    fpScene.meshDraws.push_back(std::move(d));
                }
            }

            // ── Lights from pack ──────────────────────────────────────────────
            for (const auto& ll : pack.lights)
            {
                if (ll.type == world::LevelLightType::Point)
                {
                    fpScene.pointLights.push_back({
                        ll.position,
                        ll.radius,
                        ll.color,
                        ll.intensity
                    });
                }
                else  // Spot
                {
                    render::SpotLight sl;
                    sl.position       = ll.position;
                    sl.direction      = ll.direction;
                    sl.innerConeAngle = ll.innerConeAngle;
                    sl.outerConeAngle = ll.outerConeAngle;
                    sl.range          = ll.range;
                    sl.color          = ll.color;
                    sl.intensity      = ll.intensity;
                    sl.castsShadows   = true;
                    fpScene.spotLights.push_back(sl);
                }
            }

            // ── Sun + ambient ─────────────────────────────────────────────────
            fpScene.sunDirection = pack.sun.direction;
            fpScene.sunColor     = pack.sun.color;
            fpScene.sunIntensity = pack.sun.intensity;
            // Per-sector ambient is baked into draw.material.sectorAmbient above;
            // zero the global ambient to avoid double-counting (mirrors editor).
            fpScene.ambientColor = glm::vec3(0.0f);

            // ── Camera ────────────────────────────────────────────────────────
            fpScene.view      = fpView;
            fpScene.proj      = fpProj;
            fpScene.prevView  = (fpFrameIdx == 0u) ? fpView : fpPrevView;
            fpScene.prevProj  = (fpFrameIdx == 0u) ? fpProj : fpPrevProj;
            fpScene.cameraPos = fpCamPos;
            fpScene.cameraDir = camDir;

            // ── Post-FX — driven by editor render settings passed via CLI args ──
            // Fog, SSR, DoF: off by default; enabled if --fog/--ssr/--dof was passed.
            fpScene.fog.enabled = startFog;
            fpScene.ssr.enabled = startSSR;
            fpScene.dof.enabled = startDoF;

            // Motion blur: on by default (good game feel); --nomotionblur disables.
            fpScene.motionBlur.enabled      = startMotionBlur;
            fpScene.motionBlur.shutterAngle = 0.25f;
            fpScene.motionBlur.numSamples   = 8u;

            // Colour grading: on by default with the identity LUT (passthrough);
            // --nocolorgrade disables entirely.
            fpScene.colorGrading.enabled    = startColorGrade;
            fpScene.colorGrading.intensity  = 1.0f;
            fpScene.colorGrading.lutTexture = nullptr;

            // Optional FX (vignette, film grain, CA): mild defaults; --nooptfx disables.
            fpScene.optionalFx.enabled          = startOptFx;
            fpScene.optionalFx.vignetteIntensity = 0.30f;
            fpScene.optionalFx.vignetteRadius    = 0.40f;
            fpScene.optionalFx.grainAmount       = 0.03f;
            fpScene.optionalFx.caAmount          = 0.0f;

            // FXAA: on by default; --nofxaa disables.
            fpScene.upscaling.mode = startFXAA
                ? render::UpscalingMode::FXAA
                : render::UpscalingMode::None;

            // RT mode (F6 toggles at runtime; --rt sets initial state)
            fpScene.renderMode = fpRTMode
                ? render::RenderMode::RayTraced
                : render::RenderMode::Rasterized;

            // RT sub-settings: bounces, SPP, denoiser — synced from editor panel.
            fpScene.rt.maxBounces      = startRTBounces;
            fpScene.rt.samplesPerPixel = startRTSPP;
            fpScene.rt.denoise         = startRTDenoise;

            // ── ECS entity rendering
            // spriteAnimationSystem advances UV offsets before billboard gather.
            render::spriteAnimationSystem(dlWorld, fpDt);
            render::meshRenderSystem(dlWorld, fpScene);
            render::voxelRenderSystem(dlWorld, fpScene);
            render::billboardRenderSystem(dlWorld, fpScene, fpView,
                                          dlUnitQuadVBO.get(), dlUnitQuadIBO.get());
            render::decalRenderSystem(dlWorld, fpScene);
            render::particleRenderSystem(dlWorld, fpScene, fpDt, fpFrameIdx);
            render::sortTransparentDraws(fpScene);

            // ── Render ────────────────────────────────────────────────────────
            renderer.renderFrame(*device, *queue, *swapchain, fpScene, dlSwapW, dlSwapH);

            fpPrevView = fpView;
            fpPrevProj = fpProj;
            ++fpFrameIdx;

            // ── Frame rate cap: ~60 fps ──────────────────────────────────
            // displaySyncEnabled = NO (metal_render_device.mm) decouples
            // nextDrawable from vsync; we cap here to keep per-frame mouse
            // deltas uniform and avoid over-driving the GPU.
            {
                using namespace std::chrono;
                constexpr nanoseconds kTargetFrame{16'666'667LL}; // ~60 fps
                const auto frameElapsed =
                    duration_cast<nanoseconds>(FPClock::now() - fpNowTP);
                if (frameElapsed < kTargetFrame)
                    SDL_DelayPrecise(static_cast<Uint64>(
                        (kTargetFrame - frameElapsed).count()));
            }
        }

        // ── 9. Cleanup and exit ─────────────────────────────────────────
        for (const script::ScriptHandle h : dlScriptHandles)
            dlScriptEngine->unloadScript(h);

        dlReleaseMouse();
        swapchain.reset();
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::printf("[DLevel] Clean shutdown.\n");
        return 0;
    }

    // ─── Asset setup ──────────────────────────────────────────────────────────────────────

    const std::string assetsDir  = platform->getExecutableDir() + "/assets";
    const std::string albedoPath = assetsDir + "/textures/wall_albedo.png";
    const std::string normalPath = assetsDir + "/textures/wall_normal.png";
    const std::string dmapPath   = assetsDir + "/maps/test_map.dmap";

    // Ensure asset subdirectories exist in the binary dir (created by the POST_BUILD
    // copy step, but guarded here so a first-run without a build step still works).
    (void)std::filesystem::create_directories(assetsDir + "/textures");
    (void)std::filesystem::create_directories(assetsDir + "/maps");

    // Generate test textures if they don't already exist.
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
    // Prefer a map path provided by the editor via argv[1].  If none is given,
    // build/load the hard-coded test map (generated once on first run).
    {
        const auto testMapPath = std::filesystem::path(dmapPath);
        if (!std::filesystem::exists(testMapPath))
        {
            const auto saveResult = world::saveDmap(buildTestMap(), testMapPath);
            DAEDALUS_ASSERT(saveResult.has_value(), "Failed to save test_map.dmap");
            std::printf("[Daedalus] Saved %s\n", dmapPath.c_str());

            // Also emit the .dmap.json alongside it for inspection.
            const auto jsonPath = std::filesystem::path(assetsDir + "/maps/test_map.dmap.json");
            (void)world::saveDmapJson(buildTestMap(), jsonPath);  // best-effort
        }
    }

    const std::filesystem::path dmapLoadPath = [&]() -> std::filesystem::path
    {
        if (argc >= 2 && argv[1] != nullptr)
        {
            const std::filesystem::path p(argv[1]);
            if (std::filesystem::exists(p))
            {
                std::printf("[Daedalus] Editor map: %s\n", p.string().c_str());
                return p;
            }
            std::printf("[Daedalus] Warning: map path '%s' not found; using test map.\n", argv[1]);
        }
        return std::filesystem::path(dmapPath);
    }();

    auto loadResult = world::loadDmap(dmapLoadPath);
    DAEDALUS_ASSERT(loadResult.has_value(), "Failed to load map");

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

    // Standalone MeshDraw for the mirror pre-pass: mirrors need their own draw-list
    // since the ECS entity is gathered per-frame by meshRenderSystem into the main pass.
    render::MeshDraw mirrorBoxDraw;
    mirrorBoxDraw.vertexBuffer       = boxVBO.get();
    mirrorBoxDraw.indexBuffer        = boxIBO.get();
    mirrorBoxDraw.indexCount         = static_cast<u32>(boxMesh.indices.size());
    mirrorBoxDraw.modelMatrix        = glm::mat4(1.0f);
    mirrorBoxDraw.prevModel          = glm::mat4(1.0f);
    mirrorBoxDraw.material.albedo    = wallAlbedo.get();
    mirrorBoxDraw.material.normalMap = wallNormal.get();
    mirrorBoxDraw.material.roughness = 0.75f;
    mirrorBoxDraw.material.metalness = 0.0f;

    // ─── Shared unit quad (billboard sprites)
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

    // ─── Deferred decal entities — realistic impact/damage marks ─────────────────────────
    // Two procedural 128×128 textures (RGBA8Unorm, linear colour space):
    //
    //  decalTexture       — charred albedo: near-black centre → reddish-brown ring.
    //                       Alpha fades smoothly to 0 at the disc edge.
    //
    //  decalNormalTexture — tangent-space crater normal map.  Normals tilt outward
    //                       on the crater walls (simulating a depression) and have a
    //                       subtle inward lip at the rim (displaced soil).
    //                       The deferred lighting pass reads this back through RT1
    //                       and shades accordingly — the crater depth is purely
    //                       a lighting illusion, no geometry is displaced.
    //
    // TBN used by decal.metal (identity-rotation decal):
    //   T  = model[0].xyz = world +X   (U increases along +X)
    //   B  = -model[2].xyz = world −Z  (V = 0.5−local.z → flip)
    //   N  = model[1].xyz = world +Y   (floor normal / projection axis)
    // Tangent-space flat normal (0,0,1) → world +Y.  Encoded (128,128,255).

    std::unique_ptr<rhi::ITexture> decalTexture;
    std::unique_ptr<rhi::ITexture> decalNormalTexture;
    {
        constexpr int DEC_W = 128, DEC_H = 128;
        std::vector<u8> albedoPixels(DEC_W * DEC_H * 4, 0u);
        std::vector<u8> normalPixels(DEC_W * DEC_H * 4, 0u);

        const float dcx    = (DEC_W - 1) * 0.5f;
        const float dcy    = (DEC_H - 1) * 0.5f;
        const float radius = DEC_W * 0.45f;  // disc fills ~90 % of the texture width

        for (int y = 0; y < DEC_H; ++y)
        {
            for (int x = 0; x < DEC_W; ++x)
            {
                const float ddx  = static_cast<float>(x) - dcx;
                const float ddy  = static_cast<float>(y) - dcy;
                const float dist = std::sqrt(ddx * ddx + ddy * ddy);
                const int   idx  = (y * DEC_W + x) * 4;

                const float r = dist / radius;  // 0 = centre, 1 = disc edge

                // ── Alpha ─────────────────────────────────────────────────────
                // Fully opaque inside the crater zone, quadratic fade at the edge.
                float alpha = 0.0f;
                if (r < 0.75f)
                    alpha = 0.94f;
                else if (r < 1.0f) {
                    const float t = (r - 0.75f) / 0.25f;
                    alpha = 0.94f * (1.0f - t * t);
                }
                const u8 alpha8 = static_cast<u8>(alpha * 255.0f + 0.5f);

                // ── Albedo ────────────────────────────────────────────────────
                // Scorched concentric zones (all values in linear space):
                //   r < 0.12  : near-black charred centre
                //   r < 0.35  : dark brown inner burnt ring
                //   r < 0.65  : reddish-brown scorched zone
                //   r < 1.00  : warm stone outer fade
                float ar, ag, ab;
                if (r < 0.12f)
                {
                    ar = 12.0f / 255.0f; ag = 9.0f / 255.0f; ab = 7.0f / 255.0f;
                }
                else if (r < 0.35f)
                {
                    const float t = (r - 0.12f) / 0.23f;
                    ar = (12.0f + t * 38.0f) / 255.0f;
                    ag = ( 9.0f + t * 20.0f) / 255.0f;
                    ab = ( 7.0f + t * 11.0f) / 255.0f;
                }
                else if (r < 0.65f)
                {
                    const float t = (r - 0.35f) / 0.30f;
                    ar = (50.0f + t * 40.0f) / 255.0f;
                    ag = (29.0f + t * 22.0f) / 255.0f;
                    ab = (18.0f + t * 12.0f) / 255.0f;
                }
                else
                {
                    const float t = (r - 0.65f) / 0.35f;
                    ar = (90.0f + t * 30.0f) / 255.0f;
                    ag = (51.0f + t * 18.0f) / 255.0f;
                    ab = (30.0f + t *  8.0f) / 255.0f;
                }

                albedoPixels[idx + 0] = static_cast<u8>(ar * 255.0f + 0.5f);
                albedoPixels[idx + 1] = static_cast<u8>(ag * 255.0f + 0.5f);
                albedoPixels[idx + 2] = static_cast<u8>(ab * 255.0f + 0.5f);
                albedoPixels[idx + 3] = alpha8;

                // ── Normal map ────────────────────────────────────────────────
                // Tangent-space normals.  Encoding: (n * 0.5 + 0.5) * 255.
                //   Flat (0,0,1) → (128,128,255).
                //
                // Crater wall zone [r: 0.12 → 0.55]:
                //   Bell-curve tilt (sin(π·wallT) * 0.55 rad ≈ 32° peak) outward
                //   from centre → the lighting gradient produces the depth illusion.
                // Rim zone [r: 0.55 → 0.75]:
                //   Slight negative tilt (sin(π·rimT) * 0.18 rad ≈ 10°) for a
                //   displaced-soil lip at the crater edge.
                float nx = 0.0f, ny = 0.0f, nz = 1.0f;

                if (r > 0.12f && dist > 0.001f)
                {
                    const float invDist = 1.0f / dist;
                    const float dirX    = ddx * invDist;  // tangent-T direction
                    const float dirY    = ddy * invDist;  // tangent-B direction

                    float tilt = 0.0f;
                    if (r < 0.55f)
                    {
                        const float wallT = (r - 0.12f) / 0.43f;
                        tilt = std::sin(wallT * 3.14159265f) * 0.55f;
                    }
                    else if (r < 0.75f)
                    {
                        const float rimT = (r - 0.55f) / 0.20f;
                        tilt = -std::sin(rimT * 3.14159265f) * 0.18f;
                    }

                    if (std::abs(tilt) > 0.0001f)
                    {
                        const float sinT = std::sin(tilt);
                        const float cosT = std::cos(tilt);
                        nx = sinT * dirX;
                        ny = sinT * dirY;
                        nz = cosT;
                    }
                }

                // Clamp-and-encode to [0, 255].
                auto enc = [](float v) -> u8
                {
                    return static_cast<u8>(std::fmax(0.0f,
                               std::fmin(255.0f, (v * 0.5f + 0.5f) * 255.0f + 0.5f)));
                };
                normalPixels[idx + 0] = enc(nx);
                normalPixels[idx + 1] = enc(ny);
                normalPixels[idx + 2] = enc(nz);
                normalPixels[idx + 3] = 255u;
            }
        }

        // Upload albedo texture.
        {
            rhi::TextureDescriptor td;
            td.width     = static_cast<u32>(DEC_W);
            td.height    = static_cast<u32>(DEC_H);
            td.format    = rhi::TextureFormat::RGBA8Unorm;
            td.usage     = rhi::TextureUsage::ShaderRead;
            td.initData  = albedoPixels.data();
            td.debugName = "decal_damage_albedo";
            decalTexture = device->createTexture(td);
            DAEDALUS_ASSERT(decalTexture != nullptr, "Failed to create decal albedo texture");
        }

        // Upload normal map texture.
        {
            rhi::TextureDescriptor td;
            td.width     = static_cast<u32>(DEC_W);
            td.height    = static_cast<u32>(DEC_H);
            td.format    = rhi::TextureFormat::RGBA8Unorm;
            td.usage     = rhi::TextureUsage::ShaderRead;
            td.initData  = normalPixels.data();
            td.debugName = "decal_damage_normal";
            decalNormalTexture = device->createTexture(td);
            DAEDALUS_ASSERT(decalNormalTexture != nullptr, "Failed to create decal normal texture");
        }
    }

    // Three floor impact marks at different positions in sectors 0 and 1.
    // TransformComponent: position = OBB centre; scale = OBB full extents.
    // Decal centre must be ON the floor (y=0) so the thin Y extent straddles the
    // floor plane and the fragment shader catches floor fragments.
    {
        // Decal 1 — centre of sector 0 (directly below the spotlight).
        TransformComponent t1;
        t1.position = glm::vec3( 0.0f, 0.0f,  0.0f);
        t1.scale    = glm::vec3( 2.0f, 0.30f,  2.0f);

        render::DecalComponent d1;
        d1.albedoTexture = decalTexture.get();
        d1.normalTexture = decalNormalTexture.get();  // crater normal map
        d1.roughness     = 0.95f;  // charred surface is very matte
        d1.metalness     = 0.0f;
        d1.opacity       = 0.90f;

        EntityId dec1 = world.createEntity();
        world.addComponent(dec1, t1);
        world.addComponent(dec1, std::move(d1));
    }
    {
        // Decal 2 — near the box, slightly offset.
        TransformComponent t2;
        t2.position = glm::vec3(-2.0f, 0.0f,  2.0f);
        t2.scale    = glm::vec3( 1.5f, 0.30f,  1.5f);

        render::DecalComponent d2;
        d2.albedoTexture = decalTexture.get();
        d2.normalTexture = decalNormalTexture.get();
        d2.roughness     = 0.92f;
        d2.metalness     = 0.0f;
        d2.opacity       = 0.88f;

        EntityId dec2 = world.createEntity();
        world.addComponent(dec2, t2);
        world.addComponent(dec2, std::move(d2));
    }
    {
        // Decal 3 — sector 1 floor.
        TransformComponent t3;
        t3.position = glm::vec3( 9.0f, 0.0f,  0.0f);
        t3.scale    = glm::vec3( 1.8f, 0.30f,  1.8f);

        render::DecalComponent d3;
        d3.albedoTexture = decalTexture.get();
        d3.normalTexture = decalNormalTexture.get();
        d3.roughness     = 0.93f;
        d3.metalness     = 0.0f;
        d3.opacity       = 0.90f;

        EntityId dec3 = world.createEntity();
        world.addComponent(dec3, t3);
        world.addComponent(dec3, std::move(d3));
    }

    std::printf("[Daedalus] Deferred decal entities created (3 impact marks, with crater normal maps).\n");

    // ─── Particle emitters: flame and sparks ────────────────────────────────────
    // Two procedural atlas textures, two ParticlePools, two ECS entities.
    //
    // Flame  — warm organic fire at (0, 0.1, 0): strong curl turbulence, soft
    //          glowing billboards that bloom under the spotlight.
    // Sparks — fast omnidirectional sparks at (-2, 0.5, 2): velocity-stretched
    //          white-hot streaks with full gravity and short lifetime.

    // ── Flame atlas: 32×32 single-cell radial gradient ─────────────────────────
    // White-hot centre fading to transparent edge; rendered in HDR so bloom fires.
    std::unique_ptr<rhi::ITexture> flameAtlas;
    {
        constexpr int W = 32, H = 32;
        std::vector<u8> px(W * H * 4, 0u);
        const float cx = (W - 1) * 0.5f, cy = (H - 1) * 0.5f;
        const float r  = cx * 0.9f;
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const float d = std::sqrt((x-cx)*(x-cx) + (y-cy)*(y-cy)) / r;
                if (d >= 1.0f) { continue; }
                // Falloff: smoother quad curve
                const float t = 1.0f - d * d;
                const int i   = (y * W + x) * 4;
                // Core: near-white; outer: warm yellow
                px[i+0] = static_cast<u8>(std::min(255.f, (0.8f + 0.2f * t) * 255.f + 0.5f)); // R
                px[i+1] = static_cast<u8>(std::min(255.f, (0.5f + 0.3f * t) * 255.f + 0.5f)); // G
                px[i+2] = static_cast<u8>(std::min(255.f,        0.1f * t   * 255.f + 0.5f)); // B
                px[i+3] = static_cast<u8>(std::min(255.f,              t    * 255.f + 0.5f)); // A
            }
        }
        rhi::TextureDescriptor td;
        td.width = W; td.height = H;
        td.format    = rhi::TextureFormat::RGBA8Unorm;
        td.usage     = rhi::TextureUsage::ShaderRead;
        td.initData  = px.data();
        td.debugName = "particle_flame_atlas";
        flameAtlas = device->createTexture(td);
        DAEDALUS_ASSERT(flameAtlas, "Failed to create flame atlas");
    }

    // ── Sparks atlas: 8×8 sharp bright dot ────────────────────────────────────
    std::unique_ptr<rhi::ITexture> sparksAtlas;
    {
        constexpr int W = 8, H = 8;
        std::vector<u8> px(W * H * 4, 0u);
        const float cx = (W - 1) * 0.5f, cy = (H - 1) * 0.5f;
        const float r  = cx * 0.8f;
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const float d = std::sqrt((x-cx)*(x-cx) + (y-cy)*(y-cy)) / r;
                if (d >= 1.0f) { continue; }
                const float t = 1.0f - d;
                const int i   = (y * W + x) * 4;
                px[i+0] = 255u;  // pure white — emissiveScale carries the HDR brightness
                px[i+1] = 255u;
                px[i+2] = 255u;
                px[i+3] = static_cast<u8>(std::min(255.f, t * 255.f + 0.5f));
            }
        }
        rhi::TextureDescriptor td;
        td.width = W; td.height = H;
        td.format    = rhi::TextureFormat::RGBA8Unorm;
        td.usage     = rhi::TextureUsage::ShaderRead;
        td.initData  = px.data();
        td.debugName = "particle_sparks_atlas";
        sparksAtlas = device->createTexture(td);
        DAEDALUS_ASSERT(sparksAtlas, "Failed to create sparks atlas");
    }

    // ── GPU particle pools ────────────────────────────────────────────────────
    auto flamePool  = render::createParticlePool(*device, 512u);
    auto sparksPool = render::createParticlePool(*device, 256u);

    // ── Flame emitter entity ──────────────────────────────────────────────────
    {
        TransformComponent t;
        t.position = glm::vec3(3.0f, 0.5f, -2.0f);  // sector 0 — centre-right, always in camera frustum

        render::ParticleEmitterComponent e;
        e.pool            = flamePool.get();
        e.atlasTexture    = flameAtlas.get();
        e.emissionRate    = 200.0f;
        e.emitDir         = glm::vec3(0.0f, 1.0f, 0.0f);
        e.coneHalfAngle   = glm::radians(25.0f);
        e.speedMin        = 0.4f;   e.speedMax    = 1.5f;
        e.lifetimeMin     = 1.0f;   e.lifetimeMax = 2.5f;
        // HDR tint: birth = hot white-yellow; death = dark transparent orange
        e.colorStart      = glm::vec4(3.0f, 1.5f, 0.2f, 1.0f);
        e.colorEnd        = glm::vec4(0.4f, 0.1f, 0.0f, 0.0f);
        e.sizeStart       = 0.12f;  e.sizeEnd     = 0.28f;  // slightly larger for visibility at distance
        e.emissiveScale   = 4.0f;   // strong bloom contribution
        e.gravity         = glm::vec3(0.0f, -0.8f, 0.0f);  // fire barely falls
        e.drag            = 2.0f;
        e.turbulenceScale = 1.5f;   // organic curl
        e.softRange       = 0.25f;

        EntityId eid = world.createEntity();
        world.addComponent(eid, t);
        world.addComponent(eid, std::move(e));
    }

    // ── Sparks emitter entity ─────────────────────────────────────────────────
    {
        TransformComponent t;
        t.position = glm::vec3(9.0f, 0.5f, 0.0f);  // sector 1 — centre of side room, elevated above floor

        render::ParticleEmitterComponent e;
        e.pool             = sparksPool.get();
        e.atlasTexture     = sparksAtlas.get();
        e.emissionRate     = 60.0f;
        e.emitDir          = glm::vec3(0.0f, 1.0f, 0.0f);
        e.coneHalfAngle    = glm::radians(80.0f);  // near-omnidirectional burst
        e.speedMin         = 1.5f;   e.speedMax    = 4.5f;
        e.lifetimeMin      = 0.25f;  e.lifetimeMax = 0.7f;
        // White-hot at birth; cooling orange → transparent at death
        e.colorStart       = glm::vec4(4.0f, 3.5f, 2.0f, 1.0f);
        e.colorEnd         = glm::vec4(1.0f, 0.3f, 0.0f, 0.0f);
        e.sizeStart        = 0.05f;  e.sizeEnd     = 0.02f;  // slightly larger for visibility
        e.emissiveScale    = 6.0f;   // very bright streaks
        e.gravity          = glm::vec3(0.0f, -9.8f, 0.0f);  // full gravity
        e.drag             = 0.4f;   // low drag — fast sharp arcs
        e.turbulenceScale  = 0.15f;  // slight curl variation
        e.velocityStretch  = 0.06f;  // elongated streaks along velocity
        e.softRange        = 0.02f;  // tight fade: sparks visible right down to floor level

        EntityId eid = world.createEntity();
        world.addComponent(eid, t);
        world.addComponent(eid, std::move(e));
    }

    std::printf("[Daedalus] Particle emitters created: flame (512) + sparks (256).\n");

    // ─── Phase 1D: Mirror render target + quad mesh ─────────────────────────────────
    // Planar mirror on the south wall (z=-5), x∈[-3,3], y∈[0,3].
    // Mirror quad is 6×3 world units (2:1 aspect). The RT dimensions are declared as
    // named constants so the reflected projection aspect is always derived from them —
    // never hardcoded. RT resolution must match the quad's aspect ratio to avoid
    // distortion when UV [0,1] is mapped across the surface.
    //   kMirrorRTW / kMirrorRTH = 512/256 = 2.0  ←  matches 6/3 quad aspect.
    // To use a different mirror shape, update both constants here and the quad geometry.
    constexpr u32 kMirrorRTW = 512u;
    constexpr u32 kMirrorRTH = 256u;   // 6:3 quad ⟹ 2:1 RT

    std::unique_ptr<rhi::ITexture> mirrorRT;
    {
        rhi::TextureDescriptor td;
        td.width     = kMirrorRTW;
        td.height    = kMirrorRTH;
        td.format    = rhi::TextureFormat::BGRA8Unorm;
        td.usage     = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderRead;
        td.debugName = "mirror_rt";
        mirrorRT = device->createTexture(td);
        DAEDALUS_ASSERT(mirrorRT != nullptr, "Failed to create mirror render target");
    }

    const render::MeshData mirrorQuadMesh = makeMirrorQuadMesh();

    std::unique_ptr<rhi::IBuffer> mirrorQuadVBO;
    std::unique_ptr<rhi::IBuffer> mirrorQuadIBO;
    {
        rhi::BufferDescriptor d;
        d.size      = mirrorQuadMesh.vertices.size() * sizeof(render::StaticMeshVertex);
        d.usage     = rhi::BufferUsage::Vertex;
        d.initData  = mirrorQuadMesh.vertices.data();
        d.debugName = "MirrorQuadVBO";
        mirrorQuadVBO = device->createBuffer(d);
    }
    {
        rhi::BufferDescriptor d;
        d.size      = mirrorQuadMesh.indices.size() * sizeof(u32);
        d.usage     = rhi::BufferUsage::Index;
        d.initData  = mirrorQuadMesh.indices.data();
        d.debugName = "MirrorQuadIBO";
        mirrorQuadIBO = device->createBuffer(d);
    }

    std::printf("[Mirror] %ux%u render target + quad mesh created (south wall z=-5).\n",
                kMirrorRTW, kMirrorRTH);

    // Camera sector tracking
    world::SectorId cameraSector = 0u;

    // ─── Frame timing + camera state ─────────────────────────────────────────

    using Clock   = std::chrono::steady_clock;
    auto  startTP = Clock::now();
    auto  prevTP  = startTP;

    glm::mat4 prevView(1.0f);
    glm::mat4 prevProj(1.0f);
    u32       frameIdx = 0;

    // Cached mirror VP: the view+proj used to *render* the mirror RT last frame.
    // When the pre-pass is skipped (acute angle), frame.mirrorViewProj must use
    // this cached value so the projective UV in the G-buffer shader stays in sync
    // with the stale RT content.  Using the current-frame VP instead causes the
    // UV to map to the wrong region of the RT as the camera moves → flickering.
    glm::mat4 lastMirrorReflView = glm::mat4(1.0f);
    glm::mat4 lastMirrorReflProj = glm::mat4(1.0f);
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

        // Populate SceneView::particleEmitters from ECS entities.
        render::particleRenderSystem(world, scene, dt, frameIdx);

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

        // ── Volumetric fog ────────────────────────────────────────────────────
        // Only enabled in sector 1 (adjacent room).
        // fogNear=0.5 avoids fogging surfaces right at the camera.
        scene.fog.enabled    = (cameraSector == 1u);
        scene.fog.density    = 0.010f;
        scene.fog.anisotropy = 0.3f;
        scene.fog.scattering = 0.8f;
        scene.fog.fogFar     = 80.0f;
        scene.fog.fogNear    = 0.5f;
        scene.fog.ambientFog = glm::vec3(0.04f, 0.05f, 0.06f);
        if (frameIdx == 0u)
            std::printf("[Fog] volumetric fog enabled (density=%.3f anisotropy=%.2f)\n",
                        scene.fog.density, scene.fog.anisotropy);

        // ── Screen-space reflections ──────────────────────────────────────
        // Enabled on all surfaces with roughness < 0.5 (polished floor/walls).
        // maxDistance 15 m: covers the full sector width without excessive cost.
        scene.ssr.enabled         = true;
        scene.ssr.maxDistance     = 15.0f;
        scene.ssr.thickness       = 0.15f;
        scene.ssr.roughnessCutoff = 0.5f;
        scene.ssr.fadeStart       = 0.1f;
        scene.ssr.maxSteps        = 64u;

        if (frameIdx == 0u)
            std::printf("[SSR] screen-space reflections enabled "
                        "(maxDist=%.1f roughnessCutoff=%.2f maxSteps=%u)\n",
                        scene.ssr.maxDistance, scene.ssr.roughnessCutoff,
                        scene.ssr.maxSteps);

        // ── Depth of Field ──────────────────────────────────────────────────────────────────
        // Focus on the test block at 4 m; in-focus band of 2 m; max bokeh 6 px.
        scene.dof.enabled       = true;
        scene.dof.focusDistance = 4.0f;
        scene.dof.focusRange    = 2.0f;
        scene.dof.bokehRadius   = 6.0f;
        if (frameIdx == 0u)
            std::printf("[DoF] depth of field enabled "
                        "(focus=%.1fm range=%.1fm bokeh=%.0fpx)\n",
                        scene.dof.focusDistance, scene.dof.focusRange,
                        scene.dof.bokehRadius);

        // ── Motion Blur ──────────────────────────────────────────────────────────────────────
        // Quarter-shutter blur: subtle cinematic smear on fast camera/object motion.
        scene.motionBlur.enabled      = true;
        scene.motionBlur.shutterAngle = 0.25f;
        scene.motionBlur.numSamples   = 8u;
        if (frameIdx == 0u)
            std::printf("[MotionBlur] motion blur enabled "
                        "(shutterAngle=%.2f samples=%u)\n",
                        scene.motionBlur.shutterAngle,
                        scene.motionBlur.numSamples);

        // ── Colour Grading ───────────────────────────────────────────────────────────────────
        // Identity LUT (no colour shift) at full intensity — ready to swap in a
        // custom LUT via scene.colorGrading.lutTexture for art direction.
        scene.colorGrading.enabled    = true;
        scene.colorGrading.intensity  = 1.0f;
        scene.colorGrading.lutTexture = nullptr;  // nullptr → engine identity LUT
        if (frameIdx == 0u)
            std::printf("[ColorGrading] colour grading enabled "
                        "(intensity=%.2f lut=%s)\n",
                        scene.colorGrading.intensity,
                        scene.colorGrading.lutTexture ? "custom" : "identity");

        // ── Optional post-FX: vignette + film grain ─────────────────────────────────
        // Subtle settings: vignette darkens corners without crushing highlights;
        // grain adds organic noise seeded per-frame by the renderer internally.
        // caAmount=0 keeps chromatic aberration off for the demo.
        scene.optionalFx.enabled           = true;
        scene.optionalFx.vignetteIntensity = 0.35f;
        scene.optionalFx.vignetteRadius    = 0.40f;
        scene.optionalFx.grainAmount       = 0.04f;
        scene.optionalFx.caAmount          = 0.0f;
        if (frameIdx == 0u)
            std::printf("[OptFx] vignette+grain enabled "
                        "(vignetteIntensity=%.2f grainAmount=%.2f)\n",
                        scene.optionalFx.vignetteIntensity,
                        scene.optionalFx.grainAmount);

        // ── FXAA ───────────────────────────────────────────────────────────────────────
        // FXAA is the default (UpscalingMode::FXAA) — no explicit set required.
        if (frameIdx == 0u)
            std::printf("[FXAA] 9-tap screen-space anti-aliasing enabled\n");

        // ── Planar mirror pre-pass ──────────────────────────────────────────────────
        // Reflect the camera through the south-wall mirror plane at z=-5.
        //   reflEye.z = 2*(-5) - eye.z = -10 - eye.z
        //   reflDir.z = -dir.z  (flip z component)
        // The reflected geometry is rendered into mirrorRT by renderMirrorPrepass();
        // the mirror surface quad reads mirrorRT as emissive.
        {
            const glm::vec3 camDir  = glm::normalize(target - eye);
            const glm::vec3 reflEye = glm::vec3(eye.x, eye.y, -10.0f - eye.z);
            const glm::vec3 reflDir = glm::vec3(camDir.x, camDir.y, -camDir.z);

            // Build the reflected view matrix first — we need it to project the
            // mirror quad corners into view space for the off-axis frustum below.
            const glm::mat4 reflView = glm::lookAtLH(
                reflEye, reflEye + glm::normalize(reflDir) * 10.0f, up);

            // Off-axis (asymmetric) frustum: project all 4 mirror quad corners
            // through the reflected view matrix, then build a frustum that covers
            // exactly those extents.  The RT therefore contains exactly the scene
            // visible through the mirror quad, so UV (0,1)×(0,1) on the surface
            // maps 1-to-1 to the rendered content at any camera angle/distance.
            const glm::vec3 kQuadCorners[4] = {
                {-3.f, 0.f, -4.99f},  // BL — must match makeMirrorQuadMesh()
                { 3.f, 0.f, -4.99f},  // BR
                { 3.f, 3.f, -4.99f},  // TR
                {-3.f, 3.f, -4.99f},  // TL
            };
            float vl, vr, vb, vt, vz;
            {
                const glm::vec4 c0 = reflView * glm::vec4(kQuadCorners[0], 1.0f);
                vz = c0.z;  vl = vr = c0.x / c0.z;  vb = vt = c0.y / c0.z;
                for (int ci = 1; ci < 4; ++ci)
                {
                    const glm::vec4 cv = reflView * glm::vec4(kQuadCorners[ci], 1.0f);
                    vz = std::min(vz, cv.z);
                    vl = std::min(vl, cv.x / cv.z);  vr = std::max(vr, cv.x / cv.z);
                    vb = std::min(vb, cv.y / cv.z);  vt = std::max(vt, cv.y / cv.z);
                }
            }
            // Guard: only render the mirror when the reflected camera faces the right way.
            // vz ≤ 0 means all quad corners are behind the reflected eye — the surface
            // itself is back-face culled so neither the quad nor the pre-pass is needed.
            // When 0 < vz ≤ 0.01 the angle is very acute: we still render the surface
            // quad (it's visible), but the frustum near plane is too small to produce a
            // reliable pre-pass, so reflectedDraws is left empty and
            // renderMirrorPrepass() returns immediately, preserving the last valid RT.
            if (vz > 0.0f)
            {
            render::MirrorDraw mirror;
            mirror.renderTarget   = mirrorRT.get();
            mirror.rtWidth        = kMirrorRTW;
            mirror.rtHeight       = kMirrorRTH;

            if (vz > 0.01f)
            {
                // Valid frustum: compute current reflected VP and update the cache.
                // The cache is what the G-buffer shader uses as frame.mirrorViewProj;
                // keeping it in sync with the RT we are about to render eliminates
                // the per-frame UV mismatch that caused flickering at acute angles.
                const float mirrorNear = vz * 0.99f;
                const glm::mat4 mirrorProj = glm::frustumLH_ZO(
                    vl * mirrorNear, vr * mirrorNear,
                    vb * mirrorNear, vt * mirrorNear,
                    mirrorNear, 100.0f);

                mirror.reflectedView = reflView;
                mirror.reflectedProj = mirrorProj;
                lastMirrorReflView   = reflView;    // keep cache in sync with RT
                lastMirrorReflProj   = mirrorProj;

                // Reflected scene: render ALL sectors (not just main-camera visible)
                // so the mirror sees full room geometry.
                for (const auto& sd : sectorDraws)
                {
                    if (sd.vertexBuffer != nullptr)
                    {
                        mirror.reflectedDraws.push_back(sd);
                    }
                }
                // Include current frame mesh draws from ECS/static submissions so
                // objects (box, voxel object, etc.) also appear in reflection.
                // Skip the mirror surface itself to avoid recursion feedback.
                for (const render::MeshDraw& d : scene.meshDraws)
                {
                    if (d.vertexBuffer == mirrorQuadVBO.get() &&
                        d.indexBuffer  == mirrorQuadIBO.get())
                    {
                        continue;
                    }
                    mirror.reflectedDraws.push_back(d);
                }
            }
            else
            {
                // Acute angle: pre-pass is skipped (reflectedDraws stays empty) and
                // the RT retains its last valid content.  Use the cached VP so the
                // projective UV in the G-buffer shader matches that stale RT.
                mirror.reflectedView = lastMirrorReflView;
                mirror.reflectedProj = lastMirrorReflProj;
            }

            scene.mirrors.push_back(std::move(mirror));

            // Mirror surface quad: the reflection is placed in the EMISSIVE slot so
            // the lighting pass adds it directly to the HDR buffer at full intensity.
            // Using albedo + roughness=0/metalness=1 would make the PBR GGX produce
            // near-zero HDR values (delta specular never fires), which contaminates
            // TAA history via the 3×3 AABB neighbourhood and causes exponential
            // darkening spread across the whole screen.  Emissive bypasses the BRDF.
            render::MeshDraw mirrorSurfaceDraw;
            mirrorSurfaceDraw.vertexBuffer          = mirrorQuadVBO.get();
            mirrorSurfaceDraw.indexBuffer           = mirrorQuadIBO.get();
            mirrorSurfaceDraw.indexCount            = static_cast<u32>(mirrorQuadMesh.indices.size());
            mirrorSurfaceDraw.modelMatrix           = glm::mat4(1.0f);
            mirrorSurfaceDraw.prevModel             = glm::mat4(1.0f);
            mirrorSurfaceDraw.material.albedo       = nullptr;          // white (used as F0)
            mirrorSurfaceDraw.material.emissive     = mirrorRT.get();   // reflection shown via emissive
            mirrorSurfaceDraw.material.roughness    = 1.0f;             // max roughness: flattest possible GGX lobe
            mirrorSurfaceDraw.material.metalness    = 1.0f;             // zero diffuse (kD = 0)
            mirrorSurfaceDraw.material.tint         = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // black albedo in G-buffer → F0=0 → BRDF≈0
            mirrorSurfaceDraw.material.isMirrorSurface = true;          // projective emissive UV in G-buffer shader
            scene.meshDraws.push_back(std::move(mirrorSurfaceDraw));

            if (frameIdx == 0u)
                std::printf("[Mirror] planar mirror pre-pass enabled "
                            "(south wall z=-5, RT %ux%u, off-axis frustum)\n",
                            kMirrorRTW, kMirrorRTH);
            } // end vz > 0.0f guard
        }

        // ── Render
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
