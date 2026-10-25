#include <compat/ArrayAdapter.hpp>
#include <compat/AddressCatalog.hpp>
#include <compat/ExceptionBackend.hpp>
#include <compat/Profile.hpp>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

using namespace compat;
static void Require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> static void Reject(F&& fn,Error error)
{
    bool caught=false;try {fn();}catch(const Failure& value){caught=value.code()==error;}
    Require(caught,"expected rejection");
}
struct Header { uintptr_t data;uint32_t capacity,size; };
struct Resources
{
    Domain::Ptr domain=Domain::Create(31,std::string(profile::Core.source),std::string(profile::Core.target));
    ViewArena arena{profile::Core,160,65536};
    ExceptionBackend backend{arena};
    alignas(8) std::array<std::byte,0x2D0> cls{};
    alignas(8) std::array<std::byte,0x100> function{};
    std::array<uintptr_t,7> nativeAllocator{};
    std::set<void*> allocations;
    bool failAllocation{};
    size_t allocs{},frees{};
    Header header() const { Header h;std::memcpy(&h,cls.data()+0x58,sizeof(h));return h; }
};
static Error Translate(void* context,TypeId type,Direction direction,uintptr_t source,uintptr_t& target)
{
    auto& r=*static_cast<Resources*>(context);
    return r.arena.memory().Translate(r.domain->id(),type,direction,source,target);
}
struct ResizeTag;
using Resize=CallRouter<void(void*,uint32_t,uint32_t,uint32_t,ArrayAdapter::Mover),ResizeTag>;
struct ResolveTag;
using Resolve=CallRouter<uintptr_t(uint32_t),ResolveTag>;
template<class F> static F Export(HMODULE module,const char* name)
{auto entry=reinterpret_cast<F>(GetProcAddress(module,name));Require(entry,"fixture export");return entry;}

