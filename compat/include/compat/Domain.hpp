#pragma once

#include <compat/Error.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace compat
{
// Stable control block. The loader must keep module code mapped until CanUnload.
// Closing is terminal; active calls and retained owners share one admission CAS.
class Domain final : public std::enable_shared_from_this<Domain>
{
public:
    using Ptr = std::shared_ptr<Domain>;
    class Ticket
    {
    public:
        Ticket() = default;
        Ticket(const Ticket&) = delete;
        Ticket& operator=(const Ticket&) = delete;
        Ticket(Ticket&& other) noexcept : domain_(std::move(other.domain_)), unit_(other.unit_) {}
        Ticket& operator=(Ticket&& other) noexcept
        {
            if (this != &other) { Reset(); domain_ = std::move(other.domain_); unit_ = other.unit_; }
            return *this;
        }
        ~Ticket() { Reset(); }
        explicit operator bool() const noexcept { return bool(domain_); }
        void Reset() noexcept;
    private:
        friend class Domain;
        Ticket(Ptr domain, uint64_t unit) : domain_(std::move(domain)), unit_(unit) {}
        Ptr domain_;
        uint64_t unit_{};
    };

    static Ptr Create(uint32_t id, std::string source, std::string target);
    Ticket Enter() { return Acquire(1, ActiveMask); }
    Ticket Retain() { return Acquire(OwnerUnit, OwnerMask); }
    void Close() noexcept { state_.fetch_or(ClosedBit, std::memory_order_acq_rel); }
    bool IsClosed() const noexcept { return (state_.load(std::memory_order_acquire) & ClosedBit) != 0; }
    bool CanUnload() const noexcept { return state_.load(std::memory_order_acquire) == ClosedBit; }
    uint32_t id() const noexcept { return id_; }
    const std::string& source() const noexcept { return source_; }
    const std::string& target() const noexcept { return target_; }
    static Domain* Current() noexcept;

    class Scope
    {
    public:
        explicit Scope(Domain& domain);
        // Retained-owner capability permits cleanup after terminal Close.
        explicit Scope(const Ticket& owner);
        ~Scope() { current_ = prior_; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        friend class Domain;
        Ticket call_;
        Domain* domain_;
        Scope* prior_;
    };
private:
    Domain(uint32_t id, std::string source, std::string target)
        : id_(id), source_(std::move(source)), target_(std::move(target)) {}
    Ticket Acquire(uint64_t unit, uint64_t mask, bool continuation = false);
    bool ActiveOnThread() const noexcept;
    static constexpr uint64_t ClosedBit = uint64_t{1} << 63;
    static constexpr uint64_t ActiveMask = 0xFFFFFFFFull;
    static constexpr uint64_t OwnerUnit = uint64_t{1} << 32;
    static constexpr uint64_t OwnerMask = 0x7FFFFFFF00000000ull;
    std::atomic<uint64_t> state_{};
    uint32_t id_;
    std::string source_, target_;
    static thread_local Scope* current_;
};
}
