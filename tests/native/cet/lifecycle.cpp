#include "common/Runtime.h"
#include <Windows.h>
#include <cassert>
#include <chrono>
#include <future>
#include <thread>
#include <vector>
#include <iostream>
#include <cstdlib>
#undef assert
#define assert(condition) do { if (!(condition)) { std::cerr << "Check failed: " << #condition << " at " << __FILE__ << ':' << __LINE__ << '\n'; std::exit(1); } } while (0)

// The gate/transaction implementation is production code. Only the MinHook
// calls are replaced so ordering, rollback and failure paths are deterministic.
static std::vector<void*> queued, enabled, disabled;
static bool failCreate = false, failApply = false;
extern "C" MH_STATUS WINAPI MH_Initialize() { return MH_OK; }
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID, LPVOID* original)
{
    if (failCreate) return MH_ERROR_MEMORY_ALLOC;
    *original = target;
    return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_OK; }
extern "C" MH_STATUS WINAPI MH_QueueEnableHook(LPVOID target) { queued.push_back(target); return MH_OK; }
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID target) { disabled.push_back(target); return MH_OK; }
extern "C" MH_STATUS WINAPI MH_ApplyQueued()
{
    if (failApply) return MH_ERROR_MEMORY_PROTECT;
    enabled = queued; queued.clear(); return MH_OK;
}
extern "C" const char* WINAPI MH_StatusToString(MH_STATUS) { return "injected hook failure"; }
int main()
{
    { Runtime::Callback stopped; assert(!stopped); }
    Runtime::Start();
    std::promise<void> entered, release, stopping;
    auto released = release.get_future();
    bool inside = false;
    std::thread worker([&] { Runtime::Callback callback; assert(callback); inside = true; entered.set_value(); released.wait(); inside = false; });
    entered.get_future().wait();
    auto stopped = std::async(std::launch::async,[&] { stopping.set_value(); Runtime::Stop([&] { assert(!inside); }); });
    stopping.get_future().wait();
    assert(stopped.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready);
    release.set_value(); worker.join(); stopped.get(); assert(!Runtime::Running());
    Runtime::Start();
    bool finalized = false;
    { Runtime::Callback self; assert(self); Runtime::Stop([&] { finalized = true; }); assert(!finalized); }
    assert(finalized);
    Hooks::Begin();
    void* original = nullptr; auto* target = reinterpret_cast<void*>(0x1000);
    Hooks::Create(target,reinterpret_cast<void*>(0x2000),&original);
    Hooks::Enable(target); assert(enabled.empty() && original == target);
    Runtime::Start(); Hooks::Commit(); assert(enabled.size() == 1);
    failCreate = true;
    try { Hooks::Create(reinterpret_cast<void*>(0x3000),nullptr,&original); assert(false); } catch (const std::exception&) {}
    Hooks::Disable(); assert(disabled.size() == 1 && disabled[0] == target);
    failApply = true;
    try { Hooks::Commit(); assert(false); } catch (const std::exception&) {}
    Runtime::Stop([]{}); assert(disabled.size() == 2);
    std::cout << "CET callback quiescence, self-stop and hook transaction checks passed\n";
}
