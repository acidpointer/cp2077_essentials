#pragma once

#include <cstdint>
#include <exception>

namespace compat
{
enum class Error : uint8_t
{
    None, InvalidSchema, UnknownOperation, IdentityMismatch, WrongKind,
    Capacity, Closed, Retired, Published, InvalidRange, Overlap,
    UnknownPointer, AmbiguousPointer, UnsupportedValue, InvalidCallback,
    UnsupportedInstruction, UnsupportedAccess, Busy, NativeFault
};

class Failure final : public std::exception
{
public:
    explicit Failure(Error error) noexcept : error_(error) {}
    Error code() const noexcept { return error_; }
    const char* what() const noexcept override { return "compatibility contract rejected"; }
private:
    Error error_;
};

inline void Check(Error result)
{
    if (result != Error::None) throw Failure(result);
}
}
