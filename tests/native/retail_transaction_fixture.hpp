#pragma once

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string_view>

#define RED4EXT_STATIC_LIB
#include <RED4ext/Scripting/Natives/Generated/ent/ITransformBinding.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/PersistentState.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/SlotComponent.hpp>
#include <RED4ext/Scripting/Natives/gameITransactionSystem.hpp>
#include <RED4ext/NativeTypes-inl.hpp>

namespace Fixture
{
inline void Require(bool aCondition, const char* aMessage)
{
    if (!aCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", aMessage);
        std::exit(42);
    }
}

// Anchors model objects owned elsewhere in the engine. No native object is
// constructed/destroyed and no game allocator or game DLL is loaded.
template<class T>
struct AnchoredHandle
{
    RED4ext::RefCnt refs;
    alignas(16) std::array<std::byte, 0x40> object{};
    RED4ext::Handle<T> handle;

    AnchoredHandle()
    {
        refs.strongRefs = 2;
        handle.instance = reinterpret_cast<T*>(object.data());
        handle.refCount = &refs;
    }
    ~AnchoredHandle()
    {
        handle.Reset();
        Require(refs.strongRefs == 1, "Handle ownership did not balance");
    }
};

using System = RED4ext::game::ITransactionSystem;
using Slot = RED4ext::game::AttachmentSlotData;
using Predicate = RED4ext::game::AttachmentSlotDataPredicate;
using Listener = RED4ext::game::IAttachmentSlotsListener;

struct RetailSystem
{
    std::array<uintptr_t, 0x4F0 / 8> vtable{};
    alignas(16) std::array<std::byte, 0x48> object{};
    Slot slot{};
    RED4ext::IScriptable* expectedOwner = nullptr;
    const RED4ext::ItemID* expectedItemID = nullptr;
    const RED4ext::Handle<RED4ext::IScriptable>* expectedHandle = nullptr;
    Listener* expectedListener = nullptr;
    RED4ext::RefCnt* listenerRefs = nullptr;
    RED4ext::CName expectedTag{uint64_t{0xABCDEF1234}};
    std::string_view expectedOperation;
    unsigned lockedCalls = 0;
    unsigned unlockedCalls = 0;
    bool expectDefaultAppearance = false;
    bool eraseItemAfterPredicate = false;
    inline static thread_local RetailSystem* current = nullptr;
    inline static thread_local std::set<const void*> exclusiveOwners;

    RetailSystem()
    {
        current = this;
        vtable.fill(reinterpret_cast<uintptr_t>(&WrongSlot));
        // These offsets come from the retail executable, independently of the
        // SDK declaration under test. Do not derive them from member pointers.
        vtable[0x448 / 8] = reinterpret_cast<uintptr_t>(&FindLocked);
        vtable[0x450 / 8] = reinterpret_cast<uintptr_t>(&FindUnlocked);
        vtable[0x498 / 8] = reinterpret_cast<uintptr_t>(&Register);
        vtable[0x4A0 / 8] = reinterpret_cast<uintptr_t>(&Unregister);
        vtable[0x4D8 / 8] = reinterpret_cast<uintptr_t>(&MatchID);
        vtable[0x4E0 / 8] = reinterpret_cast<uintptr_t>(&MatchHandle);
        auto table = vtable.data();
        std::memcpy(object.data(), &table, sizeof(table));
    }

    __declspec(noinline) System* Get() { return reinterpret_cast<System*>(object.data()); }

    static void WrongSlot() { Require(false, "SDK dispatched to the wrong retail vtable slot"); }
    static RetailSystem& Check(System* aThis)
    {
        Require(current && aThis == current->Get(), "this argument changed");
        return *current;
    }
    static Slot* Search(RetailSystem& aSelf, Predicate&& aPredicate)
    {
        const bool found = aPredicate(aSelf.slot);
        if (aSelf.eraseItemAfterPredicate)
            aSelf.slot.itemObject.Reset();
        return found ? &aSelf.slot : nullptr;
    }
    static Slot* FindLocked(System* aThis, RED4ext::IScriptable* aOwner, Predicate&& aPredicate)
    {
        auto& self = Check(aThis);
        Require(aOwner == self.expectedOwner, "slot lookup owner changed");
        Require(!exclusiveOwners.contains(aOwner), "self-deadlock: locking lookup under exclusive ownership");
        ++self.lockedCalls;
        return Search(self, std::move(aPredicate));
    }
    static Slot* FindUnlocked(System* aThis, RED4ext::IScriptable* aOwner, Predicate&& aPredicate)
    {
        auto& self = Check(aThis);
        Require(aOwner == self.expectedOwner, "unlocked lookup owner changed");
        Require(exclusiveOwners.contains(aOwner), "unlocked lookup without exclusive ownership");
        ++self.unlockedCalls;
        return Search(self, std::move(aPredicate));
    }
    static void CheckListener(System* aThis, RED4ext::IScriptable* aOwner,
                              const RED4ext::Handle<Listener>& aListener, std::string_view aOperation)
    {
        auto& self = Check(aThis);
        Require(self.expectedOperation == aOperation, "register/unregister dispatched to the wrong method");
        Require(aOwner == self.expectedOwner && aListener.instance == self.expectedListener,
                "listener arguments changed");
        Require(aListener.refCount == self.listenerRefs && self.listenerRefs->strongRefs == 3,
                "listener was not passed as an owning Handle by value");
    }
    static void Register(System* aThis, RED4ext::IScriptable* aOwner, RED4ext::Handle<Listener> aListener)
    {
        CheckListener(aThis, aOwner, aListener, "register");
    }
    static void Unregister(System* aThis, RED4ext::IScriptable* aOwner, RED4ext::Handle<Listener> aListener)
    {
        CheckListener(aThis, aOwner, aListener, "unregister");
    }
    static bool MatchID(System* aThis, const RED4ext::ItemID& aItemID,
                        const RED4ext::Handle<RED4ext::IScriptable>& aOwner, RED4ext::CName aTag)
    {
        auto& self = Check(aThis);
        Require(self.expectedOperation == "item-id", "item-handle call reached the item-ID method");
        Require(&aItemID == self.expectedItemID && &aOwner == self.expectedHandle && aTag == self.expectedTag,
                "item-ID visual-tag arguments changed");
        return true;
    }
    static bool MatchHandle(System* aThis, const RED4ext::Handle<RED4ext::IScriptable>& aItem,
                            RED4ext::CName aTag, bool aDefault)
    {
        auto& self = Check(aThis);
        Require(&aItem == self.expectedHandle && aTag == self.expectedTag && aDefault == self.expectDefaultAppearance,
                "item-handle visual-tag arguments changed");
        return !aDefault;
    }
};
}

uintptr_t RED4ext::UniversalRelocBase::Resolve(uint32_t)
{
    Fixture::Require(false, "test attempted to resolve a game function");
    return 0;
}

RED4ext::CRTTISystem* RED4ext::CRTTISystem::Get()
{
    Fixture::Require(false, "test attempted to access game RTTI");
    return nullptr;
}
