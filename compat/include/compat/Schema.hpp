#pragma once
#include <compat/Error.hpp>
#include <cstddef>
#include <span>
#include <string_view>

namespace compat
{
using TypeId = uint32_t;
enum class Policy { Value, LiveView, Opaque };
enum class FieldKind { Scalar, Pointer, Zero, Padding, Vtable };
enum class Direction { ToNative, ToGuest };
enum class AddressKind { Function, Table };

struct Field
{
    std::string_view name;
    size_t guest_offset, native_offset, width;
    FieldKind kind;
    TypeId pointee{};
    bool nullable{};
    // DynArray data is an allocator vtable when capacity is zero. These two
    // fields enable that explicitly reviewed union, only on live pointer fields.
    TypeId empty_pointee{};
    size_t capacity_native{};
};
struct Type
{
    TypeId id;
    std::string_view name;
    Policy policy;
    size_t guest_size, native_size, alignment;
    std::span<const Field> fields;
    TypeId base_type{}; // Reviewed base at offset zero in both ABIs.
};
struct Operation
{
    std::string_view id;
    uint32_t primary;
    std::string_view secondary;
    AddressKind kind;
    std::string_view signature;
    size_t table_size;
};
struct Schema
{
    std::string_view source, target;
    std::span<const Type> types;
    std::span<const Operation> operations;
    Error Validate() const noexcept;
    const Type* Find(TypeId id) const noexcept;
    bool IsA(TypeId actual, TypeId requested) const noexcept;
};
}
