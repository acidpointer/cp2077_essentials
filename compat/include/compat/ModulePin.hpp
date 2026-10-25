#pragma once
#ifdef _WIN32
#include <memory>
#include <atomic>
#include <array>

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
    friend class ModulePins;
    explicit ModulePin(void* module) : module_(module) {}
    void* module_;
};
// Session-resident defining modules. Concurrent first resolvers may acquire
// duplicate OS references; publication keeps one and releases the others. No
// application mutex is held across an OS loader call (avoids lock inversion).
// Keep this owner in SharedPointers until all guest code/payloads have drained.
// Construct/destroy outside DLL notifications/VEH; Retain is resolver work.
class ModulePins final
{
public:
    // moduleBase/code come from a validated live EarlyModules range. Never
    // accept an arbitrary caller-supplied pair as an admission policy.
    void Retain(uintptr_t moduleBase,uintptr_t code);
private:
    struct Slot
    {
        std::atomic<uintptr_t> base{};
        std::atomic<std::shared_ptr<ModulePin>> pin;
    };
    std::array<Slot,128> slots_;
};
}
#endif
