#include <cstddef>
#include <RED4ext/Callback.hpp>
#include <RED4ext/GameEngine.hpp>
#include <RED4ext/ResourceDepot.hpp>
#include <RED4ext/Scripting/Stack.hpp>

// Explicit checks: the SDK's general offset macro is disabled under Clang.
using FixedCallback = RED4ext::Callback<void (*)()>;
static_assert(sizeof(void*) == 8);
static_assert(sizeof(FixedCallback) == 0x48);
static_assert(offsetof(FixedCallback, handler) == 0x40);
static_assert(sizeof(RED4ext::CGameEngine) == 0x390);
// The engine is polymorphic; this deliberate Clang extension still checks its real ABI.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
static_assert(offsetof(RED4ext::CGameEngine, framework) == 0x348);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
static_assert(sizeof(RED4ext::CStackFrame) == 0xA8);
static_assert(static_cast<unsigned>(RED4ext::ArchiveScope::Mod) == 6);
