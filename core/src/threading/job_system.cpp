#include "daedalus/core/threading/job_system.h"
#include "daedalus/core/assert.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace daedalus
{

// ─── JobSystem::Impl ──────────────────────────────────────────────────────────

struct JobSystem::Impl
{
    struct Task
    {
        std::function<void()>             work;
        std::shared_ptr<std::atomic<i32>> completionCounter;
        // If set, this task must wait for the dependency counter to reach 0.
        std::shared_ptr<std::atomic<i32>> dependencyCounter;
    };

    std::vector<std::thread>    workers;
    std::queue<Task>            queue;
    std::mutex                  queueMutex;
    std::condition_variable     queueCv;
    std::atomic<bool>           shutdown{false};
    std::atomic<i32>            pendingCount{0};
    std::mutex                  allDoneMutex;
    std::condition_variable     allDoneCv;

    void workerLoop()
    {
        while (true)
        {
            Task task;

            {
                std::unique_lock lock(queueMutex);
                queueCv.wait(lock, [this]
                {
                    return shutdown.load(std::memory_order_acquire)
                           || !queue.empty();
                });

                if (shutdown.load(std::memory_order_acquire) && queue.empty())
                {
                    return;
                }

                task = std::move(queue.front());
                queue.pop();
            }

            // If there is a dependency, spin-wait for it to complete.
            // For production code this would use a per-task condition variable,
            // but a short spin is fine for Phase 1A task sizes.
            if (task.dependencyCounter)
            {
                while (task.dependencyCounter->load(std::memory_order_acquire) != 0)
                {
                    std::this_thread::yield();
                }
            }

            task.work();

            // Signal completion.
            task.completionCounter->fetch_sub(1, std::memory_order_acq_rel);

            const i32 remaining = pendingCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (remaining == 0)
            {
                std::lock_guard lk(allDoneMutex);
                allDoneCv.notify_all();
            }
        }
    }
};

// ─── JobHandle ────────────────────────────────────────────────────────────────

bool JobHandle::isPending() const noexcept
{
    if (!m_counter) { return false; }
    return m_counter->load(std::memory_order_acquire) != 0;
}

void JobHandle::wait() const noexcept
{
    if (!m_counter) { return; }
    while (m_counter->load(std::memory_order_acquire) != 0)
    {
        std::this_thread::yield();
    }
}

// ─── JobSystem ────────────────────────────────────────────────────────────────

JobSystem::JobSystem(u32 workerCount)
    : m_impl(std::make_unique<Impl>())
{
    if (workerCount == 0)
    {
        const u32 hw = static_cast<u32>(std::thread::hardware_concurrency());
        workerCount  = hw > 1u ? hw - 1u : 1u;
    }
    m_workerCount = workerCount;

    m_impl->workers.reserve(workerCount);
    for (u32 i = 0; i < workerCount; ++i)
    {
        m_impl->workers.emplace_back([this] { m_impl->workerLoop(); });
    }
}

JobSystem::~JobSystem()
{
    waitAll();
    m_impl->shutdown.store(true, std::memory_order_release);
    m_impl->queueCv.notify_all();
    for (auto& t : m_impl->workers)
    {
        if (t.joinable()) { t.join(); }
    }
}

JobHandle JobSystem::submit(std::function<void()> task, JobHandle dependency)
{
    DAEDALUS_ASSERT(task != nullptr, "JobSystem::submit: null task");

    auto counter = std::make_shared<std::atomic<i32>>(1);
    m_impl->pendingCount.fetch_add(1, std::memory_order_relaxed);

    {
        std::lock_guard lock(m_impl->queueMutex);
        m_impl->queue.push({
            std::move(task),
            counter,
            dependency.isValid() ? dependency.m_counter : nullptr
        });
    }
    m_impl->queueCv.notify_one();

    return JobHandle(counter);
}

void JobSystem::waitAll() noexcept
{
    std::unique_lock lock(m_impl->allDoneMutex);
    m_impl->allDoneCv.wait(lock, [this]
    {
        return m_impl->pendingCount.load(std::memory_order_acquire) == 0;
    });
}

usize JobSystem::pendingCount() const noexcept
{
    return static_cast<usize>(
        m_impl->pendingCount.load(std::memory_order_acquire));
}

} // namespace daedalus
