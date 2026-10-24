#include <compat/Schema.hpp>
#include <array>
#include <algorithm>

namespace compat
{
const Type* Schema::Find(TypeId id) const noexcept
{
    for (const auto& type : types) if (type.id == id) return &type;
    return nullptr;
}

Error Schema::Validate() const noexcept
{
    if (source.empty() || target.empty() || types.empty()) return Error::InvalidSchema;
    for (size_t i = 0; i < types.size(); ++i)
    {
        const auto& type = types[i];
        if (type.policy != Policy::Opaque && type.policy != Policy::Value && type.policy != Policy::LiveView)
            return Error::InvalidSchema;
        if (!type.id || type.name.empty() || !type.alignment || type.alignment > 16 ||
            (type.alignment & (type.alignment - 1))) return Error::InvalidSchema;
        for (size_t j = 0; j < i; ++j)
            if (types[j].id == type.id || types[j].name == type.name) return Error::InvalidSchema;
        if (type.policy == Policy::Opaque)
        {
            if (!type.fields.empty()) return Error::InvalidSchema;
            continue;
        }
        if (!type.guest_size || !type.native_size || type.guest_size > 1024 || type.native_size > 1024 ||
            type.guest_size % type.alignment || type.native_size % type.alignment)
            return Error::InvalidSchema;
        std::array<bool, 1024> guest{}, native{};
        for (size_t index = 0; index < type.fields.size(); ++index)
        {
            const auto& field = type.fields[index];
            if (field.kind != FieldKind::Scalar && field.kind != FieldKind::Pointer &&
                field.kind != FieldKind::Zero && field.kind != FieldKind::Padding) return Error::InvalidSchema;
            if (field.name.empty() || !field.width || field.guest_offset > type.guest_size ||
                field.width > type.guest_size - field.guest_offset) return Error::InvalidSchema;
            for (size_t earlier = 0; earlier < index; ++earlier)
                if (type.fields[earlier].name == field.name) return Error::InvalidSchema;
            if (field.kind != FieldKind::Padding && (field.native_offset > type.native_size ||
                field.width > type.native_size - field.native_offset)) return Error::InvalidSchema;
            if (field.kind == FieldKind::Pointer)
            { if (field.width != 8 || !Find(field.pointee)) return Error::InvalidSchema; }
            else if (field.pointee || field.nullable) return Error::InvalidSchema;
            if (field.empty_pointee)
            {
                const auto* empty = Find(field.empty_pointee);
                if (type.policy != Policy::LiveView || field.kind != FieldKind::Pointer ||
                    !empty || empty->policy != Policy::Opaque || field.capacity_native != field.native_offset+8 ||
                    field.capacity_native > type.native_size || 8 > type.native_size - field.capacity_native) return Error::InvalidSchema;
                bool capacityField = false;
                for (const auto& count : type.fields)
                    if (count.kind == FieldKind::Scalar && count.native_offset == field.capacity_native &&
                        count.guest_offset == field.guest_offset+8 && count.width == 8)
                        capacityField = true;
                if (!capacityField) return Error::InvalidSchema;
            }
            else if (field.capacity_native) return Error::InvalidSchema;
            for (size_t byte = 0; byte < field.width; ++byte)
            {
                if (guest[field.guest_offset + byte]) return Error::InvalidSchema;
                guest[field.guest_offset + byte] = true;
                if (field.kind == FieldKind::Padding) continue;
                if (native[field.native_offset + byte]) return Error::InvalidSchema;
                native[field.native_offset + byte] = true;
            }
        }
        if (type.policy == Policy::Value &&
            !std::all_of(guest.begin(), guest.begin() + type.guest_size, [](bool byte) { return byte; }))
            return Error::InvalidSchema;
    }
    for (size_t i = 0; i < operations.size(); ++i)
    {
        const auto& op = operations[i];
        if (op.kind != AddressKind::Function && op.kind != AddressKind::Table) return Error::InvalidSchema;
        if (op.id.empty() || !op.primary || op.secondary.empty() || op.secondary.size() > 64 ||
            op.secondary.find_first_not_of("0123456789abcdef") != std::string_view::npos || op.signature.empty() ||
            (op.kind == AddressKind::Table ? !op.table_size : op.table_size != 0)) return Error::InvalidSchema;
        for (size_t j = 0; j < i; ++j)
            if (op.id == operations[j].id || op.primary == operations[j].primary) return Error::InvalidSchema;
    }
    return Error::None;
}
}
