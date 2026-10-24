#pragma once
#ifdef _WIN32
#include <compat/FrameAdapters.hpp>
#include <compat/ViewArena.hpp>

namespace compat
{
struct ArrayServices
{
    std::function<void*(size_t,size_t)> allocate;
    std::function<void(void*)> free;
    uintptr_t nativeAllocatorVtable{};
    uint32_t allocatorHandle{};
};
// Reviewed DynArray pointer-element buffers, identified by an owning schema
// field, never by allocation size. All mutation requires exclusive guest/native
// ownership. Services must preserve backing and must not throw from free.
class ArrayAdapter
{
public:
    using Mover = void (*)(void*,void*,uint32_t,void*);
    ArrayAdapter(Domain::Ptr domain, ViewArena& arena, SharedPointers pointers, ArrayServices services);
    ~ArrayAdapter();
    ArrayAdapter(const ArrayAdapter&) = delete;
    ArrayAdapter& operator=(const ArrayAdapter&) = delete;
    void Bind(void* guestOwner, TypeId ownerType, std::string_view field);
    void Reallocate(void* guestArray, uint32_t capacity, uint32_t elementSize, uint32_t alignment, Mover mover);
    // Only after owners stop using the allocator identity and every buffer has
    // been freed. Busy retains backing/services rather than freeing live arrays.
    Error Retire() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
#endif
