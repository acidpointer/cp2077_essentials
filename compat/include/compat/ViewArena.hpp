#pragma once
#ifdef _WIN32
#include <compat/Domain.hpp>
#include <compat/LiveMemory.hpp>
#include <array>

namespace compat
{
// Independently protected page ranges per allocation, reserved for the entire
// arena lifetime. Provisional storage is writable until a typed boundary promotes
// the original address. Promotion/retirement require exclusive guest ownership.
class ViewArena
{
public:
    ViewArena(const Schema& schema, size_t capacity, size_t maxAllocationBytes = 0);
    ~ViewArena();
    ViewArena(const ViewArena&) = delete;
    ViewArena& operator=(const ViewArena&) = delete;
    void* Allocate(Domain::Ptr domain, size_t bytes);
    Error ValidateAllocation(void* guest, uint32_t domain, size_t bytes);
    Error Promote(void* guest, TypeId type, void* native, size_t count = 1, uint32_t expectedDomain = 0);
    // A pointer-element buffer followed by the reviewed allocator slot. Both
    // mappings become visible together; padding between them is inaccessible.
    Error PromoteArray(void* guest, TypeId element, void* native, size_t count,
                       TypeId allocatorSlot, uint32_t expectedDomain);
    void* Borrow(Domain::Ptr domain, TypeId type, void* native, size_t count = 1);
    Error Retire(void* guest);
    bool Owns(uintptr_t address) const noexcept;
    size_t allocation_limit() const noexcept { return stride_; }
    LiveMemory& memory() noexcept { return memory_; }
private:
    enum class State { Unused, Provisional, Live, Retired };
    struct Slot
    {
        Domain::Ptr domain;
        Domain::Ticket owner;
        size_t bytes{};
        std::array<uintptr_t,2> mappings{};
        size_t mappingCount{};
        State state{State::Unused};
    };
    const Schema& schema_;
    LiveMemory memory_;
    std::unique_ptr<Slot[]> slots_;
    std::byte* base_{};
    size_t page_{}, stride_{}, capacity_, used_{};
    std::mutex control_;
    Error PromoteParts(void* guest, std::span<const LiveMemory::Mapping> parts, uint32_t expectedDomain);
};
}
#endif
