// Daedalus Engine — Application Entry Point
// Phase 1A: SDL3 + Metal window with a per-frame clear-colour render.
// Exercises the full RHI stack: device → queue → swapchain → command buffer →
// render pass (clear) → present → commit.

#include "daedalus/core/assert.h"
#include "daedalus/core/create_platform.h"
#include "daedalus/core/ecs/world.h"
#include "daedalus/core/events/event_bus.h"
#include "daedalus/core/threading/job_system.h"
#include "daedalus/render/create_render_device.h"
#include "daedalus/render/rhi/i_command_queue.h"
#include "daedalus/render/rhi/i_command_buffer.h"
#include "daedalus/render/rhi/i_swapchain.h"

#include <SDL3/SDL.h>

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

    auto device   = rhi::createRenderDevice();
    auto queue    = device->createCommandQueue("Main Queue");
    auto swapchain = device->createSwapchain(window, WINDOW_W, WINDOW_H);

    std::printf("[Daedalus] GPU: %s\n",
                std::string(device->deviceName()).c_str());

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
                const u32 w = static_cast<u32>(event.window.data1);
                const u32 h = static_cast<u32>(event.window.data2);
                swapchain->resize(w, h);
            }
        }

        if (!running) { break; }

        // Acquire drawable
        ITexture* drawable = swapchain->nextDrawable();

        // Build render pass: clear to a dark midnight-blue
        RenderPassDescriptor rpd;
        rpd.colorAttachmentCount = 1;
        rpd.colorAttachments[0].texture    = drawable;
        rpd.colorAttachments[0].loadAction = LoadAction::Clear;
        rpd.colorAttachments[0].storeAction= StoreAction::Store;
        rpd.colorAttachments[0].clearColor = { 0.05f, 0.05f, 0.12f, 1.0f };
        rpd.debugLabel = "Clear Pass";

        // Record commands
        auto cmdBuf = queue->createCommandBuffer("Frame");
        cmdBuf->pushDebugGroup("Frame");

        IRenderPassEncoder* enc = cmdBuf->beginRenderPass(rpd);
        enc->end();

        cmdBuf->present(*swapchain);
        cmdBuf->popDebugGroup();
        cmdBuf->commit();
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
