// The production RedLib registrar and Mod Settings hook lifecycle against a
// fake retail RTTI table and fake loader. No game or mod DLL is loaded.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "Registrar.hpp"
#include <Red/TypeInfo/Registrar.hpp>

namespace {
using Callback = RED4ext::Callback<void(*)()>;
std::array<void*, 0x160 / 8> table{};
struct { void** table; } retailObject{table.data()};
std::vector<Callback> registrations, descriptions;
int registered = 0, described = 0, resolutions = 0, failure = 0, errors = 0;
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

int main() {
  Require(resolutions == 0, "no address resolution under static initialization");
  table[0xC0 / 8] = reinterpret_cast<void*>(&Register);
  table[0xC8 / 8] = reinterpret_cast<void*>(&Describe);
  Red::TypeInfoRegistrar registrar(&OnRegister, &OnDescribe);
  Red::TypeInfoRegistrar::RegisterDiscovered();
  Require(registrations.size() == 1 && descriptions.size() == 1, "RTTI registration vtable slots");
  registrations[0](); descriptions[0]();
  Require(registered == 1 && described == 1, "copied callbacks survive and invoke once");

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
  std::puts("PASS: retail callback copy/invoke and transactional hook lifecycle");
}