static void Run(const wchar_t* path)
{
    auto r=std::make_shared<Resources>();auto& arena=r->arena;auto domain=r->domain;
    auto* cls=arena.Borrow(domain,profile::Class,r->cls.data());
    auto* function=arena.Borrow(domain,profile::Function,r->function.data());
    ArrayServices services;
    services.nativeAllocatorVtable=reinterpret_cast<uintptr_t>(r->nativeAllocator.data());services.allocatorHandle=0x1729;
    services.allocate=[r](size_t bytes,size_t alignment)->void* {
        Require(alignment==8,"pointer alignment");if(r->failAllocation)return nullptr;
        auto* p=std::calloc(1,bytes);if(p){Require(r->allocations.insert(p).second,"duplicate native allocation");++r->allocs;}return p;
    };
    services.free=[r](void* p){Require(r->allocations.erase(p)==1,"double/foreign free");++r->frees;std::free(p);};
    ArrayAdapter adapter(domain,arena,{{r.get(),Translate},r},services);
    adapter.Bind(cls,profile::Class,"staticFuncsData");
    auto* array=static_cast<std::byte*>(cls)+0x58;
    auto resize=Resize::Bind(domain,[&](void* a,uint32_t n,uint32_t size,uint32_t alignment,ArrayAdapter::Mover move) {
        adapter.Reallocate(a,n,size,alignment,move);
    });
    const auto& operation=profile::Operations.back();
    Require(operation.id=="DynArray_Realloc" && operation.primary==0x7AA013D2,"reviewed relocation");
    // Resolve only this fixture's operation. Its native target is a compiled
    // adapter, never the raw game realloc implementation.
    auto resolver=Resolve::Bind(domain,[&](uint32_t hash)->uintptr_t {
        if(hash!=operation.primary)throw Failure(Error::UnknownOperation);
        return reinterpret_cast<uintptr_t>(resize.entry());
    });
    auto module=LoadLibraryW(path);Require(module,"load old array fixture");
    Export<void(*)(Resolve::Entry,void*)>(module,"ConfigureArray")(resolver.entry(),array);
    const auto append=Export<void(*)(void*)>(module,"Append");
    const auto grow=Export<void(*)(uint32_t)>(module,"Grow");
    const auto trim=Export<void(*)(uint32_t)>(module,"Trim");
    const auto data=Export<void*(*)()>(module,"Data");
    const auto element=Export<void*(*)(uint32_t)>(module,"Element");
    const auto handle=Export<uint32_t(*)()>(module,"AllocatorHandle");
    Require(handle()==0x1729,"empty allocator union and virtual dispatch");
    Reject([&]{Export<void(*)()>(module,"RejectAllocation")();},Error::UnknownOperation);
    Reject([&]{Export<void(*)()>(module,"UnknownArray")();},Error::UnknownPointer);
    Reject([&]{adapter.Reallocate(array,4,8,8,[](void*,void*,uint32_t,void*){});},Error::UnsupportedValue);
    Reject([&]{grow(1024*1024);},Error::Capacity);
    Require(!r->allocs,"unreviewed arrays/movers rejected before allocation");
    append(function);auto* old=data();
    Require(r->header().size==1 && *reinterpret_cast<uintptr_t*>(r->header().data)==reinterpret_cast<uintptr_t>(r->function.data()),"inline pointer translation");
    Require(adapter.Retire()==Error::Busy,"live arrays retain ownership");
    r->failAllocation=true;const auto before=r->header();
    Reject([&]{grow(64);},Error::Capacity);
    Require(std::memcmp(&before,r->cls.data()+0x58,sizeof(before))==0 && data()==old && element(0)==function,"failed growth preserves original");
    r->failAllocation=false;
    grow(1024);Require(r->header().capacity==1024 && element(0)==function && handle()==0x1729,"multi-page growth and tail allocator");
    std::array<std::byte,8> scratch{};
    Require(arena.memory().Read(reinterpret_cast<uintptr_t>(old),scratch)==Error::Retired,"old alias quarantined");
    for(unsigned i=1;i<1025;++i)append(function);
    Require(r->header().size==1025 && element(1024)==function,"repeated inline growth");
    old=data();trim(2);Require(r->header().capacity==2 && r->header().size==2 && element(1)==function,"shrink preserves live elements");
    Require(arena.memory().Read(reinterpret_cast<uintptr_t>(old),scratch)==Error::Retired,"shrink alias quarantined");
    trim(0);Require(!r->header().capacity && handle()==0x1729,"empty allocator identity restored");
    append(function);old=data();
    Export<void(*)()>(module,"CacheAllocator")();
    // Exhaust the mapping budget after a valid array exists. Failure to publish
    // both new mappings must free the provisional allocation and preserve it.
    for(uintptr_t i=0;;++i)
    {
        const auto result=arena.memory().Add({99,1,profile::Core.Find(profile::Bytes),0x40000000000ull+i*16,0x50000000000ull+i*16,1,true});
        if(result==Error::Capacity)break;
        Check(result);
    }
    const auto prior=r->header();const auto allocations=r->allocs,freed=r->frees;
    Reject([&]{grow(64);},Error::Capacity);
    Require(r->allocs==allocations+1 && r->frees==freed+1 && data()==old && element(0)==function &&
            std::memcmp(&prior,r->cls.data()+0x58,sizeof(prior))==0,"mapping failure rolls back provisional allocation");
    domain->Close();Require(!domain->CanUnload(),"array owners prevent unload");
    Export<void(*)()>(module,"DestroyArray")(); // Cleanup allocator works after admission closes.
    Require(!r->header().size && !r->header().capacity && r->allocations.empty() && r->allocs==r->frees,"old destructor frees exactly once");
    Require(arena.memory().Read(reinterpret_cast<uintptr_t>(old),scratch)==Error::Retired,"destructor alias quarantined");
    Check(adapter.Retire());Check(resize.Retire());Check(resolver.Retire());
    Reject([&]{Export<uint32_t(*)()>(module,"CachedHandle")();},Error::Retired);
    Check(arena.Retire(cls));Check(arena.Retire(function));Require(domain->CanUnload(),"array ownership drained");
    FreeLibrary(module);
    std::printf("PASS old SDK arrays: multi-page growth, rollback, shrink, allocator union/virtual ABI, destructor, %zu allocations/frees\n",r->allocs);
}
static void RunAdoption(const wchar_t* path)
{
    auto r=std::make_shared<Resources>();auto domain=r->domain;
    auto* cls=r->arena.Borrow(domain,profile::Class,r->cls.data());
    auto* array=static_cast<std::byte*>(cls)+0x58;
    ArrayServices services;
    services.nativeAllocatorVtable=reinterpret_cast<uintptr_t>(r->nativeAllocator.data());
    services.allocatorHandle=0x1729;
    services.allocate=[r](size_t bytes,size_t)->void* {
        auto* p=std::calloc(1,bytes);if(p){r->allocations.insert(p);++r->allocs;}return p;
    };
    services.free=[r](void* p){Require(r->allocations.erase(p)==1,"adoption foreign/double free");++r->frees;std::free(p);};
    unsigned preparations{};void* function{};
    services.prepareElement=[&](TypeId type,void* native) {
        Require(type==profile::Function && native==r->function.data(),"typed native element preparation");
        function=r->arena.Borrow(domain,type,native);++preparations;
    };
    auto makeNative=[&](uint32_t capacity,uint32_t size) {
        auto* buffer=static_cast<uintptr_t*>(services.allocate(size_t(capacity)*8+8,8));
        for(uint32_t i=0;i<size;++i)buffer[i]=reinterpret_cast<uintptr_t>(r->function.data());
        buffer[capacity]=services.nativeAllocatorVtable;
        return Header{reinterpret_cast<uintptr_t>(buffer),capacity,size};
    };
    auto h=makeNative(3,2);std::memcpy(r->cls.data()+0x58,&h,sizeof(h));
    ArrayAdapter adapter(domain,r->arena,{{r.get(),Translate},r},services);
    // Bad allocator identity is rejected without taking ownership or changing
    // the native array. The native caller remains responsible for its buffer.
    reinterpret_cast<uintptr_t*>(h.data)[h.capacity]^=8;
    Reject([&]{adapter.Bind(cls,profile::Class,"staticFuncsData");},Error::IdentityMismatch);
    Require(r->frees==0 && std::memcmp(r->cls.data()+0x58,&h,sizeof(h))==0,"failed adoption preserves owner");
    reinterpret_cast<uintptr_t*>(h.data)[h.capacity]=services.nativeAllocatorVtable;
    adapter.Bind(cls,profile::Class,"staticFuncsData");
    Require(preparations==1 && r->allocs==1,"adopt without copying native backing");
    auto resize=Resize::Bind(domain,[&](void* a,uint32_t n,uint32_t size,uint32_t alignment,ArrayAdapter::Mover move) {
        adapter.Reallocate(a,n,size,alignment,move);
    });
    auto resolver=Resolve::Bind(domain,[&](uint32_t hash)->uintptr_t {
        if(hash!=0x7AA013D2)throw Failure(Error::UnknownOperation);
        return reinterpret_cast<uintptr_t>(resize.entry());
    });
    auto module=LoadLibraryW(path);Require(module,"adoption guest load");
    Export<void(*)(Resolve::Entry,void*)>(module,"ConfigureArray")(resolver.entry(),array);
    const auto element=Export<void*(*)(uint32_t)>(module,"Element");
    const auto data=Export<void*(*)()>(module,"Data");
    Require(element(0)==function && element(1)==function,"native populated elements are canonical guest views");
    auto* oldGuest=data();
    auto replacement=makeNative(7,3);
    std::memcpy(r->cls.data()+0x58,&replacement,sizeof(replacement));
    services.free(reinterpret_cast<void*>(h.data)); // Actual native-side growth owns disposal.
    adapter.Synchronize(array);
    std::array<std::byte,8> scratch{};
    Require(r->arena.memory().Read(reinterpret_cast<uintptr_t>(oldGuest),scratch)==Error::Retired,"native growth retires old guest alias");
    Require(r->frees==1 && element(2)==function && data()!=oldGuest,"native growth is adopted without a second free");
    // A native mutation inside capacity is observed directly by retained views.
    reinterpret_cast<uintptr_t*>(replacement.data)[1]=0;
    adapter.Synchronize(array);
    Require(!element(1),"native element update remains coherent");
    Export<void(*)(uint32_t)>(module,"Grow")(12);
    Require(r->frees==2 && element(0)==function && !element(1),"guest growth after adoption uses allocator ownership");
    oldGuest=data();h=r->header();
    const Header empty{services.nativeAllocatorVtable,0,0};
    std::memcpy(r->cls.data()+0x58,&empty,sizeof(empty));services.free(reinterpret_cast<void*>(h.data));
    adapter.Synchronize(array);
    Require(r->arena.memory().Read(reinterpret_cast<uintptr_t>(oldGuest),scratch)==Error::Retired,"native clear retires aliases");
    Require(Export<uint32_t(*)()>(module,"AllocatorHandle")()==0x1729,"native clear restores guest allocator");
    Check(adapter.Retire());Check(resize.Retire());Check(resolver.Retire());
    Check(r->arena.Retire(function));Check(r->arena.Retire(cls));domain->Close();
    Require(r->allocs==r->frees && r->allocations.empty() && domain->CanUnload(),"adopted ownership drains");
    FreeLibrary(module);
    std::puts("PASS populated native arrays: adoption, typed elements, native growth/clear, retained aliases, guest growth, allocator identity");
}
int main(int argc,char**)
{
    try {const auto* path=argc>1 ? L"array_guest_unoptimized.dll" : L"array_guest.dll";RunAdoption(path);Run(path);}
    catch(const Failure& e){std::fprintf(stderr,"FAIL array contract %u\n",unsigned(e.code()));return 1;}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
