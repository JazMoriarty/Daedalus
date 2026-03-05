#include "daedalus/core/ecs/world.h"

#include <algorithm>

namespace daedalus
{

// ─── Construction ─────────────────────────────────────────────────────────────

World::World()
{
    // Reserve slot 0 so INVALID_ENTITY (index 0) is never a live entity.
    m_records.push_back({});
}

World::~World() = default;

// ─── Entity lifecycle ─────────────────────────────────────────────────────────

EntityId World::createEntity()
{
    u32 index;

    if (!m_freeSlots.empty())
    {
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
    }
    else
    {
        index = static_cast<u32>(m_records.size());
        m_records.push_back({});
    }

    EntityRecord& rec = m_records[index];
    rec.archetype     = nullptr;
    rec.row           = 0;
    // rec.generation was already incremented by destroyEntity; leave it as-is
    // for reused slots, or 0 for brand-new slots.

    ++m_entityCount;
    return makeEntityId(index, rec.generation);
}

void World::destroyEntity(EntityId id)
{
    DAEDALUS_ASSERT(isValid(id), "destroyEntity: invalid entity");

    EntityRecord& rec = recordOf(id);

    if (rec.archetype != nullptr)
    {
        Archetype* arch       = rec.archetype;
        const u32  row        = rec.row;
        const u32  lastRow    = arch->count - 1;

        // Swap-remove all component arrays.
        for (auto& [typeId, arr] : arch->arrays)
        {
            arr.swapRemove(row);
        }

        // Swap-remove entity list and fix the moved entity's record.
        if (row != lastRow)
        {
            EntityId movedEntity       = arch->entities[lastRow];
            arch->entities[row]        = movedEntity;
            m_records[entityIndex(movedEntity)].row = row;
        }
        arch->entities.resize(lastRow);
        --arch->count;

        rec.archetype = nullptr;
    }

    // Bump generation to invalidate all outstanding handles for this slot.
    ++rec.generation;
    m_freeSlots.push_back(entityIndex(id));
    --m_entityCount;
}

bool World::isValid(EntityId id) const noexcept
{
    if (id == INVALID_ENTITY) { return false; }
    const u32 index = entityIndex(id);
    if (index >= m_records.size()) { return false; }
    return m_records[index].generation == entityGeneration(id);
}

// ─── Private helpers ──────────────────────────────────────────────────────────

World::EntityRecord& World::recordOf(EntityId id)
{
    DAEDALUS_ASSERT(isValid(id), "recordOf: invalid EntityId");
    return m_records[entityIndex(id)];
}

const World::EntityRecord& World::recordOf(EntityId id) const
{
    DAEDALUS_ASSERT(isValid(id), "recordOf: invalid EntityId");
    return m_records[entityIndex(id)];
}

bool World::archetypeHasType(const Archetype& arch, ComponentTypeId typeId) noexcept
{
    return std::binary_search(arch.componentIds.begin(),
                               arch.componentIds.end(),
                               typeId);
}

ArchetypeKey World::makeKeyWith(const Archetype* arch, ComponentTypeId typeId) const
{
    ArchetypeKey key;
    if (arch)
    {
        key.ids = arch->componentIds; // already sorted
    }

    // Insert `typeId` in sorted position.
    const auto pos = std::lower_bound(key.ids.begin(), key.ids.end(), typeId);
    key.ids.insert(pos, typeId);
    return key;
}

ArchetypeKey World::makeKeyWithout(const Archetype* arch, ComponentTypeId typeId) const
{
    DAEDALUS_ASSERT(arch != nullptr, "makeKeyWithout: null archetype");
    ArchetypeKey key;
    key.ids.reserve(arch->componentIds.size() - 1);
    for (ComponentTypeId id : arch->componentIds)
    {
        if (id != typeId)
        {
            key.ids.push_back(id);
        }
    }
    return key;
}

World::Archetype* World::getOrCreateArchetype(const ArchetypeKey& key)
{
    const auto it = m_archetypes.find(key);
    if (it != m_archetypes.end())
    {
        return it->second.get();
    }

    auto arch          = std::make_unique<Archetype>();
    arch->componentIds = key.ids;

    for (ComponentTypeId typeId : key.ids)
    {
        const auto sizeIt = m_componentSizes.find(typeId);
        DAEDALUS_ASSERT(sizeIt != m_componentSizes.end(),
                        "getOrCreateArchetype: unknown component size; "
                        "call addComponent<T> to register the type first");

        ComponentArray arr;
        arr.elementSize = sizeIt->second;
        arch->arrays.emplace(typeId, std::move(arr));
    }

    Archetype* ptr = arch.get();
    m_archetypes.emplace(key, std::move(arch));
    return ptr;
}

u32 World::moveEntityToArchetype(EntityId   entity,
                                  Archetype* from,
                                  u32        fromRow,
                                  Archetype* to)
{
    const u32 toRow = to->count;

    // Append entity handle.
    to->entities.push_back(entity);

    // Copy components that exist in both archetypes; zero-initialise new ones.
    for (ComponentTypeId typeId : to->componentIds)
    {
        ComponentArray& toArr = to->arrays.at(typeId);

        if (from != nullptr && archetypeHasType(*from, typeId))
        {
            toArr.copyFrom(from->arrays.at(typeId), fromRow);
        }
        else
        {
            // Reserve uninitialised space — the caller will placement-new here.
            const usize pos = toArr.data.size();
            toArr.data.resize(pos + toArr.elementSize, byte{0});
        }
    }

    ++to->count;

    // Swap-remove entity from the source archetype.
    if (from != nullptr)
    {
        const u32 fromLastRow = from->count - 1;

        for (auto& [typeId, arr] : from->arrays)
        {
            arr.swapRemove(fromRow);
        }

        if (fromRow != fromLastRow)
        {
            EntityId movedEntity            = from->entities[fromLastRow];
            from->entities[fromRow]         = movedEntity;
            m_records[entityIndex(movedEntity)].row = fromRow;
        }
        from->entities.resize(fromLastRow);
        --from->count;
    }

    return toRow;
}

} // namespace daedalus
