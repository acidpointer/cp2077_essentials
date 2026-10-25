#pragma once
#include <compat/AddressCatalog.hpp>
#include <compat/EarlyModules.hpp>
#include <array>
#include <functional>

namespace compat
{
// Prepared before LoadLibrary, used by the resolver export after notification
// returns. Caller classification selects an ABI, never a plugin-specific rule.
// The caller owns EarlyModules, schemas, native context and all code lifetimes.
// Cached adapter entries must retain their own domain (e.g. CallRouter).
class ResolverRouter
{
public:
    using NativeResolver = uintptr_t (*)(void*, uint32_t);
    struct Result { uintptr_t address{}; Error error{}; uint32_t domain{}; };
    struct Diagnostic { uintptr_t caller{}; uint32_t primary{}, domain{}; Error error{}; };
    ResolverRouter(const EarlyModules& modules, NativeResolver native, void* context);
    // Preparation runs in ordinary resolver context, never in a notification.
    // It can retain the defining module before returning a publishing adapter.
    // It must allow concurrent callers and must not wait for another loader.
    using Prepare = std::function<void(const EarlyModules::Match&,uintptr_t)>;
    void Add(std::shared_ptr<const AddressCatalog> catalog, Prepare prepare = {});
    void Seal();
    Result Resolve(uint32_t primary, uintptr_t caller) const;
    // Bounded, append-only diagnostics. Read/format/flush outside loader/VEH
    // context. Overflow is explicit; records are never overwritten under readers.
    bool ReadDiagnostic(size_t index, Diagnostic& output) const noexcept;
    bool diagnostics_overflowed() const noexcept { return overflow_.load(std::memory_order_acquire); }
    static constexpr size_t DiagnosticCapacity=64;
private:
    Result Reject(uint32_t primary, uintptr_t caller, uint32_t domain, Error error) const noexcept;
    struct Cell { std::atomic<bool> ready{}; Diagnostic value; };
    const EarlyModules& modules_;
    NativeResolver native_;
    void* context_;
    struct Binding { std::shared_ptr<const AddressCatalog> catalog; Prepare prepare; };
    std::vector<Binding> catalogs_;
    bool sealed_{};
    mutable std::array<Cell,DiagnosticCapacity> diagnostics_;
    mutable std::atomic<size_t> next_{};
    mutable std::atomic<bool> overflow_{};
};
}
