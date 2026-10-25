// Isolated startup-boundary probe for an unchanged plugin. The host contains
// no game executable/code, installs no hooks, and stops at script registration.
// Unknown engine operations terminate with a diagnostic, never a raw address.
#define RED4EXT_STATIC_LIB
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <compat/RttiAdapter.hpp>
#include <compat/NativeRtti.hpp>
#include <compat/ModulePin.hpp>
#include <compat/ExceptionBackend.hpp>
#include <compat/EarlyModules.hpp>
#include <compat/ResolverRouter.hpp>
#include <compat/Profile.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>
#include <RED4ext/Api/v1/EMainReason.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Memory/SharedPtr.hpp>
#include <windows.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <filesystem>
#include <mutex>

using namespace compat;
using Callback=RED4ext::Callback<void(*)()>;
struct Probe
{
    std::shared_ptr<ModulePins> pins=std::make_shared<ModulePins>();
    Domain::Ptr domain=Domain::Create(61,std::string(profile::Core.source),std::string(profile::Core.target));
    ViewArena arena{profile::Core,32};
    ExceptionBackend backend{arena};
    alignas(8) std::array<std::byte,0x238> system{};
    std::array<uintptr_t,44> nativeTable{};
    std::optional<Callback> callbacks[2];
    std::unique_ptr<RttiAdapter> rtti;
    unsigned registrations[2]{};
    bool negative{},copying{};
    std::byte* unresolved{};
    std::array<uint32_t,64> hashes{};
    std::atomic<unsigned> hashCount{};
    size_t page{};
    std::mutex resolveMutex;
    EarlyModules modules{256};
    std::unique_ptr<ResolverRouter> resolver;
    std::unique_ptr<ModuleNotifications> notifications;
    alignas(8) volatile int64_t serialCounter{};
    struct Resident { void* instance{}; RED4ext::RefCnt refs; };
    std::array<Resident,16> residents;
};
static std::shared_ptr<Probe> probe;
[[noreturn]] static void Finish(unsigned code)
{
    std::fflush(stdout);std::fflush(stderr);
    // The checkpoint deliberately excludes plugin process-detach/engine
    // teardown. ExitProcess would invoke those unmodelled DLL destructors.
    TerminateProcess(GetCurrentProcess(),code);std::abort();
}
[[noreturn]] static void Unknown()
{ std::puts("UNSUPPORTED startup operation outside the probe contract");Finish(78); }
static Error Translate(void* context,TypeId type,Direction direction,uintptr_t source,uintptr_t& target)
{ auto& p=*static_cast<Probe*>(context);return p.arena.memory().Translate(p.domain->id(),type,direction,source,target); }
static void Log(RED4ext::v1::PluginHandle,const char* message) { std::printf("PLUGIN %s\n",message);std::fflush(stdout); }
static void LogF(RED4ext::v1::PluginHandle,const char* format,...)
{ va_list args;va_start(args,format);std::vprintf(format,args);va_end(args);std::puts("");std::fflush(stdout); }
static void LogW(RED4ext::v1::PluginHandle,const wchar_t*) { std::puts("PLUGIN wide log"); }
static void LogWF(RED4ext::v1::PluginHandle,const wchar_t*,...) { std::puts("PLUGIN wide formatted log"); }
static void NativeRegistration(void*,Callback callback)
{
    // Matches the receiving game's handler read/copy contract. A deliberately
    // unbridged old caller must not silently pass this negative control.
    probe->copying=true;
    std::array<std::byte,64> destination{};
    std::printf("NATIVE handler read at +0x40: %p\n",static_cast<void*>(callback.handler));std::fflush(stdout);
    callback.handler->copy(destination.data(),callback.buffer);
    std::puts("FAIL unbridged callback unexpectedly survived native copy");Finish(1);
}
template<bool Post> static void StoreNative(void*,Callback callback)
{
    auto& p=*probe;constexpr auto index=Post ? 1 : 0;
    if(p.registrations[index]++)Unknown();
    p.callbacks[index].emplace(std::move(callback));
    std::printf("BRIDGED %s callback: native size=0x%zx handler=+0x%zx\n",Post ? "post-register" : "register",sizeof(Callback),offsetof(Callback,handler));
    std::fflush(stdout);
}
static LONG CALLBACK Fault(EXCEPTION_POINTERS* exception)
{
    if(probe && exception->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
       exception->ExceptionRecord->NumberParameters>=2)
    {
        const auto address=exception->ExceptionRecord->ExceptionInformation[1];
        const auto base=reinterpret_cast<uintptr_t>(probe->unresolved);
        if(base && address>=base && address-base<probe->hashes.size()*probe->page)
        {
            std::printf("UNSUPPORTED first use of relocation 0x%08x access=%llu\n",
                        probe->hashes[(address-base)/probe->page],
                        static_cast<unsigned long long>(exception->ExceptionRecord->ExceptionInformation[0]));
            Finish(78);
        }
    }
    if(probe && probe->negative && probe->copying && exception->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION)
    {
        std::puts("PASS negative control: unbridged old callback faults in native-layout copy");Finish(0);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static bool ScriptAdded(RED4ext::v1::PluginHandle,const wchar_t*)
{
    if(probe->negative || probe->registrations[0]!=1 || probe->registrations[1]!=1)
    { std::puts("FAIL expected both callback phases before script registration");Finish(1); }
    // Exercise the native SDK's retained callback lifetime, but do not invoke
    // arbitrary plugin registration against nonexistent game services.
    for(auto& callback:probe->callbacks)callback.reset();
    std::puts("PASS unchanged DLL passed register/post-register callback conversion and reached script registration; later operations untested");
    Finish(0);
}
struct GetTag;using Get=CallRouter<void*(),GetTag>;
static Get::Entry get;
struct HandleValue { void* instance; RED4ext::RefCnt* refs; };
static HandleValue* ConstructHandle(HandleValue* out,void* instance)
{
    // Fake-engine scaffold only: an explicit host reference keeps each startup
    // object resident. This is not production handle marshalling or admission.
    if(!out || !instance)Unknown();
    for(auto& resident:probe->residents)
    {
        if(!resident.instance)
        {
            resident.instance=instance;
            resident.refs.IncWeakRef();
            const HandleValue weak{instance,&resident.refs};
            std::memcpy(static_cast<std::byte*>(instance)+8,&weak,sizeof(weak));
        }
        if(resident.instance==instance)
        {
            resident.refs.IncRef();*out={instance,&resident.refs};return out;
        }
    }
    Unknown();
}
static uintptr_t Resolve(uint32_t hash,uintptr_t caller)
{
    const auto match=probe->modules.Find(caller);
    if(match.error!=Error::None || match.domain!=probe->domain->id())
    { std::printf("FAIL early caller classification error=%u domain=%u\n",unsigned(match.error),match.domain);Finish(1); }
    std::printf("RESOLVE 0x%08x\n",hash);std::fflush(stdout);
    if(hash==0x4A610F64)
    {
        const auto result=probe->resolver->Resolve(hash,caller);
        if(result.error!=Error::None || !result.address)
        { std::printf("FAIL reviewed resolver error=%u domain=%u\n",unsigned(result.error),result.domain);Finish(1); }
        return result.address;
    }
    if(hash==0x9CCF1543)return reinterpret_cast<uintptr_t>(&probe->serialCounter);
    if(hash==0xBA0C115D)return reinterpret_cast<uintptr_t>(&ConstructHandle);
    // Opaque inaccessible sentinels allow eager address caching, but neither
    // execute code nor provide data. First use terminates the isolated probe.
    // This is diagnostic behavior only, never a compatibility fallback.
    std::lock_guard guard(probe->resolveMutex);
    const auto count=probe->hashCount.load();
    for(unsigned i=0;i<count;++i)if(probe->hashes[i]==hash)
        return reinterpret_cast<uintptr_t>(probe->unresolved+i*probe->page);
    if(count==probe->hashes.size())Unknown();
    probe->hashes[count]=hash;probe->hashCount.store(count+1);
    return reinterpret_cast<uintptr_t>(probe->unresolved+count*probe->page);
}
int wmain(int argc,wchar_t** argv)
{
    try
    {
        probe=std::make_shared<Probe>();probe->negative=argc>2;
        SYSTEM_INFO info{};GetSystemInfo(&info);probe->page=info.dwPageSize;
        probe->unresolved=static_cast<std::byte*>(VirtualAlloc(nullptr,probe->page*probe->hashes.size(),MEM_RESERVE,PAGE_NOACCESS));
        if(!probe->unresolved)return 1;
        if(!AddVectoredExceptionHandler(1,Fault))return 1;
        auto services=BindNativeRtti(reinterpret_cast<RED4ext::CRTTISystem*>(probe->system.data()));
        probe->rtti=std::make_unique<RttiAdapter>(probe->domain,probe->arena,SharedPointers{{probe.get(),Translate},probe},services,probe->system.data());
        probe->nativeTable.fill(reinterpret_cast<uintptr_t>(&Unknown));
        probe->nativeTable[0xC0/8]=reinterpret_cast<uintptr_t>(probe->negative ? &NativeRegistration : &StoreNative<false>);
        probe->nativeTable[0xC8/8]=reinterpret_cast<uintptr_t>(probe->negative ? &NativeRegistration : &StoreNative<true>);
        auto* table=probe->nativeTable.data();std::memcpy(probe->system.data(),&table,sizeof(table));
        auto route=Get::Bind(probe->domain,[]()->void*{return probe->negative ? probe->system.data() : probe->rtti->guest();});
        get=route.entry();
        const auto loader=LoadLibraryW(L"RED4ext.dll");if(!loader)return 1;
        const auto configure=reinterpret_cast<void(*)(uintptr_t(*)(uint32_t,uintptr_t))>(GetProcAddress(loader,"SetProbeResolver"));
        if(!configure)return 1;configure(Resolve);
        const auto path=std::filesystem::absolute(argc>1 ? argv[1] : L"startup_guest.dll");
        probe->modules.AddRoot(path.parent_path().wstring(),probe->domain->id());probe->modules.Seal();
        // Verified CRTTISystem_Get identity from the 2.5 address database. Its
        // receiver here is synthetic; this does not validate the retail body.
        static const Operation operation{"CRTTISystem_Get",0x4A610F64,
            "a15fa31a13e6606cbc5344b6b481eb7909589ea7b37e34ebe376eafcd6e21900",
            AddressKind::Function,"CRTTISystem*()",0};
        static const Schema schema{profile::Core.source,profile::Core.target,profile::Types,std::span(&operation,1)};
        auto catalog=std::make_shared<AddressCatalog>(schema,probe->domain);
        Check(catalog->Bind(operation.id,operation.primary,operation.secondary,operation.kind,
                            operation.signature,reinterpret_cast<uintptr_t>(get)));
        Check(catalog->Seal());
        probe->resolver=std::make_unique<ResolverRouter>(probe->modules,[](void*,uint32_t)->uintptr_t{Unknown();},nullptr);
        probe->resolver->Add(catalog,[pins=probe->pins](const EarlyModules::Match& match,uintptr_t caller) {
            pins->Retain(match.base,caller);
        });probe->resolver->Seal();
        probe->notifications=std::make_unique<ModuleNotifications>(probe->modules);
        const auto plugin=LoadLibraryW(path.c_str());
        if(!plugin){std::printf("FAIL LoadLibrary error=%lu\n",GetLastError());return 1;}
        const auto main=reinterpret_cast<bool(*)(RED4ext::v1::PluginHandle,RED4ext::v1::EMainReason,const RED4ext::v1::Sdk*)>(GetProcAddress(plugin,"Main"));
        if(!main)return 1;
        // The first resolver call already retained this module, including
        // calls made from DllMain. Drop the ordinary LoadLibrary reference.
        probe->pins->Retain(reinterpret_cast<uintptr_t>(plugin),reinterpret_cast<uintptr_t>(main));
        if(!FreeLibrary(plugin) || GetModuleHandleW(path.filename().c_str())!=plugin)return 1;
        RED4ext::v1::Logger logger{};
#define SET_LOG(Level) logger.Level=Log;logger.Level##F=LogF;logger.Level##W=LogW;logger.Level##WF=LogWF
        SET_LOG(Trace);SET_LOG(Debug);SET_LOG(Info);SET_LOG(Warn);SET_LOG(Error);SET_LOG(Critical);
#undef SET_LOG
        RED4ext::v1::Scripts scripts{ScriptAdded,[](const char*)->bool{Unknown();},[](const char*)->bool{Unknown();}};
        RED4ext::v1::Hooking hooks{[](HMODULE,void*,void*,void**)->bool{Unknown();},[](HMODULE,void*)->bool{Unknown();}};
        RED4ext::v1::GameStates states{[](HMODULE,RED4ext::EGameStateType,RED4ext::v1::GameState*)->bool{Unknown();}};
        RED4ext::v1::SemVer version{};version.major=2;version.minor=50;
        RED4ext::v1::Sdk sdk{&version,&logger,&hooks,&states,&scripts};
        Domain::Scope scope(*probe->domain);
        (void)main(plugin,RED4ext::v1::EMainReason::Load,&sdk);
        std::puts("FAIL plugin did not reach the expected script-registration checkpoint");return 1;
    }
    catch(const Failure& e){std::printf("FAIL contract %u\n",unsigned(e.code()));return 1;}
    catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}
}
