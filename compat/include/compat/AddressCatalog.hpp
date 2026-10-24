#pragma once
#include <compat/Schema.hpp>
#include <compat/Domain.hpp>
#include <string>
#include <vector>

namespace compat
{
// Build and seal before routing. The externally owned schema must remain alive.
// Observations come from a validated target image/database, not from plugin input.
class AddressCatalog
{
public:
    AddressCatalog(const Schema& schema, Domain::Ptr domain) : schema_(schema), domain_(std::move(domain))
    {
        Check(schema.Validate());
        if (!domain_ || domain_->source() != schema.source || domain_->target() != schema.target)
            throw Failure(Error::IdentityMismatch);
        bindings_.resize(schema.operations.size());
    }
    Error Bind(std::string_view id, uint32_t primary, std::string_view secondary,
               AddressKind kind, std::string_view signature, uintptr_t adapter, size_t table_size = 0);
    Error Seal() noexcept;
    uintptr_t Resolve(uint32_t primary, AddressKind expected) const;
private:
    const Schema& schema_;
    Domain::Ptr domain_;
    std::vector<uintptr_t> bindings_;
    bool sealed_{};
};
}
