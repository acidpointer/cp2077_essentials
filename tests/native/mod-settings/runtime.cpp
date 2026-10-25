// The production RedLib registrar and Mod Settings hook lifecycle against a
// fake retail RTTI table and fake loader. Optionally loads a copied plugin DLL;
// no game DLL is loaded and engine registration callbacks are not executed.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "Registrar.hpp"
#include "Addresses.hpp"
#include <Red/TypeInfo/Registrar.hpp>
#include <RED4ext/Detail/AddressHashes.hpp>
#include "discovery.hpp"

namespace {
using Callback = RED4ext::Callback<void(*)()>;
std::array<void*, 0x160 / 8> table{};
struct { void** table; } retailObject{table.data()};
std::vector<Callback> registrations, descriptions;
int registered = 0, described = 0, resolutions = 0, failure = 0, errors = 0;
int destroyedValueHolders = 0;
std::vector<uintptr_t> attached, detached;
void Require(bool value, const char* message) {
  if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(42); }
}
void Capture(std::vector<Callback>& queue, Callback& callback) {
  auto handler = *reinterpret_cast<Callback::HandlerPtr*>(reinterpret_cast<uint8_t*>(&callback) + 0x40);
  Require(handler && handler == callback.handler, "callback handler at retail +0x40");
  Callback copy;
  copy.handler = handler;
  handler->copy(copy.buffer, callback.buffer); // Retail copies through its handler.
  queue.push_back(std::move(copy));
}
void Register(void*, Callback callback) { Capture(registrations, callback); }
void Describe(void*, Callback callback) { Capture(descriptions, callback); }
void OnRegister() { ++registered; }
void OnDescribe() { ++described; }
RED4ext::CRTTISystem* GetTestRTTI() { return reinterpret_cast<RED4ext::CRTTISystem*>(&retailObject); }
void* DestructTestValueHolder(RED4ext::IScriptable* value) {
  Require(value->valueHolder == nullptr, "fixture did not construct an engine value holder");
  ++destroyedValueHolders;
  return nullptr;
}
uintptr_t ResolvePlugin(uint32_t hash) {
  if (hash == RED4ext::Detail::AddressHashes::CRTTISystem_Get)
    return reinterpret_cast<uintptr_t>(&GetTestRTTI);
  if (hash == RED4ext::Detail::AddressHashes::ISerializable_Counter) {
    static int64_t serializableCounter = 0;
    return reinterpret_cast<uintptr_t>(&serializableCounter);
  }
  if (hash == RED4ext::Detail::AddressHashes::IScriptable_DestructValueHolder)
    return reinterpret_cast<uintptr_t>(&DestructTestValueHolder);
  // Main only resolves hook targets; fake Hooking::Attach never executes them.
  using namespace ModSettings::Addresses;
  Require(hash == GetMappingCollection || hash == ClearRawMappingState || hash == ApplyOverrides ||
          hash == SkipKey || hash == ReleaseScriptData || hash == CalcInputHash,
          "plugin requested an engine service outside the isolated Main check");
  return hash;
}
void Info(RED4ext::v1::PluginHandle, const char*) {}
void InfoF(RED4ext::v1::PluginHandle, const char*, ...) {}
bool AddScript(RED4ext::v1::PluginHandle, const wchar_t*) { return true; }
bool Attach(RED4ext::v1::PluginHandle, void* target, void* detour, void** original) {
  auto id = reinterpret_cast<uintptr_t>(target);
  attached.push_back(id);
  if (static_cast<int>(id) == failure) return false;
  *original = detour;
  return true;
}
bool Detach(RED4ext::v1::PluginHandle, void* target) {
  detached.push_back(reinterpret_cast<uintptr_t>(target));
  return true;
}
void Error(RED4ext::v1::PluginHandle, const char*, ...) { ++errors; }
void* original1;
void* original2;
void* original3;
void Detour() {}
ModModuleHookHash hook1("first", 1, reinterpret_cast<void*>(&Detour), &original1);
ModModuleHookHash hook2("second", 2, reinterpret_cast<void*>(&Detour), &original2);
ModModuleHookHash hook3("third", 3, reinterpret_cast<void*>(&Detour), &original3);
}

RED4ext::CRTTISystem* RED4ext::CRTTISystem::Get() { return reinterpret_cast<CRTTISystem*>(&retailObject); }
uintptr_t RED4ext::UniversalRelocBase::Resolve(uint32_t hash) { ++resolutions; return hash; }

