#ifdef _WIN32
#include <compat/ArrayAdapter.hpp>
#include <compat/Profile.hpp>
#include <cstring>
#include <vector>

namespace compat
{
namespace
{
struct Allocation { void* memory; size_t size; };
struct Header { uintptr_t data; uint32_t capacity,size; };
static_assert(sizeof(Header)==16 && sizeof(Allocation)==16);

// The SDK copies just the allocator vptr, then calls it on that copied object.
// Real compiler-built virtual members preserve the Microsoft hidden-result ABI;
// they must never read instance fields beyond the copied vptr.
struct AllocatorAbi
{
    virtual Allocation Alloc(uint64_t) const = 0;
    virtual Allocation AllocAligned(uint64_t,uint32_t) const = 0;
    virtual Allocation Realloc(Allocation&,uint64_t) const = 0;
    virtual Allocation ReallocAligned(Allocation&,uint64_t,uint32_t) const = 0;
    virtual void Free(Allocation&) const = 0;
    virtual void Unknown(void*) const = 0;
    virtual uint32_t GetHandle() const = 0;
};
class AllocatorRoute
{
    struct Binding
    {
        Domain::Ticket owner;
        std::function<void(Allocation&)> free;
        uint32_t handle;
        Binding(const Domain::Ptr& domain,std::function<void(Allocation&)> fn,uint32_t value)
            : owner(domain->Retain()),free(std::move(fn)),handle(value) {}
        ~Binding() { Domain::Scope cleanup(owner);free={}; }
    };
    static constexpr size_t Capacity=64;
    static auto& Slots()
    { static auto* slots=new std::array<std::atomic<std::shared_ptr<Binding>>,Capacity>{};return *slots; }
    static auto Get(size_t index)
    {
        auto binding=Slots()[index].load(std::memory_order_acquire);
        if(!binding)throw Failure(Error::Retired);
        return binding;
    }
    template<size_t Index> struct Proxy final : AllocatorAbi
    {
        [[noreturn]] static void Reject() { auto b=Get(Index);Domain::Scope scope(b->owner);throw Failure(Error::UnknownOperation); }
        Allocation Alloc(uint64_t) const override { Reject(); }
        Allocation AllocAligned(uint64_t,uint32_t) const override { Reject(); }
        Allocation Realloc(Allocation&,uint64_t) const override { Reject(); }
        Allocation ReallocAligned(Allocation&,uint64_t,uint32_t) const override { Reject(); }
        void Free(Allocation& value) const override { auto b=Get(Index);Domain::Scope scope(b->owner);b->free(value); }
        void Unknown(void*) const override { Reject(); }
        uint32_t GetHandle() const override { auto b=Get(Index);Domain::Scope scope(b->owner);return b->handle; }
    };
    template<size_t Index> static uintptr_t Vtable()
    { static Proxy<Index> proxy;uintptr_t value{};std::memcpy(&value,static_cast<const void*>(&proxy),8);return value; }
    template<size_t... Index> static auto Tables(std::index_sequence<Index...>)
    { return std::array<uintptr_t,Capacity>{Vtable<Index>()...}; }
public:
    size_t index{Capacity};
    uintptr_t table{};
    void Bind(const Domain::Ptr& domain,std::function<void(Allocation&)> free,uint32_t handle)
    {
        auto binding=std::make_shared<Binding>(domain,std::move(free),handle);
        static const auto tables=Tables(std::make_index_sequence<Capacity>{});
        static std::atomic<size_t> next{};
        auto n=next.load();
        do { if(n==Capacity)throw Failure(Error::Capacity); } while(!next.compare_exchange_weak(n,n+1));
        index=n;table=tables[n];Slots()[n].store(std::move(binding),std::memory_order_release);
    }
    void Retire() noexcept
    { if(index<Capacity)Slots()[index].exchange({},std::memory_order_acq_rel);index=Capacity; }
};
}

struct ArrayAdapter::State
{
    struct Owner
    {
        uintptr_t guest{};
        Header* native{};
        TypeId element{};
        void* guestBuffer{};
        void* nativeBuffer{};
        uint32_t capacity{};
    };
    Domain::Ptr domain;
    Domain::Ticket owner;
    ViewArena& arena;
    SharedPointers pointers;
    ArrayServices services;
    AllocatorRoute allocator;
    std::vector<Owner> arrays;
    std::mutex control;
    bool mapped{},poisoned{};
    State(Domain::Ptr d,ViewArena& a,SharedPointers p,ArrayServices s)
        : domain(std::move(d)),owner(domain->Retain()),arena(a),pointers(std::move(p)),services(std::move(s)) {}
    ~State() { Domain::Scope cleanup(owner);services={};pointers.lifetime.reset(); }
    void Validate(const Header& h,TypeId element)
    {
        if(h.size>h.capacity || h.capacity>(UINT32_MAX-8)/8)throw Failure(Error::InvalidRange);
        if(!h.capacity)
        {
            if(h.data && h.data!=services.nativeAllocatorVtable)throw Failure(Error::IdentityMismatch);
            return;
        }
        const auto bytes=size_t(h.capacity)*8+8;
        if(bytes>arena.allocation_limit())throw Failure(Error::Capacity);
        if(!h.data || h.data%8 || h.data>UINTPTR_MAX-bytes)throw Failure(Error::InvalidRange);
        uintptr_t allocator{};
        std::memcpy(&allocator,reinterpret_cast<void*>(h.data+size_t(h.capacity)*8),8);
        if(allocator!=services.nativeAllocatorVtable)throw Failure(Error::IdentityMismatch);
        const auto pointee=element==profile::FunctionPointer ? profile::Function : profile::Property;
        for(uint32_t i=0;i<h.size;++i)
        {
            uintptr_t native{},guest{};std::memcpy(&native,reinterpret_cast<void*>(h.data+size_t(i)*8),8);
            auto result=arena.memory().Translate(domain->id(),pointee,Direction::ToGuest,native,guest);
            if(result==Error::UnknownPointer && services.prepareElement)
            {
                services.prepareElement(pointee,reinterpret_cast<void*>(native));
                result=arena.memory().Translate(domain->id(),pointee,Direction::ToGuest,native,guest);
            }
            Check(result);
        }
    }
    void* Adopt(const Header& h,TypeId element)
    {
        Validate(h,element);
        if(!h.capacity)return nullptr;
        auto* guest=arena.Allocate(domain,size_t(h.capacity)*8+8);
        try { Check(arena.PromoteArray(guest,element,reinterpret_cast<void*>(h.data),h.capacity,
                                       profile::ArrayAllocator,domain->id())); }
        catch(...) { (void)arena.Retire(guest);throw; }
        return guest;
    }
    Header Read(const Owner& item) const
    {
        Header h{};std::memcpy(&h,item.native,sizeof(h));
        if(h.size>h.capacity || h.capacity!=item.capacity ||
           h.data!=(item.capacity ? reinterpret_cast<uintptr_t>(item.nativeBuffer) : services.nativeAllocatorVtable))
            throw Failure(Error::IdentityMismatch);
        return h;
    }
    void Dispose(Owner& item)
    {
        // Once the owner points elsewhere, failure must retain the old resources.
        // A native observer may not race this control-plane operation.
        const auto error=arena.Retire(item.guestBuffer);
        if(error!=Error::None){poisoned=true;throw Failure(error);}
        services.free(item.nativeBuffer);
    }
    void Free(Allocation& allocation)
    {
        std::unique_lock guard(control,std::try_to_lock);
        if(!guard.owns_lock() || poisoned)throw Failure(Error::Busy);
        for(auto& item:arrays) if(item.guestBuffer==allocation.memory && allocation.memory)
        {
            const auto h=Read(item);
            if(h.size || (allocation.size && allocation.size!=size_t(h.capacity)*8+8))throw Failure(Error::InvalidRange);
            const Header empty{services.nativeAllocatorVtable,0,0};
            std::memcpy(item.native,&empty,sizeof(empty));
            Dispose(item);item.guestBuffer=nullptr;item.nativeBuffer=nullptr;item.capacity=0;
            allocation={};return;
        }
        throw Failure(Error::UnknownPointer);
    }
};
ArrayAdapter::ArrayAdapter(Domain::Ptr domain,ViewArena& arena,SharedPointers pointers,ArrayServices services)
{
    if(!domain || !pointers.lifetime || !pointers.translator.translate || !services.allocate || !services.free ||
       !services.nativeAllocatorVtable)throw Failure(Error::InvalidSchema);
    if(domain->source()!=profile::Core.source || domain->target()!=profile::Core.target)throw Failure(Error::IdentityMismatch);
    auto state=std::make_shared<State>(std::move(domain),arena,std::move(pointers),std::move(services));
    state->allocator.Bind(state->domain,[state](Allocation& a){state->Free(a);},state->services.allocatorHandle);
    const auto error=arena.memory().Add({state->domain->id(),1,profile::Core.Find(profile::AllocatorVtable),
                                        state->allocator.table,state->services.nativeAllocatorVtable,1,true});
    if(error!=Error::None){state->allocator.Retire();throw Failure(error);}
    state->mapped=true;state_=std::move(state);
}
ArrayAdapter::~ArrayAdapter() { (void)Retire(); }
void ArrayAdapter::Bind(void* guestOwner,TypeId ownerType,std::string_view name)
{
    if(!state_)throw Failure(Error::Retired);
    auto& state=*state_;Domain::Scope scope(*state.domain);
    std::unique_lock guard(state.control,std::try_to_lock);
    if(!guard.owns_lock() || state.poisoned)throw Failure(Error::Busy);
    const auto* type=profile::Core.Find(ownerType);const Field* field{};
    if(type) for(const auto& f:type->fields)if(f.name==name)field=&f;
    if(!field || field->empty_pointee!=profile::AllocatorVtable || field->capacity_native!=field->native_offset+8 ||
       (field->pointee!=profile::FunctionPointer && field->pointee!=profile::PropertyPointer))throw Failure(Error::InvalidSchema);
    uintptr_t native{};
    Check(state.arena.memory().Translate(state.domain->id(),ownerType,Direction::ToNative,reinterpret_cast<uintptr_t>(guestOwner),native));
    const auto guest=reinterpret_cast<uintptr_t>(guestOwner)+field->guest_offset;
    for(const auto& item:state.arrays)if(item.guest==guest)throw Failure(Error::Overlap);
    auto* header=reinterpret_cast<Header*>(native+field->native_offset);Header h{};std::memcpy(&h,header,sizeof(h));
    if(state.arrays.size()==64)throw Failure(Error::Capacity);
    // Reserve first: nothing after successful mapping may allocate or throw.
    state.arrays.reserve(state.arrays.size()+1);
    auto* buffer=state.Adopt(h,field->pointee);
    state.arrays.push_back({guest,header,field->pointee,buffer,
                           h.capacity ? reinterpret_cast<void*>(h.data) : nullptr,h.capacity});
    if(!h.capacity) { h.data=state.services.nativeAllocatorVtable;std::memcpy(header,&h,sizeof(h)); }
}
void ArrayAdapter::Synchronize(void* guestArray)
{
    if(!state_)throw Failure(Error::Retired);
    auto& state=*state_;Domain::Scope scope(*state.domain);
    std::unique_lock guard(state.control,std::try_to_lock);
    if(!guard.owns_lock() || state.poisoned)throw Failure(Error::Busy);
    for(auto& item:state.arrays)if(item.guest==reinterpret_cast<uintptr_t>(guestArray))
    {
        Header h{};std::memcpy(&h,item.native,sizeof(h));
        if(h.capacity && h.data==reinterpret_cast<uintptr_t>(item.nativeBuffer))
        {
            if(h.capacity!=item.capacity)throw Failure(Error::IdentityMismatch);
            state.Validate(h,item.element);return;
        }
        auto* buffer=state.Adopt(h,item.element);
        if(item.guestBuffer)
        {
            const auto error=state.arena.Retire(item.guestBuffer);
            if(error!=Error::None)
            {
                if(buffer)(void)state.arena.Retire(buffer);
                state.poisoned=true;throw Failure(error);
            }
        }
        item.guestBuffer=buffer;item.nativeBuffer=h.capacity ? reinterpret_cast<void*>(h.data) : nullptr;
        item.capacity=h.capacity;
        if(!h.capacity) { h.data=state.services.nativeAllocatorVtable;std::memcpy(item.native,&h,sizeof(h)); }
        return;
    }
    throw Failure(Error::UnknownPointer);
}
void ArrayAdapter::Reallocate(void* guestArray,uint32_t capacity,uint32_t elementSize,uint32_t alignment,Mover mover)
{
    if(!state_)throw Failure(Error::Retired);
    auto& state=*state_;Domain::Scope scope(*state.domain);
    std::unique_lock guard(state.control,std::try_to_lock);
    if(!guard.owns_lock() || state.poisoned)throw Failure(Error::Busy);
    if(elementSize!=8 || alignment!=8 || mover || capacity>(UINT32_MAX-8)/8)throw Failure(Error::UnsupportedValue);
    for(auto& item:state.arrays)if(item.guest==reinterpret_cast<uintptr_t>(guestArray))
    {
        const auto old=state.Read(item);
        if(capacity<old.size)throw Failure(Error::InvalidRange);
        if(capacity==old.capacity)return;
        void* native{};void* guest{};
        if(capacity)
        {
            const auto bytes=size_t(capacity)*8+8;
            if(bytes>state.arena.allocation_limit())throw Failure(Error::Capacity);
            native=state.services.allocate(bytes,8);
            if(!native)throw Failure(Error::Capacity);
            try
            {
                if(reinterpret_cast<uintptr_t>(native)%8)throw Failure(Error::InvalidRange);
                std::memset(native,0,bytes);
                if(old.size)std::memcpy(native,item.nativeBuffer,size_t(old.size)*8);
                std::memcpy(static_cast<std::byte*>(native)+size_t(capacity)*8,&state.services.nativeAllocatorVtable,8);
                guest=state.arena.Allocate(state.domain,bytes);
                Check(state.arena.PromoteArray(guest,item.element,native,capacity,profile::ArrayAllocator,state.domain->id()));
            }
            catch(...)
            {
                if(guest)(void)state.arena.Retire(guest);
                state.services.free(native);throw;
            }
        }
        const Header replacement{capacity ? reinterpret_cast<uintptr_t>(native) : state.services.nativeAllocatorVtable,capacity,old.size};
        auto previous=item;
        item.guestBuffer=guest;item.nativeBuffer=native;item.capacity=capacity;
        std::memcpy(item.native,&replacement,sizeof(replacement));
        if(previous.guestBuffer)state.Dispose(previous);
        return;
    }
    throw Failure(Error::UnknownPointer);
}
Error ArrayAdapter::Retire() noexcept
{
    if(!state_)return Error::Retired;
    auto state=state_;
    try
    {
        Domain::Scope cleanup(state->owner);
        std::unique_lock guard(state->control,std::try_to_lock);
        if(!guard.owns_lock() || state->poisoned)return Error::Busy;
        for(const auto& item:state->arrays)if(item.guestBuffer || state->Read(item).size)return Error::Busy;
        if(state->mapped)Check(state->arena.memory().Retire(state->domain->id(),state->allocator.table,1));
        state->mapped=false;state->allocator.Retire();state_.reset();return Error::None;
    }
    catch(...){return Error::UnsupportedValue;}
}
}
#endif
