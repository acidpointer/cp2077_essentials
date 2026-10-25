#ifdef _WIN32
#include <compat/ModulePin.hpp>
#include <compat/Error.hpp>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace compat
{
std::shared_ptr<ModulePin> ModulePin::Acquire(const void* code)
{
    if(!code)throw Failure(Error::InvalidRange);
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(code),&module))
        throw Failure(Error::UnknownPointer);
    ModulePin* pin{};
    try { pin=new ModulePin(module); }
    catch(...) { FreeLibrary(module);throw; }
    // shared_ptr destroys pin on control-block allocation failure.
    return std::shared_ptr<ModulePin>(pin);
}
ModulePin::~ModulePin() { FreeLibrary(static_cast<HMODULE>(module_)); }
void ModulePins::Retain(uintptr_t moduleBase,uintptr_t code)
{
    if(!moduleBase || code<moduleBase)throw Failure(Error::InvalidRange);
    for(auto& slot:slots_)
    {
        auto base=slot.base.load(std::memory_order_acquire);
        if(!base)
        {
            if(slot.base.compare_exchange_strong(base,moduleBase,std::memory_order_acq_rel))base=moduleBase;
        }
        if(base!=moduleBase)continue;
        if(slot.pin.load(std::memory_order_acquire))return;
        auto pin=ModulePin::Acquire(reinterpret_cast<void*>(code));
        if(reinterpret_cast<uintptr_t>(pin->module_)!=moduleBase)throw Failure(Error::IdentityMismatch);
        std::shared_ptr<ModulePin> empty;
        // No OS operation runs while atomic<shared_ptr>'s implementation lock
        // is held. The losing local reference is released after CAS returns.
        (void)slot.pin.compare_exchange_strong(empty,pin,std::memory_order_acq_rel);
        return;
    }
    throw Failure(Error::Capacity);
}
}
#endif
