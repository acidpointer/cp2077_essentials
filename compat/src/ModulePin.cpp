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
}
#endif
