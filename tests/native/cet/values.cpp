#include "common/CallAllocator.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <memory>
#include <new>

static thread_local TiltedPhoques::Allocator* current = nullptr;
void TiltedPhoques::Allocator::Set(Allocator* value) noexcept { current = value; }
TiltedPhoques::Allocator* TiltedPhoques::Allocator::Get() noexcept { return current; }
struct Payload { std::string Text; };
struct NativeAllocator
{
    int Allocations = 0, Releases = 0;
    struct Result {void* memory;};
    Result AllocAligned(size_t size,size_t) { ++Allocations; return {::operator new(size)}; }
    void Free(void* memory) { ++Releases; ::operator delete(memory); }
};
struct Type
{
    NativeAllocator Allocator;
    int Constructed = 0, Destroyed = 0;
    bool FailAssign = false;
    NativeAllocator* GetAllocator() { return &Allocator; }
    size_t GetSize() { return sizeof(Payload); }
    size_t GetAlignment() { return alignof(Payload); }
    void Construct(void* data) { new(data) Payload; ++Constructed; }
    void Destruct(void* data) { static_cast<Payload*>(data)->~Payload(); ++Destroyed; }
    void Assign(void* out,const void* in)
    {
        if (FailAssign) throw std::runtime_error("copy failed");
        *static_cast<Payload*>(out) = *static_cast<const Payload*>(in);
    }
};
namespace RED4ext
{
namespace rtti { using IType = Type; }
struct CStackType { Type* type = nullptr; void* value = nullptr; CStackType() = default; CStackType(Type* t,void* v) : type(t),value(v) {} };
}
#include "reverse/NativeValue.h"
int main()
{
    CallAllocator outer;
    auto* retained = static_cast<char*>(outer.Allocate(32768));
    assert(retained); std::memset(retained,0x5a,32768);
    {
        CallAllocator inner;
        auto* data = static_cast<char*>(inner.Allocate(65536));
        assert(data); std::memset(data,0xa5,65536);
    }
    for (int i = 0; i < 32768; ++i) assert(retained[i] == 0x5a);
    assert(!outer.Allocate(64 * 1024 * 1024));
    assert(outer.Allocate(32));
    TiltedPhoques::Allocator::Set(&outer);
    try { CallAllocator temporary; ScopedAllocator scoped(&temporary); throw std::runtime_error("fixture"); }
    catch (const std::runtime_error&) {}
    assert(TiltedPhoques::Allocator::Get() == &outer);
    Type type;
    Payload source{"original"};
    {
        NativeValue snapshot({&type,&source});
        assert(snapshot.value != &source);
        source.Text = "changed";
        assert(static_cast<Payload*>(snapshot.value)->Text == "original");
        { auto second = snapshot; assert(type.Destroyed == 0); }
        assert(type.Destroyed == 0);
    }
    assert(type.Constructed == 1 && type.Destroyed == 1 && type.Allocator.Releases == 1);
    type.FailAssign = true;
    try { NativeValue failed({&type,&source}); assert(false); } catch (const std::runtime_error&) {}
    assert(type.Constructed == 2 && type.Destroyed == 2 && type.Allocator.Releases == 2);
    // A default argument's engine storage remains owned across out-argument
    // publication and is reclaimed if later argument conversion throws.
    try
    {
        NativeValue argument(&type);
        auto out = argument;
        static_cast<Payload*>(out.value)->Text = "retained out value";
        assert(type.Destroyed == 2);
        throw std::runtime_error("next argument failed");
    }
    catch (const std::runtime_error&) {}
    assert(type.Constructed == 3 && type.Destroyed == 3 && type.Allocator.Releases == 3);
    std::cout << "CET reentrant arenas, allocator restoration and native snapshots passed\n";
}
