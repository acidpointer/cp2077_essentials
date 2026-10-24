// Actual pre-2.5 SDK Create/field access, with a fake engine boundary.
// No game DLL is loaded. The historical headers are extracted without edits.
#define RED4EXT_STATIC_LIB
#include <RED4ext/Scripting/Functions.hpp>
#include <RED4ext/SharedSpinLock-inl.hpp>

using Resolver = uintptr_t (*)(uint32_t);
static Resolver resolver;
static RED4ext::Memory::Vault vault{};
static RED4ext::Memory::PoolStorage storage{};

uintptr_t RED4ext::UniversalRelocBase::Resolve(uint32_t hash) { return resolver(hash); }
RED4ext::Memory::Vault* RED4ext::Memory::Vault::Get() { return &vault; }
RED4ext::CName RED4ext::CNamePool::Add(const char* text) { return CName(text); }

static_assert(sizeof(RED4ext::CClassStaticFunction) == 0xC0);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(RED4ext::CClassStaticFunction, parent) == 0xB0);
static_assert(offsetof(RED4ext::CClassStaticFunction, regIndex) == 0xB8);
#pragma clang diagnostic pop

extern "C" __declspec(dllexport) void Configure(Resolver value)
{
    resolver = value;
    storage.allocatorStorage = reinterpret_cast<uintptr_t>(&vault);
    vault.poolRegistry.nodes[0].storage = &storage;
    vault.poolRegistry.nodes[0].handle = RED4ext::FNV1a32("PoolRTTIFunction");
}

static void ScriptFunction(RED4ext::IScriptable*, RED4ext::CStackFrame*, void*, int64_t) {}

extern "C" __declspec(dllexport) __declspec(noinline) void* Create()
{
    return RED4ext::CClassStaticFunction::Create(nullptr, "Lab", "Lab", &ScriptFunction);
}

extern "C" __declspec(dllexport) __declspec(noinline) void* ReadParent(void* object)
{
    return static_cast<RED4ext::CClassStaticFunction*>(object)->parent;
}

extern "C" __declspec(dllexport) __declspec(noinline) void WriteParent(void* object, void* value)
{
    static_cast<RED4ext::CClassStaticFunction*>(object)->parent = static_cast<RED4ext::CClass*>(value);
}

extern "C" __declspec(dllexport) __declspec(noinline) uint32_t ReadIndex(void* object)
{
    return static_cast<RED4ext::CClassStaticFunction*>(object)->regIndex;
}

// Another allocation with the same size must remain untyped until a typed operation.
extern "C" __declspec(dllexport) __declspec(noinline) void* AllocateSameSize()
{
    RED4ext::Memory::RTTIFunctionAllocator allocator;
    return allocator.Alloc(sizeof(RED4ext::CClassStaticFunction)).memory;
}

extern "C" __declspec(dllexport) __declspec(noinline) void AppendFunction(void* array, void* function)
{
    // CClass::RegisterFunction uses this same inline PushBack operation.
    static_cast<RED4ext::DynArray<RED4ext::CClassStaticFunction*>*>(array)->PushBack(
        static_cast<RED4ext::CClassStaticFunction*>(function));
}
