// A private loader shim for the optional DLL Main(Load) check. Never installed.
#include <cstdint>
using Resolver = uintptr_t (*)(uint32_t);
static Resolver resolver;
extern "C" __declspec(dllexport) void SetTestResolver(Resolver value) { resolver = value; }
extern "C" __declspec(dllexport) uintptr_t RED4ext_ResolveAddress(uint32_t hash) { return resolver(hash); }
