#pragma once
#include <compat/Schema.hpp>
#include <atomic>
#include <memory>
#include <mutex>

namespace compat
{
// Data-plane index: fixed storage, immutable records, no locks/allocations in
// Read/Write/Translate/Atomic. Control-plane callers own native backing lifetimes.
// Guest addresses are reserved until destruction, including retired records.
class LiveMemory
{
public:
    enum class AtomicOp { Add, Or, And, Xor, Exchange, CompareExchange };
    struct Mapping
    {
        uint64_t domain, generation;
        const Type* type;
        uintptr_t guest, native;
        size_t count{1};
        bool identity{}; // Explicit bounded byte identity, only for Opaque types.
    };
    LiveMemory(const Schema& schema, size_t capacity);
    ~LiveMemory();
    LiveMemory(const LiveMemory&) = delete;
    LiveMemory& operator=(const LiveMemory&) = delete;
    Error Add(const Mapping& mapping);
    Error Retire(uint64_t domain, uintptr_t guest, uint64_t generation);
    Error Translate(uint64_t domain, TypeId type, Direction direction,
                    uintptr_t source, uintptr_t& target) const noexcept;
    Error Read(uintptr_t guest, std::span<std::byte> destination) const noexcept;
    Error Write(uintptr_t guest, std::span<const std::byte> source) const noexcept;
    Error Atomic(uintptr_t guest, size_t width, AtomicOp op, uint64_t operand,
                 uint64_t compare, uint64_t& previous) const noexcept;
    bool Contains(uintptr_t guest) const noexcept;
private:
    static constexpr uint64_t Closed = uint64_t{1} << 63;
    struct Record
    {
        Mapping mapping{};
        mutable std::atomic<uint64_t> state{Closed};
    };
    struct Lease
    {
        const Record* record{};
        Lease() = default;
        ~Lease() { if (record) record->state.fetch_sub(1, std::memory_order_release); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        bool Acquire(const Record& candidate) noexcept;
    };
    Error Find(uintptr_t address, size_t width, Lease& lease) const noexcept;
    Error Transfer(uintptr_t address, std::span<std::byte> bytes, bool write) const noexcept;
    std::unique_ptr<Record[]> records_;
    const Schema& schema_;
    size_t capacity_;
    std::atomic<size_t> size_{};
    std::mutex writer_; // Never used by the data plane.
};
}
