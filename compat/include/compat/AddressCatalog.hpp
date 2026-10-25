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
    // The RED4ext export supplies only a hash. Its kind is already part of the
    // sealed, identity-checked operation contract; never infer it from an address.
    uintptr_t Resolve(uint32_t primary) const;
    const Domain::Ptr& domain() const noexcept { return domain_; }
    bool sealed() const noexcept { return sealed_; }
private:
    const Schema& schema_;
    Domain::Ptr domain_;
    std::vector<uintptr_t> bindings_;
    bool sealed_{};
};
}
