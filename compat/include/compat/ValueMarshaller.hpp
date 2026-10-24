#pragma once
#include <compat/Schema.hpp>

namespace compat
{
struct PointerTranslator
{
    void* context;
    Error (*translate)(void*, TypeId, Direction, uintptr_t, uintptr_t&);
};

// Copies only reviewed fields. All validation/conversion finishes in bounded
// scratch storage before destination writes. Unmapped native bytes are preserved.
class ValueMarshaller
{
public:
    ValueMarshaller(const Schema& schema, TypeId id)
    {
        Check(schema.Validate());
        type_ = schema.Find(id);
        if (!type_ || type_->policy != Policy::Value) throw Failure(Error::InvalidSchema);
    }
    const Type& type() const noexcept { return *type_; }
    Error Convert(Direction direction, std::span<const std::byte> source,
                  std::span<std::byte> destination, PointerTranslator translator) const;
private:
    const Type* type_; // Immutable schema storage must outlive the plan.
};
Error ConvertPointers(TypeId type, Direction direction, std::span<const uintptr_t> source,
                      std::span<uintptr_t> destination, PointerTranslator translator);
}
