#include <compat/ValueMarshaller.hpp>
#include <array>
#include <cstring>

namespace compat
{
Error ValueMarshaller::Convert(Direction direction, std::span<const std::byte> source,
                               std::span<std::byte> destination, PointerTranslator translator) const
{
    const auto& type = *type_;
    if (type.policy != Policy::Value) return Error::InvalidSchema;
    const auto input_size = direction == Direction::ToNative ? type.guest_size : type.native_size;
    const auto output_size = direction == Direction::ToNative ? type.native_size : type.guest_size;
    if (source.size() != input_size || destination.size() != output_size || output_size > 1024)
        return Error::InvalidRange;
    std::array<std::byte, 1024> scratch{};
    std::memcpy(scratch.data(), destination.data(), output_size);
    for (const auto& field : type.fields)
    {
        if (field.kind == FieldKind::Padding) continue;
        const auto from = direction == Direction::ToNative ? field.guest_offset : field.native_offset;
        const auto to = direction == Direction::ToNative ? field.native_offset : field.guest_offset;
        if (from > input_size || field.width > input_size - from || to > output_size || field.width > output_size - to)
            return Error::InvalidSchema;
        if (field.kind == FieldKind::Zero)
        {
            for (size_t i = 0; i < field.width; ++i)
                if (source[from + i] != std::byte{}) return Error::UnsupportedValue;
        }
        if (field.kind == FieldKind::Pointer)
        {
            if (field.width != sizeof(uintptr_t)) return Error::InvalidSchema;
            uintptr_t value{}, mapped{};
            std::memcpy(&value, source.data() + from, sizeof(value));
            if (!value && !field.nullable) return Error::UnknownPointer;
            if (value)
            {
                if (!translator.translate) return Error::UnknownPointer;
                auto result = translator.translate(translator.context, field.pointee, direction, value, mapped);
                if (result != Error::None) return result;
            }
            std::memcpy(scratch.data() + to, &mapped, sizeof(mapped));
        }
        else std::memcpy(scratch.data() + to, source.data() + from, field.width);
    }
    // Avoid clobbering native-only bytes even when the destination is shared.
    // This is not a transaction across threads: caller must own/synchronize it.
    for (const auto& field : type.fields)
    {
        if (field.kind == FieldKind::Padding) continue;
        const auto to = direction == Direction::ToNative ? field.native_offset : field.guest_offset;
        std::memcpy(destination.data() + to, scratch.data() + to, field.width);
    }
    return Error::None;
}

Error ConvertPointers(TypeId type, Direction direction, std::span<const uintptr_t> source,
                      std::span<uintptr_t> destination, PointerTranslator translator)
{
    if (!type || source.size() != destination.size() || source.size() > 256) return Error::InvalidRange;
    std::array<uintptr_t, 256> scratch{};
    for (size_t i = 0; i < source.size(); ++i)
    {
        if (!source[i]) continue;
        if (!translator.translate) return Error::UnknownPointer;
        auto result = translator.translate(translator.context, type, direction, source[i], scratch[i]);
        if (result != Error::None) return result;
    }
    if (!source.empty()) std::memcpy(destination.data(), scratch.data(), source.size_bytes());
    return Error::None;
}
}
