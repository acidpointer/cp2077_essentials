#ifdef _WIN32
#include <compat/ViewArena.hpp>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace compat
{
ViewArena::ViewArena(const Schema& schema, size_t capacity, size_t maxAllocationBytes)
    : schema_(schema), memory_(schema, capacity), slots_(std::make_unique<Slot[]>(capacity)), capacity_(capacity)
{
    SYSTEM_INFO system{}; GetSystemInfo(&system); page_ = system.dwPageSize;
    if (!maxAllocationBytes) maxAllocationBytes = page_;
    if (maxAllocationBytes > 16*1024*1024 || maxAllocationBytes > SIZE_MAX-page_+1) throw Failure(Error::InvalidRange);
    stride_ = (maxAllocationBytes+page_-1)/page_*page_;
    if (capacity > SIZE_MAX/stride_) throw Failure(Error::InvalidRange);
    base_ = static_cast<std::byte*>(VirtualAlloc(nullptr, capacity * stride_, MEM_RESERVE, PAGE_NOACCESS));
    if (!base_) throw Failure(Error::Capacity);
}
ViewArena::~ViewArena()
{
    // Caller must first detach the backend and drain guest execution. The arena
    // cannot promise address quarantine after its own process-lifetime ends.
    for (size_t i = 0; i < used_; ++i) (void)Retire(base_ + i * stride_);
    VirtualFree(base_, 0, MEM_RELEASE);
}
void* ViewArena::Allocate(Domain::Ptr domain, size_t bytes)
{
    if (!domain || !bytes || bytes > stride_) throw Failure(Error::InvalidRange);
    if (domain->source() != schema_.source || domain->target() != schema_.target) throw Failure(Error::IdentityMismatch);
    auto owner = domain->Retain();
    std::lock_guard guard(control_);
    if (used_ == capacity_) throw Failure(Error::Capacity);
    const auto index = used_++; // A failed allocation also consumes its address.
    auto& slot = slots_[index];
    slot.state = State::Retired;
    auto* guest = base_ + index * stride_;
    if (!VirtualAlloc(guest,(bytes+page_-1)/page_*page_,MEM_COMMIT,PAGE_READWRITE)) throw Failure(Error::Capacity);
    slot.domain = std::move(domain); slot.owner = std::move(owner);
    slot.bytes = bytes; slot.state = State::Provisional;
    return guest;
}
Error ViewArena::ValidateAllocation(void* guest, uint32_t domain, size_t bytes)
{
    std::lock_guard guard(control_);
    const auto address=reinterpret_cast<uintptr_t>(guest);
    if (!Owns(address) || (address-reinterpret_cast<uintptr_t>(base_))%stride_) return Error::UnknownPointer;
    const auto& slot=slots_[(address-reinterpret_cast<uintptr_t>(base_))/stride_];
    if (slot.state!=State::Provisional) return Error::Retired;
    return slot.domain->id()==domain && slot.bytes==bytes ? Error::None : Error::IdentityMismatch;
}
Error ViewArena::Promote(void* guest, TypeId id, void* native, size_t count, uint32_t expectedDomain)
{
    const auto* type = schema_.Find(id);
    if (!type || !count || type->guest_size > stride_/count) return Error::InvalidRange;
    LiveMemory::Mapping part{0,0,type,reinterpret_cast<uintptr_t>(guest),reinterpret_cast<uintptr_t>(native),count};
    return PromoteParts(guest,{&part,1},expectedDomain);
}
Error ViewArena::PromoteArray(void* guest, TypeId id, void* native, size_t count, TypeId allocatorSlot, uint32_t expectedDomain)
{
    const auto* type=schema_.Find(id);const auto* tail=schema_.Find(allocatorSlot);
    if (!type || !tail || !count || type->guest_size>stride_/count || type->native_size>SIZE_MAX/count ||
        tail->guest_size!=8 || tail->native_size!=8) return Error::InvalidRange;
    const auto bytes=count*type->guest_size, nativeBytes=count*type->native_size;
    if (bytes>stride_-8 || nativeBytes>SIZE_MAX-8 || bytes%8 || nativeBytes%8 ||
        reinterpret_cast<uintptr_t>(native)>UINTPTR_MAX-nativeBytes-8) return Error::InvalidRange;
    const LiveMemory::Mapping parts[]{
        {0,0,type,reinterpret_cast<uintptr_t>(guest),reinterpret_cast<uintptr_t>(native),count},
        {0,0,tail,reinterpret_cast<uintptr_t>(guest)+bytes,reinterpret_cast<uintptr_t>(native)+nativeBytes,1}};
    return PromoteParts(guest,parts,expectedDomain);
}
Error ViewArena::PromoteParts(void* guest, std::span<const LiveMemory::Mapping> parts, uint32_t expectedDomain)
{
    std::lock_guard guard(control_);
    const auto address=reinterpret_cast<uintptr_t>(guest);
    if (!Owns(address) || (address-reinterpret_cast<uintptr_t>(base_))%stride_) return Error::UnknownPointer;
    const auto index=(address-reinterpret_cast<uintptr_t>(base_))/stride_;
    auto& slot=slots_[index];
    if (slot.state!=State::Provisional) return Error::Retired;
    if (expectedDomain && slot.domain->id()!=expectedDomain) return Error::IdentityMismatch;
    if (parts.empty() || parts.size()>2) return Error::InvalidRange;
    std::array<LiveMemory::Mapping,2> maps{};
    size_t extent{};
    for (size_t i=0;i<parts.size();++i)
    {
        maps[i]=parts[i];maps[i].domain=slot.domain->id();maps[i].generation=index+1;
        extent=maps[i].guest-address+maps[i].count*maps[i].type->guest_size;
        if (extent>slot.bytes) return Error::InvalidRange;
    }
    if (extent!=slot.bytes) return Error::InvalidRange;
    auto result=memory_.AddBatch({maps.data(),parts.size()});
    if (result!=Error::None) return result;
    DWORD previous{};
    if (!VirtualProtect(guest,(slot.bytes+page_-1)/page_*page_,PAGE_NOACCESS,&previous))
    {
        for (size_t i=0;i<parts.size();++i) (void)memory_.Retire(slot.domain->id(),maps[i].guest,index+1);
        slot.state=State::Retired;slot.owner={};slot.domain.reset();return Error::UnsupportedAccess;
    }
    for(size_t i=0;i<parts.size();++i)slot.mappings[i]=maps[i].guest;
    slot.mappingCount=parts.size();slot.state=State::Live;return Error::None;
}
void* ViewArena::Borrow(Domain::Ptr domain, TypeId id, void* native, size_t count)
{
    const auto* type = schema_.Find(id);
    if (!type || !count || type->guest_size > stride_ / count) throw Failure(Error::InvalidRange);
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
    if (!Owns(address) || (address - reinterpret_cast<uintptr_t>(base_)) % stride_) return Error::UnknownPointer;
    const auto index = (address - reinterpret_cast<uintptr_t>(base_)) / stride_;
    auto& slot = slots_[index];
    if (slot.state == State::Unused || slot.state == State::Retired) return Error::Retired;
    if (slot.state == State::Live)
    {
        for(size_t i=0;i<slot.mappingCount;++i) Check(memory_.Retire(slot.domain->id(),slot.mappings[i],index+1));
    }
    else
    {
        DWORD previous{};
        if (!VirtualProtect(guest, (slot.bytes+page_-1)/page_*page_, PAGE_NOACCESS, &previous)) return Error::UnsupportedAccess;
    }
    slot.state = State::Retired; slot.owner = {}; slot.domain.reset();
    return Error::None;
}
bool ViewArena::Owns(uintptr_t address) const noexcept
{
    return address >= reinterpret_cast<uintptr_t>(base_) && address - reinterpret_cast<uintptr_t>(base_) < capacity_ * stride_;
}
}
#endif
