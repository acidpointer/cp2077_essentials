#pragma once
#include <compat/ValueMarshaller.hpp>
#include <array>
#include <utility>

namespace compat
{
// Synchronous nonescaping binding. Owns only guest scratch storage, never the
// native object/tail. TLS is a stack of bindings, not callback domain identity.
class BorrowedValue
{
public:
    BorrowedValue(uint32_t domain, const ValueMarshaller& plan,
                  std::span<std::byte> native, PointerTranslator pointers);
    ~BorrowedValue() { current_ = prior_; }
    BorrowedValue(const BorrowedValue&) = delete;
    BorrowedValue& operator=(const BorrowedValue&) = delete;
    void* guest() noexcept { return guest_.data(); }
    void* native() noexcept { return native_.data(); }
    Error ToGuest();
    Error ToNative();
    static BorrowedValue* Find(uint32_t domain, TypeId type, const void* guest) noexcept;
    template<class F> void CallGuest(F&& operation)
    {
        try { std::forward<F>(operation)(guest()); }
        catch (...) { Check(ToNative()); throw; }
        Check(ToNative());
    }
    template<class F> void CallNative(F&& operation)
    {
        Check(ToNative());
        try { std::forward<F>(operation)(native()); }
        catch (...) { Check(ToGuest()); throw; }
        Check(ToGuest());
    }
private:
    static Error Translate(void*, TypeId, Direction, uintptr_t, uintptr_t&);
    uint32_t domain_;
    const ValueMarshaller& plan_;
    std::span<std::byte> native_;
    PointerTranslator pointers_;
    alignas(16) std::array<std::byte, 1024> guest_{};
    BorrowedValue* prior_;
    static thread_local BorrowedValue* current_;
};
}
