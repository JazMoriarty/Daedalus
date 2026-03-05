#pragma once

#include "daedalus/core/assert.h"
#include "daedalus/core/types.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace daedalus
{

// ─── SubscriptionHandle ───────────────────────────────────────────────────────
// Opaque token returned by EventBus::subscribe().  Pass to unsubscribe() to
// remove the handler.

struct SubscriptionHandle
{
    u64 id = 0;
    [[nodiscard]] bool isValid() const noexcept { return id != 0; }
};

// ─── EventBus ─────────────────────────────────────────────────────────────────
// A fully typed synchronous publish/subscribe message bus.
//
// Template parameter T is the event type.  Each distinct T has its own
// independent subscription set; there is no shared base class or dynamic_cast.
//
// Thread safety: NOT thread-safe.  All subscribe/publish/unsubscribe calls must
// occur on the same thread.  For cross-thread events use the job system to
// marshal onto the main thread.

template<typename T>
class EventBus
{
public:
    using Handler = std::function<void(const T&)>;

    EventBus()  = default;
    ~EventBus() = default;

    // Non-copyable; moving is permitted.
    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&)                 = default;
    EventBus& operator=(EventBus&&)      = default;

    // ─── Subscribe ────────────────────────────────────────────────────────────

    /// Register a handler to receive events of type T.
    /// Returns a SubscriptionHandle that can be passed to unsubscribe().
    [[nodiscard]] SubscriptionHandle subscribe(Handler handler)
    {
        DAEDALUS_ASSERT(handler != nullptr, "subscribe: null handler");
        const u64 id = ++m_nextId;
        m_handlers.push_back({ id, std::move(handler) });
        return SubscriptionHandle{ id };
    }

    /// Remove a previously registered handler.  Silently ignores invalid
    /// handles.
    void unsubscribe(SubscriptionHandle handle) noexcept
    {
        if (!handle.isValid()) { return; }
        m_handlers.erase(
            std::remove_if(m_handlers.begin(), m_handlers.end(),
                           [id = handle.id](const Entry& e) { return e.id == id; }),
            m_handlers.end());
    }

    // ─── Publish ──────────────────────────────────────────────────────────────

    /// Deliver `event` synchronously to all current subscribers.
    /// Handlers are called in subscription order.
    void publish(const T& event) const
    {
        // Iterate a snapshot: publishing during a callback is allowed.
        const std::vector<Entry> snapshot = m_handlers;
        for (const Entry& e : snapshot)
        {
            e.handler(event);
        }
    }

    [[nodiscard]] usize subscriberCount() const noexcept { return m_handlers.size(); }

private:
    struct Entry
    {
        u64     id      = 0;
        Handler handler;
    };

    std::vector<Entry> m_handlers;
    u64                m_nextId = 0;
};

} // namespace daedalus
