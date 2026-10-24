#pragma once
#include <compat/CallbackBridge.hpp>

namespace compat
{
// Instantiate with the current SDK's fixed Callback<void(*)()> (64-byte
// payload). Construct directly from a lambda; never instantiate its broken
// const-copy path. The old handler owns/copies/destroys the 32-byte payload.
template<class NativeCallback>
NativeCallback AdaptRegistrationCallback(Domain::Ptr domain, std::span<std::byte> legacyPayload, const void* handler)
{
    static_assert(sizeof(NativeCallback)==0x48);
    static_assert(offsetof(NativeCallback,handler)==0x40);
    auto bridge=std::shared_ptr<CallbackBridge<void(),32>>(CallbackBridge<void(),32>::Copy(std::move(domain),legacyPayload,handler));
    return NativeCallback([bridge=std::move(bridge)] { (*bridge)(); });
}
}
