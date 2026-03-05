// Daedalus Engine — Application Entry Point
// Phase 1B: SDL3 + Metal deferred renderer.
// Deferred PBR pipeline: depth-prepass → G-buffer → SSAO → lighting
//   → skybox → TAA → bloom → tone-mapping.

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

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace daedalus;
using namespace daedalus::rhi;

// ─── Example components (ECS smoke test) ─────────────────────────────────────

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

        // ── Camera: slow orbit around the room centre at head height ──────────
        const float angle   = time * 0.25f;  // one full rotation every ~25 s
        const float camR    = 3.5f;
        const glm::vec3 eye    = { std::cos(angle) * camR, 1.8f, std::sin(angle) * camR };
        const glm::vec3 target = { 0.0f, 1.8f, 0.0f };
        const glm::vec3 up     = { 0.0f, 1.0f, 0.0f };

        const float fovY   = glm::radians(60.0f);
        const float aspect = static_cast<float>(swapW) / static_cast<float>(swapH);

        const glm::mat4 view = glm::lookAtLH(eye, target, up);
        const glm::mat4 proj = glm::perspectiveLH_ZO(fovY, aspect, 0.1f, 50.0f);

        // ── Build SceneView ───────────────────────────────────────────────────
        render::SceneView scene;
        scene.view      = view;
        scene.proj      = proj;
        scene.prevView  = prevView;
        scene.prevProj  = prevProj;
        scene.cameraPos = eye;
        scene.cameraDir = glm::normalize(target - eye);
        scene.time      = time;
        scene.deltaTime = dt;
        scene.frameIndex = frameIdx;

        // Single warm overhead point light
        scene.pointLights.push_back({
            glm::vec3(0.0f, 3.6f, 0.0f),  // position
            4.0f,                           // radius
            glm::vec3(1.0f, 0.92f, 0.78f), // warm white
            3.5f                            // intensity
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
