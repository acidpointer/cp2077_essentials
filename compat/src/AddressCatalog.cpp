#include <compat/AddressCatalog.hpp>

namespace compat
{
Error AddressCatalog::Bind(std::string_view id, uint32_t primary, std::string_view secondary,
                           AddressKind kind, std::string_view signature, uintptr_t adapter, size_t table_size)
{
    if (sealed_) return Error::Closed;
    for (size_t i = 0; i < schema_.operations.size(); ++i)
    {
        const auto& operation = schema_.operations[i];
        if (operation.id != id) continue;
        if (operation.primary != primary || operation.secondary != secondary) return Error::IdentityMismatch;
        if (operation.signature != signature) return Error::IdentityMismatch;
        if (kind != operation.kind || table_size != operation.table_size) return Error::WrongKind;
        if (!adapter || bindings_[i]) return Error::InvalidRange;
        bindings_[i] = adapter;
        return Error::None;
    }
    return Error::UnknownOperation;
}

Error AddressCatalog::Seal() noexcept
{
    for (auto entry : bindings_) if (!entry) return Error::UnknownOperation;
    sealed_ = true;
    return Error::None;
}

uintptr_t AddressCatalog::Resolve(uint32_t primary, AddressKind expected) const
{
    if (!sealed_) throw Failure(Error::Closed);
    if (Domain::Current() != domain_.get()) throw Failure(Error::IdentityMismatch);
    for (size_t i = 0; i < schema_.operations.size(); ++i)
    {
        const auto& operation = schema_.operations[i];
        if (operation.primary != primary) continue;
        if (operation.kind != expected) throw Failure(Error::WrongKind);
        return bindings_[i];
    }
    throw Failure(Error::UnknownOperation);
}

uintptr_t AddressCatalog::Resolve(uint32_t primary) const
{
    if (!sealed_) throw Failure(Error::Closed);
    if (Domain::Current() != domain_.get()) throw Failure(Error::IdentityMismatch);
    for (size_t i = 0; i < schema_.operations.size(); ++i)
        if (schema_.operations[i].primary == primary) return bindings_[i];
    throw Failure(Error::UnknownOperation);
}
}
