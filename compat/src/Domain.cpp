#include <compat/Domain.hpp>

namespace compat
{
thread_local Domain::Scope* Domain::current_ = nullptr;

Domain::Ptr Domain::Create(uint32_t id, std::string source, std::string target)
{
    if (!id || source.empty() || target.empty()) throw Failure(Error::InvalidSchema);
    return Ptr(new Domain(id, std::move(source), std::move(target)));
}

Domain::Ticket Domain::Acquire(uint64_t unit, uint64_t mask, bool continuation)
{
    auto value = state_.load(std::memory_order_acquire);
    for (;;)
    {
        if ((value & ClosedBit) && !continuation) throw Failure(Error::Closed);
        if ((value & mask) == mask) throw Failure(Error::Capacity);
        if (state_.compare_exchange_weak(value, value + unit, std::memory_order_acq_rel,
                                        std::memory_order_acquire))
            return Ticket(shared_from_this(), unit);
    }
}

void Domain::Ticket::Reset() noexcept
{
    if (domain_) { domain_->state_.fetch_sub(unit_, std::memory_order_acq_rel); domain_.reset(); }
}

Domain* Domain::Current() noexcept { return current_ ? current_->domain_ : nullptr; }

bool Domain::ActiveOnThread() const noexcept
{
    for (auto* scope = current_; scope; scope = scope->prior_)
        if (scope->domain_ == this) return true;
    return false;
}

Domain::Scope::Scope(Domain& domain)
    : call_(domain.Acquire(1, ActiveMask, domain.ActiveOnThread())), domain_(&domain), prior_(current_)
{
    current_ = this;
}

Domain::Scope::Scope(const Ticket& owner) : domain_(nullptr), prior_(current_)
{
    if (!owner.domain_ || owner.unit_ != OwnerUnit) throw Failure(Error::InvalidCallback);
    domain_ = owner.domain_.get();
    call_ = domain_->Acquire(1, ActiveMask, true);
    current_ = this;
}
}
