#ifdef _WIN32
#include <compat/FunctionAdapter.hpp>
#include <compat/Profile.hpp>
#include <optional>

namespace compat
{
struct FunctionAdapter::State
{
    Domain::Ptr domain;
    Domain::Ticket owner;
    SharedPointers pointers;
    ViewArena& arena;
    FunctionServices services;
    void* guest{};
    void* native{};
    std::optional<ScriptRoute::Registration> script;
    bool entered{}, permanent{}, constructed{};
    State(Domain::Ptr d, SharedPointers p, ViewArena& a, FunctionServices s)
        : domain(std::move(d)),owner(domain->Retain()),pointers(std::move(p)),arena(a),services(std::move(s)) {}
    ~State() noexcept
    {
        Domain::Scope cleanup(owner);
        // Normal cleanup has already retired the route/views and freed native
        // memory. Before publication, failed preparation may still own storage.
        if(!entered)
        {
            if(guest) (void)arena.Retire(guest);
            if(native) services.free_unconstructed(native);
        }
        services={}; pointers.lifetime.reset();
    }
    void Pin() noexcept
    {
        permanent=true;
        // The compiled entry captures this state; publishing retains all backing
        // and the translator/arena lifetime even if the public handle is lost.
        if(script)script->Publish();
    }
};
FunctionAdapter FunctionAdapter::Create(Domain::Ptr domain, ViewArena& arena, SharedPointers pointers,
    FunctionServices services, void* guest, void* parent, uint64_t full, uint64_t shortName,
    ScriptEntry legacy, uint32_t flags, ScalarResult result)
{
    if(!domain || !pointers.lifetime || !pointers.translator.translate || !legacy || !parent || !result.Valid() ||
       !services.allocate || !services.free_unconstructed || !services.construct || bool(services.detach)!=bool(services.destroy))
        throw Failure(Error::InvalidSchema);
    if(domain->source()!=profile::Core.source || domain->target()!=profile::Core.target) throw Failure(Error::IdentityMismatch);
    const auto& type=*profile::Core.Find(profile::Function);
    Check(arena.ValidateAllocation(guest,domain->id(),type.guest_size));
    uintptr_t nativeParent{};
    Check(pointers.translator.translate(pointers.translator.context,profile::Class,Direction::ToNative,
                                       reinterpret_cast<uintptr_t>(parent),nativeParent));
    if(!nativeParent)throw Failure(Error::UnknownPointer);
    auto state=std::make_shared<State>(domain,std::move(pointers),arena,std::move(services));
    state->guest=guest;
    state->native=state->services.allocate(type.native_size,type.alignment);
    if(!state->native)throw Failure(Error::Capacity);
    try
    {
        state->script.emplace(BindScript(domain,legacy,{state->pointers.translator,state},result));
        // Reserve mappings, protect the original address and own every resource
        // before the native constructor can publish a callback slot.
        Check(arena.Promote(guest,profile::Function,state->native,1,domain->id()));
    }
    catch(...)
    {
        if(state->script) (void)state->script->Retire(); // Break provisional self-retention.
        throw;
    }
    state->entered=true;
    if(!state->services.detach)state->Pin();
    try
    {
        auto* resultPointer=state->services.construct(state->native,reinterpret_cast<void*>(nativeParent),full,shortName,
                                                     state->script->entry(),flags);
        if(resultPointer!=state->native)throw Failure(Error::IdentityMismatch);
        state->constructed=true;
    }
    catch(...)
    {
        // The callback may already be published even when construction throws.
        // No guessed destructor/rollback on a partially constructed engine object.
        state->Pin();throw;
    }
    return FunctionAdapter(std::move(state));
}
FunctionAdapter::~FunctionAdapter() { (void)Retire(); }
void* FunctionAdapter::guest() const noexcept { return state_ ? state_->guest : nullptr; }
void* FunctionAdapter::native() const noexcept { return state_ ? state_->native : nullptr; }
Error FunctionAdapter::Retire() noexcept
{
    if(!state_)return Error::Retired;
    auto& state=*state_;
    if(state.permanent)return Error::Published;
    try
    {
        Domain::Scope cleanup(state.owner);
        if(!state.constructed || !state.services.detach || !state.services.detach(state.native,state.script->entry()))
        { state.Pin();return Error::Published; }
        // Detach guarantees callers have drained before route/view/backing removal.
        Check(state.arena.Retire(state.guest));
        state.services.destroy(state.native);
        Check(state.script->Retire());
        state.native=nullptr;state.guest=nullptr;
    }
    catch(...)
    {
        state.Pin();return Error::UnsupportedValue;
    }
    state_.reset();return Error::None;
}
}
#endif
