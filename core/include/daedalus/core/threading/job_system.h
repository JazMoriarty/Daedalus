#pragma once

#include "daedalus/core/types.h"

#include <atomic>
#include <functional>
#include <memory>

namespace daedalus
{

// ─── JobHandle ────────────────────────────────────────────────────────────────
// Lightweight reference to a submitted job.  Allows the caller to block until
// the job (and all jobs it transitively depends on) has completed.

class JobHandle
{
public:
    JobHandle() = default;

    /// Returns true while the referenced job is still in-flight.
    [[nodiscard]] bool isPending() const noexcept;

    /// Block the calling thread until the referenced job completes.
    void wait() const noexcept;

    /// A default-constructed JobHandle is invalid.
    [[nodiscard]] bool isValid() const noexcept { return m_counter != nullptr; }

private:
    friend class JobSystem;
    explicit JobHandle(std::shared_ptr<std::atomic<i32>> counter)
        : m_counter(std::move(counter)) {}

    std::shared_ptr<std::atomic<i32>> m_counter;
};

// ─── JobSystem ────────────────────────────────────────────────────────────────
// A fixed-size thread pool that executes submitted tasks concurrently.
// Tasks are plain callables (std::function<void()>); optional dependency on a
// previous JobHandle delays execution until that job completes.
//
// Thread safety: submit() and wait() are thread-safe.

class JobSystem
{
public:
    /// Create a job system with `workerCount` threads.
    /// Pass 0 to use std::thread::hardware_concurrency() - 1.
    explicit JobSystem(u32 workerCount = 0);
    ~JobSystem();

    // Non-copyable, non-movable.
    JobSystem(const JobSystem&)            = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // ─── Submission ───────────────────────────────────────────────────────────

    /// Submit a task to the thread pool.
    /// If `dependency` is valid and pending, the task will not start until it
    /// completes.
    /// Returns a JobHandle that can be used to wait for completion.
    [[nodiscard]] JobHandle submit(std::function<void()> task,
                                   JobHandle             dependency = {});

    // ─── Synchronisation ──────────────────────────────────────────────────────

    /// Block until all currently submitted jobs have completed.
    void waitAll() noexcept;

    // ─── Diagnostics ──────────────────────────────────────────────────────────

    [[nodiscard]] u32   workerCount() const noexcept { return m_workerCount; }
    [[nodiscard]] usize pendingCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    u32                   m_workerCount = 0;
};

} // namespace daedalus
