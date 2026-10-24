#include <compat/BorrowedValue.hpp>

namespace compat
{
thread_local BorrowedValue* BorrowedValue::current_ = nullptr;

BorrowedValue::BorrowedValue(uint32_t domain, const ValueMarshaller& plan,
                           std::span<std::byte> native, PointerTranslator pointers)
    : domain_(domain), plan_(plan), native_(native), pointers_(pointers), prior_(current_)
{
    if (!domain || native.size() != plan.type().native_size) throw Failure(Error::InvalidRange);
    current_ = this;
    try { Check(ToGuest()); }
    catch (...) { current_ = prior_; throw; }
}

Error BorrowedValue::Translate(void* context, TypeId type, Direction direction,
                               uintptr_t source, uintptr_t& target)
{
    auto& self = *static_cast<BorrowedValue*>(context);
    if (type == self.plan_.type().id)
    {
        for (auto* value = current_; value; value = value->prior_)
        {
            if (value->domain_ != self.domain_ || value->plan_.type().id != type) continue;
            auto* from = direction == Direction::ToNative ? value->guest() : value->native();
            auto* to = direction == Direction::ToNative ? value->native() : value->guest();
            if (reinterpret_cast<uintptr_t>(from) == source)
            { target = reinterpret_cast<uintptr_t>(to); return Error::None; }
        }
        return Error::UnknownPointer;
    }
    if (!self.pointers_.translate) return Error::UnknownPointer;
    return self.pointers_.translate(self.pointers_.context, type, direction, source, target);
}

Error BorrowedValue::ToGuest()
{
    return plan_.Convert(Direction::ToGuest, native_, {guest_.data(), plan_.type().guest_size}, {this, Translate});
}
Error BorrowedValue::ToNative()
{
    return plan_.Convert(Direction::ToNative, {guest_.data(), plan_.type().guest_size}, native_, {this, Translate});
}

BorrowedValue* BorrowedValue::Find(uint32_t domain, TypeId type, const void* guest) noexcept
{
    for (auto* value = current_; value; value = value->prior_)
        if (value->domain_ == domain && value->plan_.type().id == type && value->guest() == guest) return value;
    return nullptr;
}
}
