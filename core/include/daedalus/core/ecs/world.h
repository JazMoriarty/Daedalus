#pragma once

#include "daedalus/core/assert.h"
#include "daedalus/core/types.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace daedalus
{

// ─── ComponentTypeId ──────────────────────────────────────────────────────────
// A unique integer assigned to each component type on first use.
// Assignment happens at static initialisation via componentTypeId<T>().

using ComponentTypeId = u32;

namespace detail
{
    inline std::atomic<ComponentTypeId> g_nextComponentTypeId{1};
} // namespace detail

/// Returns the unique ComponentTypeId for T.  Thread-safe.
template<typename T>
[[nodiscard]] ComponentTypeId componentTypeId() noexcept
{
    static const ComponentTypeId id =
        detail::g_nextComponentTypeId.fetch_add(1u, std::memory_order_relaxed);
    return id;
}

// ─── ArchetypeKey ─────────────────────────────────────────────────────────────
// A sorted set of ComponentTypeIds that uniquely identifies an archetype.

struct ArchetypeKey
{
    std::vector<ComponentTypeId> ids; // always sorted ascending

    bool operator==(const ArchetypeKey&) const = default;
};

struct ArchetypeKeyHash
{
    [[nodiscard]] usize operator()(const ArchetypeKey& key) const noexcept
    {
        usize seed = key.ids.size();
        for (ComponentTypeId id : key.ids)
        {
            seed ^= static_cast<usize>(id) + 0x9e3779b9u
                  + (seed << 6u) + (seed >> 2u);
        }
        return seed;
    }
};

// ─── World ────────────────────────────────────────────────────────────────────
// The ECS container.  Entities are identified by an EntityId (opaque 64-bit
// value); components are stored in archetype tables for cache-efficient
// iteration.
//
// Design decisions:
//   • Archetype table per unique component set (Structure of Arrays layout).
//   • Swap-remove on entity delete keeps arrays compact.
//   • Generation counters detect use-after-free on entity handles.
//   • Template methods are fully defined in this header; all non-template
//     helpers are declared here and defined in world.cpp.

class World
{
public:
    World();
    ~World();

    // Non-copyable, non-movable.
    World(const World&)            = delete;
    World& operator=(const World&) = delete;

    // ─── Entity lifecycle ─────────────────────────────────────────────────────

    /// Create a new entity with no components.  O(1).
    [[nodiscard]] EntityId createEntity();

    /// Destroy an entity and all its component data.  O(components).
    void destroyEntity(EntityId id);

    /// Returns true if `id` refers to a currently live entity.
    [[nodiscard]] bool isValid(EntityId id) const noexcept;

    // ─── Component mutation ───────────────────────────────────────────────────

    /// Add component T to `entity`.  Moving the entity to the appropriate
    /// archetype.  Asserts if T is already present.
    template<typename T>
    void addComponent(EntityId entity, T component)
    {
        DAEDALUS_ASSERT(isValid(entity), "addComponent: invalid entity");
        const ComponentTypeId typeId = componentTypeId<T>();

        // Register element size so getOrCreateArchetype can build arrays.
        m_componentSizes.emplace(typeId, sizeof(T));

        EntityRecord& rec = recordOf(entity);
        DAEDALUS_ASSERT(rec.archetype == nullptr
                        || !archetypeHasType(*rec.archetype, typeId),
                        "addComponent: component already present");

        Archetype* from = rec.archetype;
        ArchetypeKey newKey = makeKeyWith(from, typeId);
        Archetype*   to    = getOrCreateArchetype(newKey);

        const u32 toRow = moveEntityToArchetype(entity, from, rec.row, to);

        // Write the new component into the destination archetype.
        auto* slot = static_cast<T*>(to->arrays.at(typeId).get(toRow));
        ::new (slot) T(std::move(component));

        rec.archetype = to;
        rec.row       = toRow;
    }

    /// Remove component T from `entity`.  Asserts if T is not present.
    template<typename T>
    void removeComponent(EntityId entity)
    {
        DAEDALUS_ASSERT(isValid(entity), "removeComponent: invalid entity");
        const ComponentTypeId typeId = componentTypeId<T>();

        EntityRecord& rec = recordOf(entity);
        DAEDALUS_ASSERT(rec.archetype != nullptr
                        && archetypeHasType(*rec.archetype, typeId),
                        "removeComponent: component not present");

        // Destruct the component in its current slot.
        auto* slot = static_cast<T*>(rec.archetype->arrays.at(typeId).get(rec.row));
        slot->~T();

        Archetype* from   = rec.archetype;
        ArchetypeKey newKey = makeKeyWithout(from, typeId);
        Archetype*   to    = getOrCreateArchetype(newKey);

        const u32 toRow = moveEntityToArchetype(entity, from, rec.row, to);

        rec.archetype = to;
        rec.row       = toRow;
    }

    /// Returns a mutable reference to component T on `entity`.
    /// Asserts if T is not present.
    template<typename T>
    [[nodiscard]] T& getComponent(EntityId entity)
    {
        DAEDALUS_ASSERT(isValid(entity), "getComponent: invalid entity");
        const ComponentTypeId typeId = componentTypeId<T>();
        const EntityRecord&   rec   = recordOf(entity);
        DAEDALUS_ASSERT(rec.archetype != nullptr
                        && archetypeHasType(*rec.archetype, typeId),
                        "getComponent: component not present");
        return *static_cast<T*>(rec.archetype->arrays.at(typeId).get(rec.row));
    }

    template<typename T>
    [[nodiscard]] const T& getComponent(EntityId entity) const
    {
        DAEDALUS_ASSERT(isValid(entity), "getComponent: invalid entity");
        const ComponentTypeId typeId = componentTypeId<T>();
        const EntityRecord&   rec   = recordOf(entity);
        DAEDALUS_ASSERT(rec.archetype != nullptr
                        && archetypeHasType(*rec.archetype, typeId),
                        "getComponent: component not present");
        return *static_cast<const T*>(rec.archetype->arrays.at(typeId).get(rec.row));
    }

    /// Returns true if `entity` currently has component T.
    template<typename T>
    [[nodiscard]] bool hasComponent(EntityId entity) const noexcept
    {
        if (!isValid(entity)) { return false; }
        const ComponentTypeId typeId = componentTypeId<T>();
        const EntityRecord&   rec   = recordOf(entity);
        return rec.archetype != nullptr && archetypeHasType(*rec.archetype, typeId);
    }

    // ─── Iteration ────────────────────────────────────────────────────────────

    /// Call `func(EntityId, Ts&...)` for every entity that has all of Ts.
    /// Components are iterated archetype-by-archetype for cache efficiency.
    template<typename... Ts, typename Func>
    void each(Func&& func)
    {
        // Collect required type IDs.
        const ComponentTypeId required[] = { componentTypeId<Ts>()... };
        const usize nRequired = sizeof...(Ts);

        for (auto& [key, archPtr] : m_archetypes)
        {
            Archetype& arch = *archPtr;
            if (arch.count == 0) { continue; }

            // Check that the archetype has every required component.
            bool matches = true;
            for (usize i = 0; i < nRequired; ++i)
            {
                if (!archetypeHasType(arch, required[i]))
                {
                    matches = false;
                    break;
                }
            }
            if (!matches) { continue; }

            // Iterate rows.
            for (u32 row = 0; row < arch.count; ++row)
            {
                func(arch.entities[row],
                     *static_cast<Ts*>(arch.arrays.at(componentTypeId<Ts>()).get(row))...);
            }
        }
    }

    // ─── Statistics ───────────────────────────────────────────────────────────

    [[nodiscard]] usize entityCount()    const noexcept { return m_entityCount; }
    [[nodiscard]] usize archetypeCount() const noexcept { return m_archetypes.size(); }

private:
    // ─── ComponentArray ───────────────────────────────────────────────────────
    // Contiguous byte storage for a single component type within one archetype.
    struct ComponentArray
    {
        std::vector<byte> data;
        usize             elementSize = 0;

        void push(const void* src)
        {
            const usize pos = data.size();
            data.resize(pos + elementSize);
            std::memcpy(data.data() + pos, src, elementSize);
        }

        // Copy the element at `srcRow` from another array into a new slot.
        void copyFrom(const ComponentArray& other, u32 srcRow)
        {
            push(other.data.data() + srcRow * elementSize);
        }

        // Remove the element at `row` using swap-remove.
        void swapRemove(u32 row)
        {
            const usize last = data.size() - elementSize;
            const usize pos  = row * static_cast<usize>(elementSize);
            if (pos != last)
            {
                std::memcpy(data.data() + pos, data.data() + last, elementSize);
            }
            data.resize(last);
        }

        [[nodiscard]] void* get(u32 row) noexcept
        {
            return data.data() + row * elementSize;
        }
        [[nodiscard]] const void* get(u32 row) const noexcept
        {
            return data.data() + row * elementSize;
        }
    };

    // ─── Archetype ────────────────────────────────────────────────────────────
    struct Archetype
    {
        std::vector<ComponentTypeId>                        componentIds; // sorted
        std::unordered_map<ComponentTypeId, ComponentArray> arrays;
        std::vector<EntityId>                               entities;
        u32                                                 count = 0;
    };

    // ─── Entity record ────────────────────────────────────────────────────────
    struct EntityRecord
    {
        Archetype* archetype  = nullptr;
        u32        row        = 0;
        u32        generation = 0;
    };

    // ─── Non-template helpers (defined in world.cpp) ─────────────────────────

    [[nodiscard]] EntityRecord&       recordOf(EntityId id);
    [[nodiscard]] const EntityRecord& recordOf(EntityId id) const;

    [[nodiscard]] static bool archetypeHasType(const Archetype& arch,
                                               ComponentTypeId  typeId) noexcept;

    [[nodiscard]] ArchetypeKey makeKeyWith(const Archetype* arch,
                                          ComponentTypeId  typeId) const;

    [[nodiscard]] ArchetypeKey makeKeyWithout(const Archetype* arch,
                                             ComponentTypeId  typeId) const;

    [[nodiscard]] Archetype* getOrCreateArchetype(const ArchetypeKey& key);

    /// Move all components (except those being added/removed) from `fromRow` in
    /// `from` into a new row at the end of `to`.
    /// Returns the new row index in `to`.
    [[nodiscard]] u32 moveEntityToArchetype(EntityId   entity,
                                            Archetype* from,
                                            u32        fromRow,
                                            Archetype* to);

    // ─── Storage ──────────────────────────────────────────────────────────────

    std::vector<EntityRecord> m_records;     // indexed by entity slot index
    std::vector<u32>          m_freeSlots;   // recycled slot indices
    usize                     m_entityCount = 0;

    // Maps ComponentTypeId -> sizeof(T), populated lazily via addComponent.
    std::unordered_map<ComponentTypeId, usize> m_componentSizes;

    std::unordered_map<ArchetypeKey, std::unique_ptr<Archetype>, ArchetypeKeyHash>
        m_archetypes;
};

} // namespace daedalus
