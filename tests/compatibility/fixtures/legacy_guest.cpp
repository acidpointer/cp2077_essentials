// Actual old SDK operations; fake services are supplied by compat_sdk_tests.
#define RED4EXT_STATIC_LIB
#include <RED4ext/Scripting/Functions.hpp>
#include <RED4ext/Scripting/Utils.hpp>
#undef RED4EXT_INLINE
#define RED4EXT_INLINE inline
#include <RED4ext/RTTITypes-inl.hpp>
#include <RED4ext/SharedSpinLock-inl.hpp>
#include <RED4ext/Scripting/OpcodeHandlers-inl.hpp>
#include <RED4ext/Callback.hpp>
#include <cstring>

struct Counts { int live, copies, calls; };
static Counts* counts;
static RED4ext::CClass* guest_class;
static RED4ext::CClassStaticFunction* guest_function;
static uintptr_t (*resolve)(uint32_t);
static RED4ext::Memory::Vault vault{};
static RED4ext::Memory::PoolStorage storage{};
uintptr_t RED4ext::UniversalRelocBase::Resolve(uint32_t hash) { return resolve(hash); }
RED4ext::Memory::Vault* RED4ext::Memory::Vault::Get() { return &vault; }
RED4ext::CName RED4ext::CNamePool::Add(const char* text) { return text ? CName(text) : CName(uint64_t{0}); }

static void Script(RED4ext::IScriptable*, RED4ext::CStackFrame* frame, int* result, int64_t)
{
    int a = 0, b = 0;
    RED4ext::GetParameter(frame, &a);
    RED4ext::GetParameter(frame, &b);
    frame->Step();
    *result = a + b;
}
static void Register()
{
    ++counts->calls;
    guest_function = RED4ext::CClassStaticFunction::Create(guest_class, "Research", "Research", &Script);
    guest_class->RegisterFunction(guest_function);
}
struct Payload
{
    Payload() { ++counts->live; }
    Payload(const Payload&) { ++counts->live; ++counts->copies; }
    ~Payload() { --counts->live; }
};
extern "C" __declspec(dllexport) void Configure(uintptr_t (*resolver)(uint32_t), void* cls, Counts* value)
{
    resolve = resolver; guest_class = static_cast<RED4ext::CClass*>(cls); counts = value;
    storage.allocatorStorage = reinterpret_cast<uintptr_t>(&vault);
    vault.poolRegistry.nodes[0].storage = &storage;
    vault.poolRegistry.nodes[0].handle = RED4ext::FNV1a32("PoolRTTIFunction");
}
extern "C" __declspec(dllexport) void Install(void* sink)
{
    using Callback = RED4ext::Callback<void (*)()>;
    reinterpret_cast<void (*)(Callback)>(sink)(Callback([owned = Payload()] { Register(); }));
}
extern "C" __declspec(dllexport) void* Lookup()
{
    return guest_class->GetFunction("Research");
}
extern "C" __declspec(dllexport) void ChangeName()
{
    guest_function->fullName = RED4ext::CName("ChangedAfterRegistration");
}
