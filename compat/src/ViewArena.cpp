#ifdef _WIN32
#include <compat/ViewArena.hpp>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace compat
{
ViewArena::ViewArena(const Schema& schema, size_t capacity)
    : schema_(schema), memory_(schema, capacity), slots_(std::make_unique<Slot[]>(capacity)), capacity_(capacity)
{
    SYSTEM_INFO system{}; GetSystemInfo(&system); page_ = system.dwPageSize;
    base_ = static_cast<std::byte*>(VirtualAlloc(nullptr, capacity * page_, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));
    if (!base_) throw Failure(Error::Capacity);
}
ViewArena::~ViewArena()
{
    // Caller must first detach the backend and drain guest execution. The arena
    // cannot promise address quarantine after its own process-lifetime ends.
    for (size_t i = 0; i < used_; ++i) (void)Retire(base_ + i * page_);
    VirtualFree(base_, 0, MEM_RELEASE);
}
void* ViewArena::Allocate(Domain::Ptr domain, size_t bytes)
{
    if (!domain || !bytes || bytes > page_) throw Failure(Error::InvalidRange);
    if (domain->source() != schema_.source || domain->target() != schema_.target) throw Failure(Error::IdentityMismatch);
    auto owner = domain->Retain();
    std::lock_guard guard(control_);
    if (used_ == capacity_) throw Failure(Error::Capacity);
    const auto index = used_++; // A failed allocation also consumes its address.
    auto& slot = slots_[index];
    slot.state = State::Retired;
    auto* guest = base_ + index * page_;
    DWORD previous{};
    if (!VirtualProtect(guest, page_, PAGE_READWRITE, &previous)) throw Failure(Error::Capacity);
    slot.domain = std::move(domain); slot.owner = std::move(owner);
    slot.bytes = bytes; slot.state = State::Provisional;
    return guest;
}
Error ViewArena::ValidateAllocation(void* guest, uint32_t domain, size_t bytes)
{
    std::lock_guard guard(control_);
    const auto address=reinterpret_cast<uintptr_t>(guest);
    if (!Owns(address) || (address-reinterpret_cast<uintptr_t>(base_))%page_) return Error::UnknownPointer;
    const auto& slot=slots_[(address-reinterpret_cast<uintptr_t>(base_))/page_];
    if (slot.state!=State::Provisional) return Error::Retired;
    return slot.domain->id()==domain && slot.bytes==bytes ? Error::None : Error::IdentityMismatch;
}
Error ViewArena::Promote(void* guest, TypeId id, void* native, size_t count, uint32_t expectedDomain)
{
    std::lock_guard guard(control_);
    const auto address = reinterpret_cast<uintptr_t>(guest);
    if (!Owns(address) || (address - reinterpret_cast<uintptr_t>(base_)) % page_) return Error::UnknownPointer;
    const auto index = (address - reinterpret_cast<uintptr_t>(base_)) / page_;
    auto& slot = slots_[index];
    if (slot.state != State::Provisional) return Error::Retired;
    if (expectedDomain && slot.domain->id()!=expectedDomain) return Error::IdentityMismatch;
    const auto* type = schema_.Find(id);
    if (!type || !count || type->guest_size > slot.bytes / count || type->guest_size * count != slot.bytes)
        return Error::InvalidRange;
    auto result = memory_.Add({slot.domain->id(), index + 1, type, address, reinterpret_cast<uintptr_t>(native), count});
    if (result != Error::None) return result;
    DWORD previous{};
    if (!VirtualProtect(guest, page_, PAGE_NOACCESS, &previous))
    {
        (void)memory_.Retire(slot.domain->id(), address, index + 1);
        slot.state = State::Retired; slot.owner = {}; slot.domain.reset();
        return Error::UnsupportedAccess;
    }
    slot.state = State::Live;
    return Error::None;
}
void* ViewArena::Borrow(Domain::Ptr domain, TypeId id, void* native, size_t count)
{
    const auto* type = schema_.Find(id);
    if (!type || !count || type->guest_size > page_ / count) throw Failure(Error::InvalidRange);
    uintptr_t existing{};
    if (domain && memory_.Translate(domain->id(), id, Direction::ToGuest, reinterpret_cast<uintptr_t>(native), existing) == Error::None)
    {
        uintptr_t end{};
        if (reinterpret_cast<uintptr_t>(native)>UINTPTR_MAX-(count-1)*type->native_size ||
            memory_.Translate(domain->id(),id,Direction::ToGuest,reinterpret_cast<uintptr_t>(native)+(count-1)*type->native_size,end)!=Error::None ||
            end!=existing+(count-1)*type->guest_size) throw Failure(Error::InvalidRange);
        return reinterpret_cast<void*>(existing);
    }
    auto* guest = Allocate(std::move(domain), type->guest_size * count);
    auto result = Promote(guest, id, native, count);
    if (result != Error::None) { (void)Retire(guest); throw Failure(result); }
    return guest;
}
Error ViewArena::Retire(void* guest)
{
    std::lock_guard guard(control_);
    const auto address = reinterpret_cast<uintptr_t>(guest);
    if (!Owns(address) || (address - reinterpret_cast<uintptr_t>(base_)) % page_) return Error::UnknownPointer;
    const auto index = (address - reinterpret_cast<uintptr_t>(base_)) / page_;
    auto& slot = slots_[index];
    if (slot.state == State::Unused || slot.state == State::Retired) return Error::Retired;
    if (slot.state == State::Live) Check(memory_.Retire(slot.domain->id(), address, index + 1));
    else
    {
        DWORD previous{};
        if (!VirtualProtect(guest, page_, PAGE_NOACCESS, &previous)) return Error::UnsupportedAccess;
    }
    slot.state = State::Retired; slot.owner = {}; slot.domain.reset();
    return Error::None;
}
bool ViewArena::Owns(uintptr_t address) const noexcept
{
    return address >= reinterpret_cast<uintptr_t>(base_) && address - reinterpret_cast<uintptr_t>(base_) < capacity_ * page_;
}
}
#endif
