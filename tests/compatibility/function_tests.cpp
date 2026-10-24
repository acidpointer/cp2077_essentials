#include <compat/ExceptionBackend.hpp>
#include <compat/FunctionAdapter.hpp>
#include <compat/Profile.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

using namespace compat;
static void Require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
struct Resources
{
    Domain::Ptr domain=Domain::Create(8,std::string(profile::Core.source),std::string(profile::Core.target));
    alignas(16) std::array<std::byte,0x2D0> cls{};
    alignas(16) std::array<std::byte,0x100> function{};
    ViewArena arena{profile::Core,8};
    ExceptionBackend backend{arena};
    void* guestClass=arena.Borrow(domain,profile::Class,cls.data());
    ScriptEntry callback{};
    unsigned allocated{},freed{},constructed{},destroyed{};
    bool expectResident{};
    ~Resources() { if(expectResident)std::abort(); }
};
static Error Translate(void* context,TypeId type,Direction direction,uintptr_t value,uintptr_t& target)
{
    auto& state=*static_cast<Resources*>(context);
    return state.arena.memory().Translate(state.domain->id(),type,direction,value,target);
}
static void Legacy(void*,void*,void*,int64_t) {}
static FunctionServices Services(Resources& state)
{
    FunctionServices services;
    services.allocate=[&](size_t size,size_t alignment) -> void* {
        Require(size==0x100 && alignment==8,"allocation contract");++state.allocated;return state.function.data();
    };
    services.free_unconstructed=[&](void* memory) { Require(memory==state.function.data(),"free contract");++state.freed; };
    services.construct=[&](void* memory,void* parent,uint64_t,uint64_t,ScriptEntry entry,uint32_t) -> void* {
        Require(memory==state.function.data() && parent==state.cls.data(),"native arguments");
        uintptr_t guest{};
        Check(state.arena.memory().Translate(state.domain->id(),profile::Function,Direction::ToGuest,reinterpret_cast<uintptr_t>(memory),guest));
        Require(guest!=0,"promoted before engine publication");++state.constructed;state.callback=entry;return memory;
    };
    services.detach=[&](void* memory,ScriptEntry entry) {
        Require(memory==state.function.data() && entry==state.callback,"detach identity");state.callback=nullptr;return true;
    };
    services.destroy=[&](void* memory) { Require(memory==state.function.data() && !state.callback,"destroy after detach");++state.destroyed; };
    return services;
}
static void PreparationAndDetach()
{
    auto state=std::make_shared<Resources>();auto domain=state->domain;
    SharedPointers pointers{{state.get(),Translate},state};
    auto* guest=state->arena.Allocate(domain,0xC0);
    auto services=Services(*state);
    auto foreign=Domain::Create(9,std::string(profile::Core.source),std::string(profile::Core.target));
    bool rejected=false;
    try { (void)FunctionAdapter::Create(foreign,state->arena,pointers,services,guest,state->guestClass,1,2,Legacy,0); }
    catch(const Failure& error) { rejected=error.code()==Error::IdentityMismatch; }
    Require(rejected && !state->allocated && !state->constructed,"cross-domain object rejected before allocation");
    // Force registry exhaustion while leaving the existing provisional allocation
    // intact. No native publication may occur on this preparation failure.
    const auto* bytes=profile::Core.Find(profile::Bytes);
    for(uintptr_t i=0;i<7;++i) Check(state->arena.memory().Add({domain->id(),1,bytes,0x100000+i*4096,0x200000+i*4096,1,true}));
    rejected=false;
    try { (void)FunctionAdapter::Create(domain,state->arena,pointers,services,guest,state->guestClass,1,2,Legacy,0); }
    catch(const Failure& error) { rejected=error.code()==Error::Capacity; }
    Require(rejected && state->allocated==1 && state->freed==1 && !state->constructed,"prepublication failure frees provisional resources");
    Require(state->arena.ValidateAllocation(guest,domain->id(),0xC0)==Error::Retired,"failed allocation quarantined");
    Check(state->arena.Retire(state->guestClass));domain->Close();Require(domain->CanUnload(),"preparation owners released");
    pointers.lifetime.reset();state.reset(); // Remove backend before a new environment.

    state=std::make_shared<Resources>();domain=state->domain;pointers={{state.get(),Translate},state};services=Services(*state);
    guest=state->arena.Allocate(domain,0xC0);
    auto function=FunctionAdapter::Create(domain,state->arena,pointers,services,guest,state->guestClass,1,2,Legacy,0);
    Require(function.guest()==guest && function.native()==state->function.data(),"original pointer preserved");
    domain->Close();Require(!domain->CanUnload(),"retained native function blocks unload");
    Check(function.Retire());Require(state->destroyed==1 && state->freed==0 && !state->callback,"detach destroys exactly once");
    Require(function.Retire()==Error::Retired,"repeated retirement");
    Check(state->arena.Retire(state->guestClass));Require(domain->CanUnload(),"detached function and views release owners");
    std::puts("PASS function preparation, cross-domain rejection, rollback before publication, explicit detach");
}
static void Publication(std::string_view mode)
{
    auto state=std::make_shared<Resources>();auto domain=state->domain;
    std::weak_ptr<Resources> retained=state;
    auto services=Services(*state);
    auto construct=services.construct;
    if(mode=="throw")services.construct=[construct](void* object,void* parent,uint64_t full,uint64_t name,ScriptEntry script,uint32_t flags) -> void* {
        construct(object,parent,full,name,script,flags);throw std::runtime_error("engine published then threw");
    };
    if(mode=="persistent") { services.detach={};services.destroy={}; }
    if(mode=="detach-failure")services.detach=[](void*,ScriptEntry) { return false; };
    auto* guest=state->arena.Allocate(domain,0xC0);
    bool thrown=false;
    try
    {
        auto function=FunctionAdapter::Create(domain,state->arena,{{state.get(),Translate},state},services,guest,state->guestClass,1,2,Legacy,0);
        Require(function.Retire()==Error::Published,"no unreviewed detach");
    }
    catch(const std::runtime_error&) { thrown=true; }
    Require(thrown==(mode=="throw") && state->constructed==1 && state->callback && !state->destroyed && !state->freed,"published state retained on every exit path");
    domain->Close();Require(!domain->CanUnload(),"published entries prevent unload");
    state->expectResident=true;
    state.reset();Require(!retained.expired(),"lost public handle does not free engine-reachable backing");
    std::printf("PASS publication retention: %.*s; no shutdown-time guest cleanup\n",int(mode.size()),mode.data());
}
int main(int argc,char** argv)
{
    try { if(argc>1)Publication(argv[1]);else PreparationAndDetach(); }
    catch(const Failure& error) { std::fprintf(stderr,"FAIL contract %u\n",unsigned(error.code()));return 1; }
    catch(const std::exception& error) { std::fprintf(stderr,"FAIL %s\n",error.what());return 1; }
}
