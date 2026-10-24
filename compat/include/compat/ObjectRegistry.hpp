#pragma once
#include <compat/Schema.hpp>
#include <shared_mutex>
#include <vector>

namespace compat
{
struct Mapping
{
    uint32_t domain;
    TypeId type;
    uint64_t generation;
    uintptr_t guest, native;
    size_t guest_size, native_size;
    bool linear_bytes{}, allow_end{};
};

// Bounded control-plane implementation. ReadGuard holds lifetimes steady during
// conversion; Retire waits for readers. NOT an exception-handler lookup index.
// Caller owns backing memory and guest address reservations, and must Retire
// before releasing native backing. Guest address ranges cannot be reused.
class ObjectRegistry
{
public:
    ObjectRegistry(const Schema& schema, size_t capacity) : schema_(schema), capacity_(capacity)
    { Check(schema.Validate()); entries_.reserve(capacity); }
    Error Add(Mapping mapping);
    Error Retire(uint32_t domain, uintptr_t guest, uint64_t generation);
    class ReadGuard
    {
    public:
        Error Translate(uint32_t domain, TypeId type, Direction direction, uintptr_t source,
                        uintptr_t& target) const noexcept;
    private:
        friend class ObjectRegistry;
        explicit ReadGuard(const ObjectRegistry& registry) : registry_(registry), lock_(registry.mutex_) {}
        const ObjectRegistry& registry_;
        std::shared_lock<std::shared_mutex> lock_;
    };
    ReadGuard Read() const { return ReadGuard(*this); }
private:
    struct Entry { Mapping mapping; bool live; };
    const Schema& schema_;
    size_t capacity_;
    mutable std::shared_mutex mutex_;
    std::vector<Entry> entries_;
};
}
