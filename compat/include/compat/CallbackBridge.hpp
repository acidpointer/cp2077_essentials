#pragma once

#include <compat/Domain.hpp>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>

namespace compat
{
template<class Signature> struct CallbackHandler;
template<class R, class... Args> struct CallbackHandler<R(Args...)>
{
    R (*invoke)(const void*, Args&&...);
    void (*copy)(void*, void*);
    void (*move)(void*, void*);
    void (*destroy)(void*);
};

template<class Signature, size_t InlineSize> class CallbackBridge;
template<class R, class... Args, size_t InlineSize>
class CallbackBridge<R(Args...), InlineSize>
{
public:
    using Handler = CallbackHandler<R(Args...)>;
    // Decode the handler record with memcpy; do not alias an SDK template object
    // as a different C++ type. The profile/ABI assertions establish this layout.
    static std::unique_ptr<CallbackBridge> Copy(Domain::Ptr domain,
                                               std::span<std::byte> source, const void* handler)
    {
        if (!domain || source.size() != InlineSize || !handler) throw Failure(Error::InvalidCallback);
        auto owner = domain->Retain();
        Handler operations{};
        std::memcpy(&operations, handler, sizeof(operations));
        if (!operations.invoke || !operations.copy || !operations.move || !operations.destroy)
            throw Failure(Error::InvalidCallback);
        auto result = std::unique_ptr<CallbackBridge>(new CallbackBridge(std::move(domain), std::move(owner), operations));
        Domain::Scope call(*result->domain_);
        operations.copy(result->buffer_.data(), source.data());
        result->constructed_ = true;
        return result;
    }
    CallbackBridge(const CallbackBridge&) = delete;
    CallbackBridge& operator=(const CallbackBridge&) = delete;
    ~CallbackBridge() noexcept
    {
        // Cleanup is allowed after Close while our retained owner prevents
        // protocol-compliant module unload. Guest destructors must not throw.
        if (constructed_) { Domain::Scope cleanup(owner_); handler_.destroy(buffer_.data()); }
    }
    R operator()(Args... args) const
    {
        Domain::Scope call(*domain_);
        return handler_.invoke(buffer_.data(), std::forward<Args>(args)...);
    }
private:
    CallbackBridge(Domain::Ptr domain, Domain::Ticket owner, Handler handler)
        : domain_(std::move(domain)), owner_(std::move(owner)), handler_(handler) {}
    Domain::Ptr domain_;
    Domain::Ticket owner_;
    Handler handler_;
    alignas(8) std::array<std::byte, InlineSize> buffer_{};
    bool constructed_{};
};
}
