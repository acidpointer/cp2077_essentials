#include "retail_transaction_fixture.hpp"

template<class T>
void CheckUnlocked(T* aSystem, Fixture::RetailSystem& aRetail)
{
    if constexpr (requires { aSystem->FindSlotDataUnlocked(aRetail.expectedOwner, Fixture::Predicate{}); })
    {
        Fixture::RetailSystem::exclusiveOwners.insert(aRetail.expectedOwner);
        auto found = aSystem->FindSlotDataUnlocked(aRetail.expectedOwner, [](const auto&) { return true; });
        Fixture::Require(found == &aRetail.slot && aRetail.unlockedCalls == 1, "unlocked lookup/result ABI");
        Fixture::RetailSystem::exclusiveOwners.clear();
    }
    else
        Fixture::Require(false, "SDK has no typed unlocked slot API");
}

int main(int argc, char** argv)
{
    using namespace Fixture;
    RetailSystem retail;
    AnchoredHandle<RED4ext::IScriptable> owner;
    AnchoredHandle<Listener> listener;
    RED4ext::ItemID itemID{};
    retail.expectedOwner = owner.handle.instance;
    retail.expectedHandle = &owner.handle;
    retail.expectedItemID = &itemID;
    retail.expectedListener = listener.handle.instance;
    retail.listenerRefs = &listener.refs;
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    auto system = retail.Get();

    if (selected == "all" || selected == "lookup")
    {
        unsigned calls = 0;
        auto found = system->FindSlotData(retail.expectedOwner, [&](const auto& slot)
        {
            ++calls;
            return &slot == &retail.slot;
        });
        Require(found == &retail.slot && calls == 1 && retail.lockedCalls == 1, "locking lookup/predicate ABI");
        Require(!system->FindSlotData(retail.expectedOwner, [](const auto&) { return false; }), "missing slot result");
        CheckUnlocked(system, retail);
    }
    if (selected == "all" || selected == "register")
    {
        retail.expectedOperation = "register";
        system->RegisterSlotListener(retail.expectedOwner, listener.handle);
        Require(listener.refs.strongRefs == 2, "register leaked its by-value handle");
    }
    if (selected == "all" || selected == "unregister")
    {
        retail.expectedOperation = "unregister";
        system->UnregisterSlotListener(retail.expectedOwner, listener.handle);
        Require(listener.refs.strongRefs == 2, "unregister leaked its by-value handle");
    }
    if (selected == "all" || selected == "item-id")
    {
        retail.expectedOperation = "item-id";
        Require(system->MatchVisualTagByItemID(itemID, owner.handle, retail.expectedTag), "item-ID result lost");
    }
    if (selected == "all" || selected == "item-handle")
    {
        retail.expectedOperation = "item-handle";
        Require(system->MatchVisualTag(owner.handle, retail.expectedTag, false), "item-handle result lost");
        retail.expectDefaultAppearance = true;
        Require(!system->MatchVisualTag(owner.handle, retail.expectedTag, true), "bool argument/result changed");
    }
    std::puts("PASS: retail transaction vtable dispatch, arguments, results and Handle ownership");
}
