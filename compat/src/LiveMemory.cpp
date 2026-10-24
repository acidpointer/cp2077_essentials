#include <compat/LiveMemory.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace compat
{
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<size_t>::is_always_lock_free);
namespace
{
size_t Extent(const LiveMemory::Mapping& map, Direction direction) noexcept
{
    return map.count * (map.identity ? 1 : direction == Direction::ToNative ?
                       map.type->guest_size : map.type->native_size);
}
bool Overlaps(uintptr_t a, size_t an, uintptr_t b, size_t bn) noexcept
{ return a < b + bn && b < a + an; }
const Field* FindField(const Type& type, size_t offset) noexcept
{
    for (const auto& field : type.fields)
        if (offset >= field.guest_offset && offset - field.guest_offset < field.width) return &field;
    return nullptr;
}
#ifdef _WIN32
__declspec(noinline)
#endif
bool NativeCopy(void* destination,const void* source,size_t size) noexcept
{
#ifdef _WIN32
    // Catch invalid native backing at the leaf, before it can unwind across an
    // access lease or leave the exception handler's reentrancy state set.
    __try { std::memcpy(destination,source,size); }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return false; }
#else
    std::memcpy(destination,source,size);
#endif
    return true;
}
template<class T> uint64_t AtomicWord(void* memory, LiveMemory::AtomicOp op, uint64_t value, uint64_t compare) noexcept
{
    // Backing must contain a live, suitably aligned scalar object. All concurrent
    // accesses must obey the caller's C++/engine atomic lifetime contract.
    static_assert(std::atomic_ref<T>::is_always_lock_free);
    std::atomic_ref<T> word(*static_cast<T*>(memory));
    const auto operand = static_cast<T>(value);
    switch (op)
    {
    case LiveMemory::AtomicOp::Add: return word.fetch_add(operand);
    case LiveMemory::AtomicOp::Or: return word.fetch_or(operand);
    case LiveMemory::AtomicOp::And: return word.fetch_and(operand);
    case LiveMemory::AtomicOp::Xor: return word.fetch_xor(operand);
    case LiveMemory::AtomicOp::Exchange: return word.exchange(operand);
    case LiveMemory::AtomicOp::CompareExchange:
        { auto expected = static_cast<T>(compare); word.compare_exchange_strong(expected, operand); return expected; }
    }
    return 0;
}
#ifdef _WIN32
__declspec(noinline)
#endif
bool NativeAtomic(void* pointer,size_t width,LiveMemory::AtomicOp op,uint64_t operand,uint64_t compare,uint64_t& previous) noexcept
{
#ifdef _WIN32
    __try
#endif
    {
        switch(width)
        {
        case 1: previous=AtomicWord<uint8_t>(pointer,op,operand,compare); break;
        case 2: previous=AtomicWord<uint16_t>(pointer,op,operand,compare); break;
        case 4: previous=AtomicWord<uint32_t>(pointer,op,operand,compare); break;
        case 8: previous=AtomicWord<uint64_t>(pointer,op,operand,compare); break;
        }
    }
#ifdef _WIN32
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return false; }
#endif
    return true;
}
}

