// Uses the actual old SDK resolver export lookup and registration calls.
#define RED4EXT_STATIC_LIB
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>
#include <RED4ext/Api/v1/EMainReason.hpp>
#undef RED4EXT_INLINE
#define RED4EXT_INLINE inline
#include <RED4ext/Api/v1/SemVer-inl.hpp>
#include <RED4ext/Relocation-inl.hpp>
#include <RED4ext/RTTISystem-inl.hpp>
static void Register() {}
// The first resolution occurs before LoadLibrary returns. Later virtual calls
// also run on a worker with no inherited host TLS domain.
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,void*)
{ if(reason==DLL_PROCESS_ATTACH)RED4ext::CRTTISystem::Get()->AddRegisterCallback(&Register);return TRUE; }
static DWORD WINAPI Worker(void*)
{
    // Construct a new relocation here: exercise both fresh export resolution
    // and an already cached RTTI entry on a thread without inherited host TLS.
    RED4ext::UniversalRelocFunc<RED4ext::CRTTISystem*(*)()> get(0x4A610F64);
    if(get()!=RED4ext::CRTTISystem::Get())return 1;
    get()->AddPostRegisterCallback(&Register);return 0;
}
extern "C" __declspec(dllexport) bool Main(RED4ext::v1::PluginHandle handle,RED4ext::v1::EMainReason reason,
                                           const RED4ext::v1::Sdk* sdk)
{
    if(reason==RED4ext::v1::EMainReason::Load)
    {
        const auto worker=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
        if(!worker || WaitForSingleObject(worker,5000)!=WAIT_OBJECT_0)return false;
        DWORD result{};GetExitCodeThread(worker,&result);CloseHandle(worker);
        if(result)return false;
        return sdk->scripts->Add(handle,L"fixture.reds");
    }
    return true;
}
