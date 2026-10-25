#ifdef _WIN32
#include <compat/EarlyModules.hpp>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
namespace compat
{
namespace
{
struct Notification
{
    ULONG flags;
    const UNICODE_STRING* fullName;
    const UNICODE_STRING* baseName;
    void* base;
    ULONG size;
};
static_assert(offsetof(Notification,base)==0x18 && sizeof(Notification)==0x28);
using Register=LONG (NTAPI*)(ULONG,void (CALLBACK*)(ULONG,const Notification*,void*),void*,void**);
using Unregister=LONG (NTAPI*)(void*);
void CALLBACK Notify(ULONG reason,const Notification* notification,void* context)
{
    auto& modules=*static_cast<EarlyModules*>(context);
    if(reason==1)
    {
        const auto* name=notification->fullName;
        const auto text=name && name->Buffer && !(name->Length%sizeof(wchar_t)) ?
            std::wstring_view(name->Buffer,name->Length/sizeof(wchar_t)) : std::wstring_view{};
        (void)modules.Loaded(reinterpret_cast<uintptr_t>(notification->base),notification->size,text);
    }
    else if(reason==2)(void)modules.Unloaded(reinterpret_cast<uintptr_t>(notification->base));
}
}
ModuleNotifications::ModuleNotifications(EarlyModules& modules)
{
    if(!modules.sealed())throw Failure(Error::InvalidSchema);
    const auto module=GetModuleHandleW(L"ntdll.dll");
    const auto install=reinterpret_cast<Register>(GetProcAddress(module,"LdrRegisterDllNotification"));
    unregister_=reinterpret_cast<void*>(GetProcAddress(module,"LdrUnregisterDllNotification"));
    if(!install || !unregister_ || install(0,Notify,&modules,&cookie_)<0)throw Failure(Error::UnsupportedAccess);
}
ModuleNotifications::~ModuleNotifications()
{
    if(cookie_ && reinterpret_cast<Unregister>(unregister_)(cookie_)<0)std::terminate();
}
}
#endif