int main(int argc, char** argv) {
  Require(resolutions == 0, "no address resolution under static initialization");
  table[0xC0 / 8] = reinterpret_cast<void*>(&Register);
  table[0xC8 / 8] = reinterpret_cast<void*>(&Describe);
  Red::TypeInfoRegistrar registrar(&OnRegister, &OnDescribe);
  Red::TypeInfoRegistrar::RegisterDiscovered();
  Require(registrations.size() == 1 && descriptions.size() == 1, "RTTI registration vtable slots");
  registrations[0](); descriptions[0]();
  Require(registered == 1 && described == 1, "copied callbacks survive and invoke once");
  Require(discoveredRegistrations == 1 && discoveredDescriptions == 1 && discoveredExpansions == 1,
          "declared RTTI classes and expansions discovered exactly once across translation units");

  RED4ext::v1::Logger logger{}; logger.ErrorF = Error;
  RED4ext::v1::Hooking hooking{Attach, Detach};
  RED4ext::v1::Sdk sdk{}; sdk.logger = &logger; sdk.hooking = &hooking;
  auto& factory = ModModuleFactory::GetInstance();
  failure = 2;
  Require(!factory.Load(&sdk, nullptr), "failed attachment returns failure");
  Require(attached == std::vector<uintptr_t>{1, 2} && detached == std::vector<uintptr_t>{1},
          "failed hook tried once, earlier hook rolled back");
  Require(errors == 1 && !hook1.attached && !hook2.attached && !hook3.attached, "rollback state");
  attached.clear(); detached.clear(); failure = 0;
  Require(factory.Load(&sdk, nullptr), "successful hook installation");
  Require(attached == std::vector<uintptr_t>{1, 2, 3}, "all hooks attached once");
  factory.Unload(&sdk, nullptr);
  Require(detached == std::vector<uintptr_t>{3, 2, 1}, "reverse detach order");
  factory.Unload(&sdk, nullptr);
  Require(detached.size() == 3, "repeated unload is harmless");
  if (argc == 2) {
    // The plugin must be copied into this private fixture directory without its
    // archive: no ArchiveXL/game allocator or native class backend is emulated.
    // Exercise the real DLL initializers and Main, then inspect queued callbacks.
    auto loader = LoadLibraryW(L"RED4ext.dll");
    Require(loader != nullptr, "private loader shim loads");
    using SetResolver = void(*)(uintptr_t (*)(uint32_t));
    auto setResolver = reinterpret_cast<SetResolver>(GetProcAddress(loader, "SetTestResolver"));
    Require(setResolver != nullptr, "private resolver setter exported");
    setResolver(&ResolvePlugin);
    auto plugin = LoadLibraryA(argv[1]);
    if (!plugin) std::fprintf(stderr, "LoadLibrary error: %lu\n", GetLastError());
    Require(plugin != nullptr, "rebuilt plugin DLL loads");
    using Main = bool(*)(RED4ext::v1::PluginHandle, RED4ext::v1::EMainReason, const RED4ext::v1::Sdk*);
    auto entry = reinterpret_cast<Main>(GetProcAddress(plugin, "Main"));
    Require(entry != nullptr, "plugin Main exported");
    registrations.clear(); descriptions.clear(); attached.clear(); detached.clear();
    logger.Info = Info; logger.InfoF = InfoF;
    RED4ext::v1::Scripts scripts{}; scripts.Add = AddScript; sdk.scripts = &scripts;
    Require(entry(plugin, RED4ext::v1::EMainReason::Load, &sdk), "plugin Main(Load) succeeds");
    Require(registrations.size() == 1 && descriptions.size() == 1,
            "actual DLL discovers native RTTI registration and description callbacks");
    Require(!attached.empty(), "actual DLL installs hooks");
    Require(entry(plugin, RED4ext::v1::EMainReason::Unload, &sdk), "plugin Main(Unload) succeeds");
    Require(attached.size() == detached.size(), "actual DLL detaches all hooks");
    // Callback handlers point into the plugin, so destroy copies before unloading.
    registrations.clear(); descriptions.clear();
    Require(FreeLibrary(plugin) && destroyedValueHolders == 1, "actual DLL global instance destroyed");
    Require(FreeLibrary(loader), "private loader shim unloaded");
    std::puts("PASS: actual plugin DLL discovers RTTI during Main(Load)");
  }
  std::puts("PASS: declarative RTTI discovery, retail callback copy/invoke and transactional hook lifecycle");
}
