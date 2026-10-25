// Test-only RED4ext export surface. Never linked into runtime packages.
#include <cstdint>
#include <atomic>
#include <intrin.h>
using Resolver=uintptr_t (*)(uint32_t,uintptr_t);
static std::atomic<Resolver> resolver{};
extern "C" __declspec(dllexport) void SetProbeResolver(Resolver value) { resolver.store(value); }
extern "C" __declspec(dllexport) uintptr_t RED4ext_ResolveAddress(uint32_t hash)
{ auto fn=resolver.load();return fn ? fn(hash,reinterpret_cast<uintptr_t>(_ReturnAddress())) : 0; }
