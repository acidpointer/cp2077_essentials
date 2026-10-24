#include "retail_transaction_fixture.hpp"
#include "App/Extensions/Attachment/SlotQuery.hpp"
#include "App/Extensions/Attachment/HookProtection.hpp"

#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
using Fixture::Require;
struct Component { RED4ext::IScriptable* owner; };
struct Exclusive
{
    const void* owner;
    explicit Exclusive(const void* aOwner) : owner(aOwner) { Fixture::RetailSystem::exclusiveOwners.insert(owner); }
    ~Exclusive() { Fixture::RetailSystem::exclusiveOwners.erase(owner); }
};

std::array<bool, 10> attached{};
int failAttach = -1;
int failDetach = -1;
template<int I>
struct Hook
{
    static void Attach()
    {
        if constexpr (I >= 3)
            Require(attached[0] && attached[1] && attached[2], "consumer installed without every guard");
        if (I == failAttach)
            throw std::runtime_error("injected attach failure");
        attached[I] = true;
    }
    static bool Detach()
    {
        if constexpr (I < 3)
            for (int i = 3; i != 10; ++i)
                Require(!attached[i], "guard removed while a consumer was still attached");
        if (attached[I] && I == failDetach)
            return false;
        attached[I] = false;
        return true;
    }
    static constexpr App::ProtectedHook value{Attach, Detach};
};

void CheckHooks()
{
    const App::HookProtection<3, 7> hooks{{{Hook<0>::value, Hook<1>::value, Hook<2>::value}},
        {{Hook<3>::value, Hook<4>::value, Hook<5>::value, Hook<6>::value,
          Hook<7>::value, Hook<8>::value, Hook<9>::value}}};
    for (failAttach = 0; failAttach < 10; ++failAttach)
    {
        bool threw = false;
        try { hooks.Attach(); }
        catch (const std::runtime_error&) { threw = true; }
        Require(threw, "hook failure was swallowed");
        for (bool state : attached)
            Require(!state, "partial startup left a hook installed");
    }
    failAttach = -1;
    for (int failure : {3, 9})
    {
        hooks.Attach();
        failDetach = failure;
        Require(!hooks.Detach(), "detach failure reported success");
        Require(attached[0] && attached[1] && attached[2], "detach failure removed protection");
        failDetach = -1;
        Require(hooks.Detach() && hooks.Detach(), "detach retry/idempotence failed");
    }
}
}

int main(int argc, char** argv)
{
    using namespace Fixture;
    RetailSystem retail;
    AnchoredHandle<RED4ext::IScriptable> owner;
    AnchoredHandle<RED4ext::IScriptable> otherOwner;
    AnchoredHandle<RED4ext::game::ItemObject> item;
    Component component{owner.handle.instance};
    Component otherComponent{otherOwner.handle.instance};
    retail.expectedOwner = component.owner;
    retail.slot.slotID = RED4ext::TweakDBID("AttachmentSlots.Torso");
    retail.slot.spawningItemID.tdbid = RED4ext::TweakDBID("Items.Test");
    retail.slot.spawningItemID.rngSeed = 1;
    retail.slot.itemObject = item.handle;
    auto query = [&] { return App::ReadAttachmentSlot(retail.Get(), retail.expectedOwner, retail.slot.slotID); };

    if (argc > 1 && std::string_view(argv[1]) == "old-reacquisition")
    {
        Exclusive exclusive(component.owner);
        // Negative control models the old production call, failing immediately
        // rather than spinning forever in a non-recursive reader lock.
        retail.Get()->FindSlotData(component.owner, [](const auto&) { return true; });
        Require(false, "negative control failed to detect reacquisition");
    }

    auto ordinary = query();
    Require(ordinary.itemObject == item.handle.instance && ordinary.spawning, "ordinary slot snapshot lost state");
    ordinary.itemObject.Reset();
    Require(retail.lockedCalls == 1, "ordinary query bypassed locking");

    int request = 1, input = 2, output = 0;
    bool returned = App::SlotLockScope::Invoke(&component,
        [&](Component* current, void* aRequest, void* aItem, void* aOutput)
        {
            Require(current == &component && aRequest == &request && aItem == &input, "four-argument forwarding");
            Exclusive exclusive(current->owner);
            Require(query().itemObject == item.handle.instance, "locked update lost item");
            *static_cast<int*>(aOutput) = 19;
            return false;
        }, &request, &input, &output);
    Require(!returned && output == 19, "boolean result or output argument changed");

    App::SlotLockScope::Invoke(&component, [&](Component* current, void* event)
    {
        Require(event == &input, "event argument changed");
        Exclusive exclusive(current->owner);
        query();
    }, &input);
    returned = App::SlotLockScope::Invoke(&component, [&](Component* current, RED4ext::TweakDBID slot, bool flag)
    {
        Require(slot == retail.slot.slotID && flag, "slot/bool arguments changed");
        Exclusive exclusive(current->owner);
        query();
        return true;
    }, retail.slot.slotID, true);
    Require(returned && retail.unlockedCalls == 3, "one of the three guarded entry shapes missed routing");
    Require(!App::SlotLockScope::Owns(component.owner), "scope leaked after return");

    {
        App::SlotLockScope outer(&component, component.owner);
        Exclusive exclusive(component.owner);
        retail.expectedOwner = otherComponent.owner;
        query(); // Another owner must still acquire its own read lock.
        {
            App::SlotLockScope inner(&otherComponent, otherComponent.owner);
            Exclusive otherExclusive(otherComponent.owner);
            query();
            retail.expectedOwner = component.owner;
            query(); // A -> B -> A searches the full scope stack.
        }
        Require(!App::SlotLockScope::Owns(otherComponent.owner), "nested scope was not restored");
        query();
        std::thread worker([&]
        {
            Require(!App::SlotLockScope::Owns(component.owner), "scope leaked across threads");
            RetailSystem other;
            other.expectedOwner = component.owner;
            App::ReadAttachmentSlot(other.Get(), component.owner, {});
            Require(other.lockedCalls == 1 && other.unlockedCalls == 0, "other thread bypassed locking");
        });
        worker.join();
    }

    try
    {
        App::SlotLockScope::Invoke(&component, [](Component*) { throw std::runtime_error("unwind"); });
    }
    catch (const std::runtime_error&) {}
    Require(!App::SlotLockScope::Owns(component.owner), "scope leaked on C++ unwind");
    Require(!App::SlotLockScope::Owns(nullptr), "null owner matched a scope");

    retail.eraseItemAfterPredicate = true;
    auto snapshot = query();
    Require(!retail.slot.itemObject && snapshot.itemObject == item.handle.instance,
            "item Handle was copied after the engine released the slot");
    snapshot.itemObject.Reset();
    Require(!query().itemObject, "empty slot returned an item");
    auto missing = App::ReadAttachmentSlot(retail.Get(), component.owner, RED4ext::TweakDBID("AttachmentSlots.Missing"));
    Require(!missing.itemObject && !missing.spawning, "missing slot snapshot was not empty");
    CheckHooks();
    std::puts("PASS: three entry ABIs, owner/thread routing, nesting, unwind, snapshots and hook failures");
}