LiveMemory::LiveMemory(const Schema& schema, size_t capacity) : schema_(schema), capacity_(capacity)
{
    Check(schema.Validate());
    if (!capacity || capacity > 4096) throw Failure(Error::Capacity);
    records_ = std::make_unique<Record[]>(capacity);
}
LiveMemory::~LiveMemory()
{
    for (size_t i = 0; i < size_.load(); ++i)
    {
        records_[i].state.fetch_or(Closed);
        while (records_[i].state.load(std::memory_order_acquire) != Closed) std::this_thread::yield();
    }
}
bool LiveMemory::Lease::Acquire(const Record& candidate) noexcept
{
    auto state = candidate.state.load(std::memory_order_acquire);
    // Bounded retry: contention must not turn a fault handler into a spin lock.
    for (unsigned retry = 0; retry < 64 && !(state & Closed); ++retry)
        if (candidate.state.compare_exchange_weak(state, state + 1, std::memory_order_acquire))
        { record = &candidate; return true; }
    return false;
}
Error LiveMemory::Add(const Mapping& map) { return AddBatch({&map,1}); }
Error LiveMemory::AddBatch(std::span<const Mapping> mappings)
{
    if (mappings.empty()) return Error::InvalidRange;
    std::lock_guard guard(writer_);
    const auto count = size_.load(std::memory_order_relaxed);
    if (mappings.size() > capacity_ - count) return Error::Capacity;
    for (size_t n = 0; n < mappings.size(); ++n)
    {
        const auto& map = mappings[n];
        if (!map.domain || !map.generation || !map.type || !map.guest || !map.native || !map.count)
            return Error::InvalidRange;
        if (schema_.Find(map.type->id) != map.type) return Error::InvalidSchema;
        if (map.identity ? map.type->policy != Policy::Opaque :
            map.type->policy != Policy::LiveView || map.type->fields.empty()) return Error::InvalidSchema;
        const auto gs = map.identity ? 1 : map.type->guest_size;
        const auto ns = map.identity ? 1 : map.type->native_size;
        if (!gs || !ns || map.count > SIZE_MAX / gs || map.count > SIZE_MAX / ns ||
            map.guest > UINTPTR_MAX - map.count * gs || map.native > UINTPTR_MAX - map.count * ns)
            return Error::InvalidRange;
        if (!map.identity && (map.guest % map.type->alignment || map.native % map.type->alignment))
            return Error::InvalidRange;
        for (size_t i = 0; i < count + n; ++i)
        {
            const auto& existing = i < count ? records_[i].mapping : mappings[i-count];
            if (Overlaps(map.guest, map.count * gs, existing.guest, Extent(existing, Direction::ToNative)))
                return Error::Overlap;
            if (map.domain == existing.domain &&
                Overlaps(map.native, map.count * ns, existing.native, Extent(existing, Direction::ToGuest)))
            {
                if (i >= count || !(records_[i].state.load(std::memory_order_acquire) & Closed)) return Error::Overlap;
                if (map.generation <= existing.generation) return Error::IdentityMismatch;
            }
        }
    }
    for (size_t n = 0; n < mappings.size(); ++n)
    {
        records_[count+n].mapping = mappings[n];
        records_[count+n].state.store(0,std::memory_order_release);
    }
    size_.store(count+mappings.size(),std::memory_order_release);
    return Error::None;
}
Error LiveMemory::Retire(uint64_t domain, uintptr_t guest, uint64_t generation)
{
    std::lock_guard guard(writer_);
    for (size_t i = 0; i < size_.load(std::memory_order_relaxed); ++i)
    {
        auto& record = records_[i];
        if (record.mapping.domain != domain || record.mapping.guest != guest) continue;
        if (record.mapping.generation != generation) return Error::IdentityMismatch;
        if (record.state.fetch_or(Closed, std::memory_order_acq_rel) & Closed) return Error::Retired;
        while (record.state.load(std::memory_order_acquire) != Closed) std::this_thread::yield();
        return Error::None;
    }
    return Error::UnknownPointer;
}
Error LiveMemory::Find(uintptr_t address, size_t width, Lease& lease) const noexcept
{
    if (!width || address > UINTPTR_MAX - width) return Error::InvalidRange;
    const auto count = size_.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& map = records_[i].mapping;
        const auto size = Extent(map, Direction::ToNative);
        if (address < map.guest || address - map.guest >= size) continue;
        if (width > size - (address - map.guest)) return Error::InvalidRange;
        if (lease.Acquire(records_[i])) return Error::None;
        return records_[i].state.load(std::memory_order_relaxed) & Closed ? Error::Retired : Error::Busy;
    }
    return Error::UnknownPointer;
}
bool LiveMemory::Contains(uintptr_t guest) const noexcept
{
    Lease lease;
    const auto result = Find(guest, 1, lease);
    return result != Error::UnknownPointer && result != Error::InvalidRange;
}
Error LiveMemory::Translate(uint64_t domain, TypeId type, Direction direction,
                            uintptr_t source, uintptr_t& target) const noexcept
{
    if (!source) { target = 0; return Error::None; }
    const auto count = size_.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& map = records_[i].mapping;
        if (map.domain != domain || map.type->id != type) continue;
        const auto base = direction == Direction::ToNative ? map.guest : map.native;
        const auto extent = Extent(map, direction);
        if (source < base || source - base >= extent) continue;
        Lease lease;
        if (!lease.Acquire(records_[i])) continue;
        const auto offset = source - base;
        const auto stride = map.identity ? 1 : direction == Direction::ToNative ?
            map.type->guest_size : map.type->native_size;
        if (offset % stride) return Error::UnsupportedAccess;
        const auto destStride = map.identity ? 1 : direction == Direction::ToNative ?
            map.type->native_size : map.type->guest_size;
        target = (direction == Direction::ToNative ? map.native : map.guest) + offset / stride * destStride;
        return Error::None;
    }
    return Error::UnknownPointer;
}
Error LiveMemory::Read(uintptr_t guest, std::span<std::byte> destination) const noexcept
{ return Transfer(guest, destination, false); }
Error LiveMemory::Write(uintptr_t guest, std::span<const std::byte> source) const noexcept
{
    if (source.size() > 32) return Error::UnsupportedAccess;
    std::array<std::byte, 32> scratch{};
    std::memcpy(scratch.data(), source.data(), source.size());
    return Transfer(guest, {scratch.data(), source.size()}, true);
}
Error LiveMemory::Transfer(uintptr_t address, std::span<std::byte> bytes, bool write) const noexcept
{
    if (bytes.empty() || bytes.size() > 32) return Error::UnsupportedAccess;
    Lease lease;
    if (auto error = Find(address, bytes.size(), lease); error != Error::None) return error;
    const auto& map = lease.record->mapping;
    if (map.identity) return Error::WrongKind;
    struct Segment { uintptr_t native{}; size_t offset{}, size{}; };
    std::array<Segment, 32> segments{};
    std::array<std::byte, 32> converted{};
    size_t segmentCount = 0;
    for (size_t done = 0; done < bytes.size();)
    {
        const auto offset = address - map.guest + done;
        const auto index = offset / map.type->guest_size;
        const auto member = offset % map.type->guest_size;
        const auto* field = FindField(*map.type, member);
        if (!field) return Error::UnsupportedAccess;
        const auto inside = member - field->guest_offset;
        const auto size = std::min(bytes.size() - done, field->width - inside);
        auto* native = reinterpret_cast<std::byte*>(map.native + index * map.type->native_size + field->native_offset + inside);
        if (field->kind == FieldKind::Padding) { done += size; continue; }
        if (field->kind == FieldKind::Pointer)
        {
            // Partial pointer stores have no meaningful bidirectional mapping.
            if (write && (inside || size != sizeof(uintptr_t))) return Error::UnsupportedAccess;
            uintptr_t source{}, target{};
            if (write) std::memcpy(&source,bytes.data()+done,sizeof(source));
            else if (!NativeCopy(&source,native-inside,sizeof(source))) return Error::NativeFault;
            if (!source && !field->nullable) return Error::UnsupportedValue;
            TypeId pointee = field->pointee;
            if (field->empty_pointee)
            {
                uint32_t capacity{};
                if (!NativeCopy(&capacity,reinterpret_cast<const void*>(map.native + index * map.type->native_size + field->capacity_native),4))
                    return Error::NativeFault;
                // A single vector store may replace both the pointer and capacity.
                // Interpret the pointer using the fully supplied new discriminator.
                for (const auto& count : map.type->fields)
                    if (write && count.kind == FieldKind::Scalar && count.native_offset == field->capacity_native)
                    {
                        const auto pos = map.guest + index * map.type->guest_size + count.guest_offset;
                        if (pos >= address && pos - address <= bytes.size() && 4 <= bytes.size() - (pos-address))
                            std::memcpy(&capacity,bytes.data()+(pos-address),4);
                    }
                if (!capacity) pointee = field->empty_pointee;
            }
            if (auto error = Translate(map.domain, pointee, write ? Direction::ToNative : Direction::ToGuest,
                                       source, target); error != Error::None) return error;
            std::memcpy(converted.data() + done, reinterpret_cast<const std::byte*>(&target) + inside, size);
        }
        else
        {
            if (write) std::memcpy(converted.data()+done,bytes.data()+done,size);
            else if (!NativeCopy(converted.data()+done,native,size)) return Error::NativeFault;
            if (field->kind == FieldKind::Zero)
                for (size_t byte = 0; byte < size; ++byte)
                    if (converted[done+byte] != std::byte{}) return Error::UnsupportedValue;
        }
        segments[segmentCount++] = {reinterpret_cast<uintptr_t>(native), done, size};
        done += size;
    }
    // Nothing changes until every field and pointer in the instruction passes.
    if (write)
    {
        for (size_t i = 0; i < segmentCount; ++i)
            if (!NativeCopy(reinterpret_cast<void*>(segments[i].native), converted.data() + segments[i].offset, segments[i].size))
                return Error::NativeFault;
    }
    else std::memcpy(bytes.data(), converted.data(), bytes.size());
    return Error::None;
}
Error LiveMemory::Atomic(uintptr_t guest, size_t width, AtomicOp op, uint64_t operand,
                         uint64_t compare, uint64_t& previous) const noexcept
{
    if (width != 1 && width != 2 && width != 4 && width != 8) return Error::UnsupportedAccess;
    if (op!=AtomicOp::Add && op!=AtomicOp::Or && op!=AtomicOp::And && op!=AtomicOp::Xor &&
        op!=AtomicOp::Exchange && op!=AtomicOp::CompareExchange) return Error::UnsupportedAccess;
    Lease lease;
    if (auto error = Find(guest, width, lease); error != Error::None) return error;
    const auto& map = lease.record->mapping;
    if (map.identity) return Error::WrongKind;
    const auto offset = guest - map.guest;
    const auto member = offset % map.type->guest_size;
    const auto* field = FindField(*map.type, member);
    if (!field || field->kind != FieldKind::Scalar || width > field->width - (member - field->guest_offset))
        return Error::UnsupportedAccess;
    const auto native = map.native + offset / map.type->guest_size * map.type->native_size +
        field->native_offset + member - field->guest_offset;
    if (guest % width || native % width) return Error::UnsupportedAccess;
    if (!NativeAtomic(reinterpret_cast<void*>(native),width,op,operand,compare,previous))return Error::NativeFault;
    return Error::None;
}
}
