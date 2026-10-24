#ifdef _WIN32
#include <compat/PropertyAdapter.hpp>
#include <compat/Profile.hpp>

namespace compat
{
struct PropertyAdapter::State
{
    Domain::Ptr domain;
    Domain::Ticket owner;
    SharedPointers pointers;
    ViewArena& arena;
    PropertyServices services;
    void* guest{};
    void* native{};
    std::shared_ptr<State> resident;
    State(Domain::Ptr d,ViewArena& a,SharedPointers p,PropertyServices s)
        : domain(std::move(d)),owner(domain->Retain()),pointers(std::move(p)),arena(a),services(std::move(s)) {}
    ~State()
    {
        Domain::Scope cleanup(owner);
        if(guest)(void)arena.Retire(guest);
        if(native)services.destroy(native);
        services={};pointers.lifetime.reset();
    }
};
PropertyAdapter PropertyAdapter::Create(Domain::Ptr domain,ViewArena& arena,SharedPointers pointers,PropertyServices services)
{
    if(!domain || !pointers.lifetime || !pointers.translator.translate || !services.construct || !services.destroy)
        throw Failure(Error::InvalidSchema);
    if(domain->source()!=profile::Core.source || domain->target()!=profile::Core.target)throw Failure(Error::IdentityMismatch);
    Domain::Scope call(*domain);
    auto state=std::make_shared<State>(std::move(domain),arena,std::move(pointers),std::move(services));
    state->guest=arena.Allocate(state->domain,profile::Core.Find(profile::Property)->guest_size);
    state->native=state->services.construct();
    if(!state->native)throw Failure(Error::Capacity);
    Check(arena.Promote(state->guest,profile::Property,state->native,1,state->domain->id()));
    return PropertyAdapter(std::move(state));
}
PropertyAdapter::~PropertyAdapter() = default;
void* PropertyAdapter::guest() const noexcept { return state_->guest; }
void* PropertyAdapter::native() const noexcept { return state_->native; }
void PropertyAdapter::Publish() noexcept { state_->resident=state_; }
}
#endif
