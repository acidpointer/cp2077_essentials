#include <compat/DomainRegistry.hpp>
#include <limits>
#include <mutex>

namespace compat
{
Error DomainRegistry::Add(uintptr_t base, size_t size, Domain::Ptr domain)
{
    if (!domain || !base || !size || size > std::numeric_limits<uintptr_t>::max() - base)
        return Error::InvalidRange;
    std::unique_lock lock(mutex_);
    for (const auto& module : modules_)
    {
        if (module.domain->id() == domain->id() && module.domain != domain) return Error::IdentityMismatch;
        if (base < module.base + module.size && module.base < base + size) return Error::Overlap;
    }
    if (modules_.size() == capacity_) return Error::Capacity;
    modules_.push_back({base, size, std::move(domain)});
    return Error::None;
}

Domain::Ptr DomainRegistry::Find(uintptr_t instruction) const
{
    std::shared_lock lock(mutex_);
    for (const auto& module : modules_)
        if (instruction >= module.base && instruction - module.base < module.size) return module.domain;
    return {};
}
}
