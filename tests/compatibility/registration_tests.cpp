#define RED4EXT_STATIC_LIB
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <compat/RttiAdapter.hpp>
#include <compat/PropertyAdapter.hpp>
#include <compat/ModulePin.hpp>
#include <compat/FunctionAdapter.hpp>
#include <compat/ArrayAdapter.hpp>
#include <compat/ExceptionBackend.hpp>
#include <compat/Profile.hpp>
#include <RED4ext/Callback.hpp>
#include <RED4ext/Scripting/CProperty.hpp>
#undef RED4EXT_INLINE
#define RED4EXT_INLINE inline
#include <RED4ext/SharedSpinLock-inl.hpp>
#include <RED4ext/Scripting/CProperty-inl.hpp>
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace compat;
static void Require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> static void Reject(F&& fn,Error expected)
{ bool caught=false;try{fn();}catch(const Failure& e){caught=e.code()==expected;}Require(caught,"expected contract rejection"); }
template<class F> static F Export(HMODULE module,const char* name)
{ auto f=reinterpret_cast<F>(GetProcAddress(module,name));Require(f,"fixture export");return f; }
RED4ext::CName RED4ext::CNamePool::Add(const char* text) { return text ? CName(text) : CName(uint64_t{}); }
static uint64_t Hash(const char* text) { return RED4ext::FNV1a64(text); }
static uint32_t Current() { return Domain::Current() ? Domain::Current()->id() : 0; }
static const char* stage="retirement";
struct Counts { int live{},copies{},calls{},badDomain{};uint32_t (*current)()=Current; };
struct Resources
{
    std::shared_ptr<ModulePin> modulePin; // Destroy after every possible guest payload.
    Domain::Ptr domain=Domain::Create(41,std::string(profile::Core.source),std::string(profile::Core.target));
    ViewArena arena{profile::Core,160,65536};
    ExceptionBackend backend{arena};
    alignas(8) std::array<std::byte,0x238> system{};
    alignas(8) std::array<std::byte,0x2D0> cls{};
    alignas(8) std::array<std::byte,16> integer{};
    RED4ext::Memory::Vault vault{};
    RED4ext::Memory::PoolStorage pools[2]{};
    alignas(8) uintptr_t poolContexts[2]{};
    std::array<uintptr_t,7> allocator{};
    std::byte bytecode{};
    std::optional<RED4ext::Callback<void(*)()>> callbacks[2];
    Counts counts;
    bool throwSink{};
    unsigned allocations{},properties{},destroyed{},namePairs{},registeredTypes{};
    ScriptEntry script{};
    Resources()
    {
        auto name=Hash("CompatHost");uint32_t size=64,alignment=8;
        std::memcpy(cls.data()+0x18,&name,8);std::memcpy(cls.data()+0x20,&name,8);
        std::memcpy(cls.data()+0x68,&size,4);std::memcpy(cls.data()+0x74,&alignment,4);
    }
};
static Error Translate(void* context,TypeId type,Direction direction,uintptr_t source,uintptr_t& target)
{ auto& r=*static_cast<Resources*>(context);return r.arena.memory().Translate(r.domain->id(),type,direction,source,target); }
struct GetTag;using GetRoute=CallRouter<void*(),GetTag>;
struct AllocTag;using AllocRoute=CallRouter<void(void*,RED4ext::Memory::AllocationResult*,uint64_t),AllocTag>;
struct NameTag;using NameRoute=CallRouter<void*(void*,const char*),NameTag>;
struct ConstructTag;using ConstructRoute=CallRouter<void*(void*,void*,uint64_t,uint64_t,ScriptEntry,uint32_t),ConstructTag>;
struct ResizeTag;using ResizeRoute=CallRouter<void(void*,uint32_t,uint32_t,uint32_t,ArrayAdapter::Mover),ResizeTag>;
struct ResolveTag;using ResolveRoute=CallRouter<uintptr_t(uint32_t),ResolveTag>;

