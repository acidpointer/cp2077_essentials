#pragma once
#ifdef _WIN32
#include <memory>

namespace compat
{
// An OS reference, distinct from the Domain's admission/ownership counters.
// Keep this in the SharedPointers lifetime owner for every defining module.
// Acquire/release on the control plane, never in VEH or DLL notifications.
class ModulePin final
{
public:
    static std::shared_ptr<ModulePin> Acquire(const void* code);
    ~ModulePin();
    ModulePin(const ModulePin&) = delete;
    ModulePin& operator=(const ModulePin&) = delete;
private:
    explicit ModulePin(void* module) : module_(module) {}
    void* module_;
};
}
#endif
