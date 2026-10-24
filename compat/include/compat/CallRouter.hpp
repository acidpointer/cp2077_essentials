#pragma once

#include <compat/Domain.hpp>
#include <array>
#include <functional>
#include <utility>

namespace compat
{
// Each Tag names a distinct typed operation bank. Slots are never reused, so
// cached entries remain tombstones after retirement. No generated code or casts
// between signatures. Binding/retirement are not loader-notification/VEH APIs.
template<class Signature, class Tag, size_t Capacity = 64> class CallRouter;

template<class R, class... Args, class Tag, size_t Capacity>
class CallRouter<R(Args...), Tag, Capacity>
{
    struct Binding
    {
        Domain::Ptr domain;
        Domain::Ticket owner;
        std::function<R(Args...)> target; // Destroy guest callable before releasing owner.
        Binding(Domain::Ptr d, std::function<R(Args...)> t)
            : domain(std::move(d)), owner(domain->Retain()), target(std::move(t)) {}
        ~Binding() noexcept
        {
            Domain::Scope cleanup(owner);
            target = {}; // Callable cleanup runs in its domain before owner release.
        }
    };
    static auto& Slots()
    {
        // Published engine entries have no detach contract. Keep their control
        // blocks for the process; static destruction must not call into guest
        // DLLs whose teardown order is unknown. Ordinary Retire still releases
        // unpublished bindings. Storage is bounded and initialized by Bind.
        static auto* bank = new std::array<std::atomic<std::shared_ptr<Binding>>, Capacity>{};
        return *bank;
    }
    inline static std::atomic<size_t> next_{};
    template<size_t Index> static R Invoke(Args... args)
    {
        auto binding = Slots()[Index].load(std::memory_order_acquire);
        if (!binding) throw Failure(Error::Retired);
        Domain::Scope call(*binding->domain);
        return binding->target(std::forward<Args>(args)...);
    }
    template<size_t... Index> static constexpr auto Entries(std::index_sequence<Index...>)
    { return std::array<Entry, Capacity>{&Invoke<Index>...}; }
public:
    using Entry = R (*)(Args...);
    class Registration
    {
    public:
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        Registration(Registration&& other) noexcept
            : index_(std::exchange(other.index_, Capacity)), published_(other.published_) {}
        Registration& operator=(Registration&&) = delete;
        ~Registration() { if (index_ < Capacity && !published_) Retire(); }
        Entry entry() const noexcept { return index_ < Capacity ? entries_[index_] : nullptr; }
        // Call before handing the entry to an engine registry with no detach
        // contract. Its owner and callable remain resident for the session.
        void Publish() noexcept { if (index_ < Capacity) published_ = true; }
        Error Retire() noexcept
        {
            if (published_) return Error::Published;
            if (index_ == Capacity) return Error::Retired;
            Slots()[index_].exchange({}, std::memory_order_acq_rel);
            index_ = Capacity;
            return Error::None;
        }
    private:
        friend class CallRouter;
        explicit Registration(size_t index) : index_(index) {}
        size_t index_;
        bool published_{};
    };
    static Registration Bind(Domain::Ptr domain, std::function<R(Args...)> target)
    {
        if (!domain || !target) throw Failure(Error::InvalidSchema);
        auto binding = std::make_shared<Binding>(std::move(domain), std::move(target));
        auto index = next_.load(std::memory_order_relaxed);
        do { if (index >= Capacity) throw Failure(Error::Capacity); }
        while (!next_.compare_exchange_weak(index, index + 1, std::memory_order_relaxed));
        Slots()[index].store(std::move(binding), std::memory_order_release);
        return Registration(index);
    }
private:
    inline static constexpr auto entries_ = Entries(std::make_index_sequence<Capacity>{});
};
}
