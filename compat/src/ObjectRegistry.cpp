#include <compat/ObjectRegistry.hpp>
#include <limits>
#include <mutex>

namespace compat
{
namespace
{
bool Valid(uintptr_t base, size_t size) { return base && size && size <= std::numeric_limits<uintptr_t>::max() - base; }
bool Overlaps(uintptr_t a, size_t an, uintptr_t b, size_t bn) { return a < b + bn && b < a + an; }
}
Error ObjectRegistry::Add(Mapping map)
{
    const auto* type = schema_.Find(map.type);
    if (!type || (type->guest_size && type->guest_size != map.guest_size) ||
        (type->native_size && type->native_size != map.native_size)) return Error::InvalidSchema;
    if (!map.domain || !map.type || !map.generation || !Valid(map.guest, map.guest_size) ||
        !Valid(map.native, map.native_size) || (map.allow_end && !map.linear_bytes) ||
        (map.linear_bytes && map.guest_size != map.native_size)) return Error::InvalidRange;
    std::unique_lock lock(mutex_);
    for (const auto& entry : entries_)
    {
        const auto& old = entry.mapping;
        if (Overlaps(map.guest, map.guest_size, old.guest, old.guest_size)) return Error::Overlap;
        if (old.domain != map.domain) continue;
        if (Overlaps(map.native, map.native_size, old.native, old.native_size))
        {
            if (entry.live) return Error::Overlap;
            if (map.generation <= old.generation) return Error::IdentityMismatch;
        }
    }
    if (entries_.size() == capacity_) return Error::Capacity;
    entries_.push_back({map, true});
    return Error::None;
}

Error ObjectRegistry::Retire(uint32_t domain, uintptr_t guest, uint64_t generation)
{
    std::unique_lock lock(mutex_);
    for (auto& entry : entries_)
        if (entry.mapping.domain == domain && entry.mapping.guest == guest && entry.mapping.generation == generation)
        { if (!entry.live) return Error::Retired; entry.live = false; return Error::None; }
    return Error::UnknownPointer;
}

Error ObjectRegistry::ReadGuard::Translate(uint32_t domain, TypeId type, Direction direction,
                                          uintptr_t source, uintptr_t& target) const noexcept
{
    if (!source) { target = 0; return Error::None; }
    bool found = false;
    uintptr_t result{};
    for (const auto& entry : registry_.entries_)
    {
        const auto& map = entry.mapping;
        if (!entry.live || map.domain != domain || !registry_.schema_.IsA(map.type,type)) continue;
        auto from = direction == Direction::ToNative ? map.guest : map.native;
        auto to = direction == Direction::ToNative ? map.native : map.guest;
        auto size = direction == Direction::ToNative ? map.guest_size : map.native_size;
        if (source < from) continue;
        auto offset = source - from;
        if (offset != 0 && (map.type != type || !map.linear_bytes || offset > size ||
                            (offset == size && !map.allow_end))) continue;
        if (found) return Error::AmbiguousPointer;
        found = true; result = to + offset;
    }
    if (!found) return Error::UnknownPointer;
    target = result;
    return Error::None;
}
}
