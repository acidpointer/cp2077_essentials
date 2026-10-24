#pragma once
#include <compat/Domain.hpp>
#include <shared_mutex>
#include <vector>

namespace compat
{
// Control-plane registry, not a loader notification callback implementation.
class DomainRegistry
{
public:
    explicit DomainRegistry(size_t capacity) : capacity_(capacity) { modules_.reserve(capacity); }
    Error Add(uintptr_t base, size_t size, Domain::Ptr domain);
    Domain::Ptr Find(uintptr_t instruction) const;
private:
    struct Module { uintptr_t base; size_t size; Domain::Ptr domain; };
    size_t capacity_;
    mutable std::shared_mutex mutex_;
    std::vector<Module> modules_;
};
}