static RttiServices Services(const std::shared_ptr<Resources>& r)
{
    RttiServices s;
    s.getType=[r](uint64_t name)->RttiObject {
        if(name==Hash("CompatHost"))return {r->cls.data(),profile::Class};
        if(name==Hash("Int32"))return {r->integer.data(),profile::RttiMetadata};
        return {};
    };
    s.getClass=[r](uint64_t name)->RttiObject { return name==Hash("CompatHost") ? RttiObject{r->cls.data(),profile::Class} : RttiObject{}; };
    s.getAsyncType=[r](uint32_t id)->RttiObject { Require(id==17,"async id");return {r->integer.data(),profile::RttiMetadata}; };
    s.getScriptClass=[r](uint64_t name)->RttiObject { Require(name==Hash("ScriptHost"),"script class name");return {r->cls.data(),profile::Class}; };
    s.metadata=[r](void* native)->TypeMetadata {
        if(native==r->integer.data())return {Hash("Int32"),Hash("Int32"),4,4,1};
        Require(native==r->cls.data(),"metadata native identity");return {Hash("CompatHost"),Hash("CompatHost"),64,8,2};
    };
    s.registerScriptName=[r](uint64_t native,uint64_t script) {
        Require(native==Hash("CompatHost") && script==Hash("ScriptHost") && Current()==41,"name registration");++r->namePairs;
    };
    s.convertName=[](uint64_t name,bool toScript) {
        Require(name==Hash(toScript ? "CompatHost" : "ScriptHost"),"name input");return Hash(toScript ? "ScriptHost" : "CompatHost");
    };
    s.addCallback=[r](std::function<void()> invoke,bool post) {
        Require(Current()==41,"callback sink domain");
        r->callbacks[post ? 1 : 0].emplace([invoke=std::move(invoke)]{invoke();});
        if(r->throwSink)throw std::runtime_error("sink published then threw");
    };
    s.registerType=[r](void* type,uint32_t id) {
        Require(type==r->cls.data() && id==0x12345 && Current()==41,"native registration identity/id/domain");
        ++r->registeredTypes;
        if(r->throwSink)throw std::runtime_error("type published then threw");
    };
    return s;
}
static void Retirement()
{
    auto module=LoadLibraryW(L"registration_guest.dll");Require(module,"pin fixture load");
    auto code=GetProcAddress(module,"ConfigureRegistration");Require(code,"pin fixture export");
    auto pin=ModulePin::Acquire(reinterpret_cast<const void*>(code));
    Require(FreeLibrary(module) && GetModuleHandleW(L"registration_guest.dll")==module,"OS reference retains DLL");
    pin.reset();Require(!GetModuleHandleW(L"registration_guest.dll"),"final OS reference releases DLL");
    module=LoadLibraryW(L"registration_guest.dll");Require(module,"concurrent pin fixture load");
    code=GetProcAddress(module,"ConfigureRegistration");
    auto pins=std::make_shared<ModulePins>();
    std::array<std::thread,8> callers;
    for(auto& thread:callers)thread=std::thread([&]{pins->Retain(reinterpret_cast<uintptr_t>(module),reinterpret_cast<uintptr_t>(code));});
    for(auto& thread:callers)thread.join();
    Require(FreeLibrary(module) && GetModuleHandleW(L"registration_guest.dll")==module,"concurrent first resolvers retain module");
    pins.reset();Require(!GetModuleHandleW(L"registration_guest.dll"),"duplicate concurrent references are balanced");
    auto r=std::make_shared<Resources>();auto domain=r->domain;
    RttiAdapter rtti(domain,r->arena,{{r.get(),Translate},r},Services(r),r->system.data());
    auto* guest=rtti.guest();auto* cls=rtti.View({r->cls.data(),profile::Class});
    uintptr_t table{};Check(r->arena.memory().Read(reinterpret_cast<uintptr_t>(guest),std::as_writable_bytes(std::span(&table,1))));
    auto entry=reinterpret_cast<void*(*)(void*,uint64_t)>(*reinterpret_cast<uintptr_t*>(table));
    Require(entry(guest,Hash("CompatHost"))==cls,"canonical class identity");
    Reject([&]{entry(cls,Hash("CompatHost"));},Error::UnknownPointer);
    auto other=Domain::Create(42,std::string(profile::Core.source),std::string(profile::Core.target));
    RttiAdapter second(other,r->arena,{{r.get(),Translate},r},Services(r),r->system.data());
    auto* otherClass=second.View({r->cls.data(),profile::Class});
    Require(otherClass!=cls,"same native class has distinct ABI-domain views");
    Reject([&]{entry(second.guest(),Hash("CompatHost"));},Error::UnknownPointer);
    other->Close();Check(second.Retire());Require(other->CanUnload(),"second domain drained independently");
    auto makeProperty=[r]()->void* {++r->properties;return new RED4ext::CProperty(nullptr,"",nullptr,0,"",{});};
    { auto property=PropertyAdapter::Create(domain,r->arena,{{r.get(),Translate},r},{makeProperty,[r](void* p){++r->destroyed;delete static_cast<RED4ext::CProperty*>(p);}}); }
    Require(r->properties==1 && r->destroyed==1,"unpublished property reclaimed");
    Reject([&]{(void)PropertyAdapter::Create(domain,r->arena,{{r.get(),Translate},r},{[]()->void*{return nullptr;},[](void*){Require(false,"null property destroyed");}});},Error::Capacity);
    for(uintptr_t i=0;;++i)
    {
        auto result=r->arena.memory().Add({99,1,profile::Core.Find(profile::Bytes),0x40000000000ull+i*16,0x50000000000ull+i*16,1,true});
        if(result==Error::Capacity)break;
        Check(result);
    }
    Reject([&]{(void)PropertyAdapter::Create(domain,r->arena,{{r.get(),Translate},r},
        {makeProperty,[r](void* p){++r->destroyed;delete static_cast<RED4ext::CProperty*>(p);}});},Error::Capacity);
    Require(r->properties==2 && r->destroyed==2,"failed property mapping destroys constructed native backing");
    domain->Close();Check(rtti.Retire());
    Reject([&]{entry(guest,Hash("CompatHost"));},Error::Retired);
    Require(domain->CanUnload(),"unpublished RTTI and property ownership drained");
}
static void Registration(const char* mode)
{
    auto r=std::make_shared<Resources>();auto domain=r->domain;
    auto pointers=SharedPointers{{r.get(),Translate},r};
    r->throwSink=std::strcmp(mode,"throw")==0;
    RttiAdapter rtti(domain,r->arena,pointers,Services(r),r->system.data());
    auto* cls=rtti.View({r->cls.data(),profile::Class});
    ArrayServices arrayServices;
    arrayServices.nativeAllocatorVtable=reinterpret_cast<uintptr_t>(r->allocator.data());arrayServices.allocatorHandle=0x1729;
    arrayServices.allocate=[](size_t bytes,size_t)->void*{return std::calloc(1,bytes);};
    arrayServices.free=[](void* pointer){std::free(pointer);};
    ArrayAdapter arrays(domain,r->arena,pointers,arrayServices);
    arrays.Bind(cls,profile::Class,"propsData");arrays.Bind(cls,profile::Class,"staticFuncsData");
    for(size_t i=0;i<2;++i)
    {
        r->pools[i].allocatorStorage=reinterpret_cast<uintptr_t>(&r->poolContexts[i]);
        auto& node=r->vault.poolRegistry.nodes[i];node.storage=&r->pools[i];
        node.handle=RED4ext::FNV1a32(i ? "PoolRTTIFunction" : "PoolRTTIProperty");
    }
    auto get=GetRoute::Bind(domain,[&]{return rtti.guest();});
    auto allocate=AllocRoute::Bind(domain,[r,pointers](void* context,RED4ext::Memory::AllocationResult* out,uint64_t bytes){
        Require(out,"allocation out");++r->allocations;
        if(context==&r->poolContexts[0])
        {
            Require(bytes==48,"property pool extent");
            auto property=PropertyAdapter::Create(r->domain,r->arena,pointers,{
                [r]()->void*{++r->properties;return new RED4ext::CProperty(nullptr,"",nullptr,0,"",{});},
                [r](void* p){++r->destroyed;delete static_cast<RED4ext::CProperty*>(p);}});
            property.Publish();*out={property.guest(),size_t(bytes)};
        }
        else
        {
            Require(context==&r->poolContexts[1] && bytes==0xC0,"function pool context and extent");
            *out={r->arena.Allocate(r->domain,size_t(bytes)),size_t(bytes)};
        }
    });
    auto names=NameRoute::Bind(domain,[](void* out,const char* text)->void*{auto hash=text ? Hash(text) : 0;std::memcpy(out,&hash,8);return out;});
    auto construct=ConstructRoute::Bind(domain,[r,pointers](void* guest,void* parent,uint64_t full,uint64_t shortName,ScriptEntry script,uint32_t flags)->void*{
        FunctionServices services;
        services.allocate=[](size_t bytes,size_t)->void*{return std::calloc(1,bytes);};
        services.free_unconstructed=[](void* p){std::free(p);};
        services.construct=[r](void* p,void* parent,uint64_t full,uint64_t shortName,ScriptEntry entry,uint32_t flags)->void* {
            auto* bytes=static_cast<std::byte*>(p);std::memcpy(bytes+8,&full,8);std::memcpy(bytes+16,&shortName,8);
            flags|=3; // Fake class-static constructor supplies native/static flags.
            std::memcpy(bytes+0xF0,&parent,8);std::memcpy(bytes+0xA8,&flags,4);r->script=entry;return p;
        };
        auto function=FunctionAdapter::Create(r->domain,r->arena,pointers,services,guest,parent,full,shortName,script,flags);
        Require(function.Retire()==Error::Published,"function publication retains backing");return function.guest();
    });
    auto resize=ResizeRoute::Bind(domain,[&](void* a,uint32_t n,uint32_t size,uint32_t alignment,ArrayAdapter::Mover move){arrays.Reallocate(a,n,size,alignment,move);});
    auto resolver=ResolveRoute::Bind(domain,[&](uint32_t hash)->uintptr_t{
        switch(hash)
        {
        case 0x4A610F64:return reinterpret_cast<uintptr_t>(get.entry());
        case 0x1B530DEC:return reinterpret_cast<uintptr_t>(&r->vault);
        case 2418676960:return reinterpret_cast<uintptr_t>(allocate.entry());
        case 0xA00C9B:return reinterpret_cast<uintptr_t>(names.entry());
        case 2920426135:return reinterpret_cast<uintptr_t>(construct.entry());
        case 0x7AA013D2:return reinterpret_cast<uintptr_t>(resize.entry());
        default:throw Failure(Error::UnknownOperation);
        }
    });
    auto module=LoadLibraryW(std::strcmp(mode,"unoptimized")==0 ? L"registration_guest_unoptimized.dll" : L"registration_guest.dll");
    Require(module,"load registration guest");
    auto configure=Export<void(*)(ResolveRoute::Entry,Counts*)>(module,"ConfigureRegistration");
    r->modulePin=ModulePin::Acquire(reinterpret_cast<const void*>(configure));
    Require(FreeLibrary(module),"release loader reference while callback owner pins code");
    configure(resolver.entry(),&r->counts);
    const auto registerType=Export<void(*)(void*,uint32_t)>(module,"RegisterMappedType");
    Reject([&]{registerType(nullptr,0x12345);},Error::UnknownPointer);
    Reject([&]{registerType(r->cls.data(),0x12345);},Error::UnknownPointer);
    Require(!r->registeredTypes,"unknown type identity never reaches native registry");
    bool typeThrew=false;
    try {registerType(cls,0x12345);} catch(const std::runtime_error&) {typeThrew=true;}
    Require(typeThrew==r->throwSink && r->registeredTypes==1 && rtti.Retire()==Error::Published,
            "type publication retains domain even when the registry throws");
    stage="metadata";Require(Export<bool(*)()>(module,"CheckMetadata")(),"old SDK virtual metadata and CName return ABI");
    Reject([&]{Export<void(*)()>(module,"UnknownVirtual")();},Error::UnknownOperation);
    const auto install=Export<void(*)(bool)>(module,"InstallRegistration");
    for(bool post:{false,true})
    {
        stage="callback installation";
        Domain::Scope scope(*domain);
        bool caught=false;try{install(post);}catch(const std::runtime_error&){caught=true;}
        Require(caught==r->throwSink,"sink exception propagated");
    }
    Require(!Current() && r->counts.live==2 && !r->counts.badDomain,"by-value cleanup stayed in domain");
    stage="callback invocation";for(auto& callback:r->callbacks)(*callback)();
    Require(r->counts.calls==2 && r->properties==2 && r->allocations==4 && !r->counts.badDomain,"nested old SDK registration chain");
    stage="registration lookup";Require(Export<bool(*)()>(module,"CheckRegistration")(),"live property and function publication");
    stage="script invocation";alignas(16) std::array<std::byte,0xA8> frame{};int result{};
    const auto code=reinterpret_cast<uintptr_t>(&r->bytecode);
    Check(r->arena.memory().Add({domain->id(),1,profile::Core.Find(profile::Bytecode),code,code,1,true}));
    std::memcpy(frame.data(),&code,8);r->script(nullptr,frame.data(),&result,0);
    Require(result==73 && !Current(),"published script callback returns through old frame");
    auto voidScript=BindScript(domain,Export<ScriptEntry>(module,"VoidScript"),pointers,ScalarResult::Void());
    voidScript.entry()(nullptr,frame.data(),nullptr,0);
    Require(frame[0x62]==std::byte{1} && !Current(),"actual old-header void script synchronizes frame without an output buffer");
    Reject([&]{voidScript.entry()(nullptr,frame.data(),&result,0);},Error::InvalidRange);
    bool voidThrew=false;
    try {voidScript.entry()(nullptr,frame.data(),nullptr,-1);}catch(int code){voidThrew=code==82;}
    Require(voidThrew && frame[0x62]==std::byte{2} && !Current(),"void script exception preserves frame copy-out and domain cleanup");
    Check(voidScript.Retire());
    const OpcodeTable::Operation voidOperation{17,[](void*,void* native,void* output,void* auxiliary) {
        Require(!output && !auxiliary,"void opcode has no result/auxiliary buffers");
        ++*reinterpret_cast<uint8_t*>(static_cast<std::byte*>(native)+0x62);
    },ScalarResult::Void()};
    OpcodeTable voidOpcodes(domain,pointers,{&voidOperation,1});
    {
        const ValueMarshaller framePlan(profile::Core,profile::StackFrame);
        BorrowedValue binding(domain->id(),framePlan,frame,pointers.translator);
        binding.CallGuest([&](void* guest) {voidOpcodes.data()[17](nullptr,guest,nullptr,nullptr);});
        Require(frame[0x62]==std::byte{3},"void native opcode synchronizes frame without an output buffer");
        Reject([&]{voidOpcodes.data()[17](nullptr,binding.guest(),&result,nullptr);},Error::InvalidRange);
    }
    Check(voidOpcodes.Retire());
    Require(rtti.Retire()==Error::Published,"published RTTI stays resident after handle retirement");
    domain->Close();Reject([&]{(*r->callbacks[0])();},Error::Closed);
    for(auto& callback:r->callbacks)callback.reset();
    Require(!r->counts.live && !r->counts.badDomain && !domain->CanUnload(),"callback cleanup after close, published native objects pin domain");
    // The ordinary loader reference was released before registration; the
    // pinned shared lifetime now owns the code used by all retained handlers.
    std::puts("PASS historical registration: virtual RTTI, metadata, callback conversion, typed pools, inline properties, arrays, functions, close cleanup");
}
int main(int argc,char** argv)
{
    try{Retirement();Registration(argc>1 ? argv[1] : "normal");}
    catch(const Failure& e){std::fprintf(stderr,"FAIL registration contract %u during %s\n",unsigned(e.code()),stage);return 1;}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
