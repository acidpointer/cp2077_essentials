// Exercises the actual SDK container against the retail allocator contract.
// This is an isolated unit executable; it never loads game or mod DLLs.
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>
#include <type_traits>

#define RED4EXT_STATIC_LIB
#include <RED4ext/Containers/DynArray.hpp>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "%s\n", message);
        std::exit(42);
    }
}

struct Tracked
{
    inline static std::set<const Tracked*> live;
    inline static int constructions = 0;
    inline static int destructions = 0;
    inline static int assignments = 0;
    std::unique_ptr<int> resource;

    Tracked() : resource(std::make_unique<int>(++constructions))
    {
        Require(live.insert(this).second, "construction over a live object");
    }
    Tracked(Tracked&& other) noexcept : resource(std::move(other.resource))
    {
        Require(live.contains(&other), "moving from unconstructed storage");
        Require(live.insert(this).second, "move construction over a live object");
        ++constructions;
    }
    Tracked& operator=(Tracked&& other) noexcept
    {
        Require(live.contains(this), "BUG: move assignment into unconstructed storage (even when zero-filled)");
        Require(live.contains(&other), "move assignment from unconstructed storage");
        resource = std::move(other.resource);
        ++assignments;
        return *this;
    }
    ~Tracked()
    {
        Require(live.erase(this) == 1, "double destruction or destruction of unconstructed storage");
        ++destructions;
    }
};

struct Allocator final : RED4ext::Memory::IAllocator
{
    RED4ext::Memory::AllocationResult Alloc(uint64_t size) const override
    {
        auto* memory = std::calloc(1, static_cast<size_t>(size));
        Require(memory != nullptr, "allocation failed");
        return {memory, static_cast<size_t>(size)};
    }
    RED4ext::Memory::AllocationResult AllocAligned(uint64_t size, uint32_t) const override { return Alloc(size); }
    RED4ext::Memory::AllocationResult Realloc(RED4ext::Memory::AllocationResult&, uint64_t) const override
    {
        std::abort();
    }
    RED4ext::Memory::AllocationResult ReallocAligned(RED4ext::Memory::AllocationResult&, uint64_t, uint32_t) const override
    {
        std::abort();
    }
    void Free(RED4ext::Memory::AllocationResult& allocation) const override { std::free(allocation.memory); }
    void sub_28(void*) const override { std::abort(); }
    // Match the SDK's const scalar return type exactly.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-qualifiers"
    const uint32_t GetHandle() const override { return 0; }
#pragma clang diagnostic pop
};

using Array = RED4ext::DynArray<Tracked>;
using Mover = void (*)(Tracked*, Tracked*, uint32_t, Array*);
// Game-owned layout; keep the fake boundary independent of private SDK members.
struct Layout { Tracked* entries; uint32_t capacity; uint32_t size; };
static_assert(sizeof(Array) == sizeof(Layout));

void ResizeBuffer(Array* array, uint32_t capacity, uint32_t elementSize, uint32_t alignment, Mover move)
{
    Require(elementSize == sizeof(Tracked) && alignment == 8, "unexpected relocation ABI");
    Layout old{};
    std::memcpy(&old, array, sizeof(old));
    uintptr_t allocatorVtable = reinterpret_cast<uintptr_t>(old.entries);
    const auto oldBytes = static_cast<size_t>(old.capacity) * elementSize;
    if (old.capacity)
        std::memcpy(&allocatorVtable, reinterpret_cast<const char*>(old.entries) + RED4ext::AlignUp(oldBytes, size_t{8}), 8);
    const auto bytes = RED4ext::AlignUp(static_cast<size_t>(capacity) * elementSize, size_t{8});
    auto* entries = static_cast<Tracked*>(std::calloc(1, bytes + 8));
    Require(entries != nullptr, "allocation failed");
    // Retail ResizeBuffer zeroes allocation, then invokes the move callback.
    if (old.capacity)
    {
        Require(move != nullptr, "nontrivial elements require relocation");
        move(entries, old.entries, static_cast<uint32_t>(oldBytes), array);
        std::free(old.entries);
    }
    std::memcpy(reinterpret_cast<char*>(entries) + bytes, &allocatorVtable, 8);
    const Layout replacement{entries, capacity, old.size};
    std::memcpy(static_cast<void*>(array), &replacement, sizeof(replacement));
}
} // namespace

uintptr_t RED4ext::UniversalRelocBase::Resolve(uint32_t hash)
{
    Require(hash == RED4ext::Detail::AddressHashes::DynArray_Realloc, "unexpected game address request");
    return reinterpret_cast<uintptr_t>(&ResizeBuffer);
}

int main()
{
    Allocator allocator;
    {
        Array values(&allocator);
        values.Reserve(16);
        values.Resize(3);
        const int a = *values[0].resource, b = *values[1].resource, c = *values[2].resource;
        values.Reserve(128); // capacity exceeds size: relocate only the live elements.
        Require(Tracked::live.size() == 3, "growth changed the live-object count");
        Require(*values[0].resource == a && *values[1].resource == b && *values[2].resource == c, "growth lost ownership/value");
        values.Resize(5);
        values.Reserve(256);
        Require(Tracked::live.size() == 5, "second growth changed the live-object count");
        values.Resize(2);
        values.ShrinkToFit();
        Require(Tracked::live.size() == 2, "shrink changed the live-object count");
        values.Clear();
        values.Reserve(512); // allocated but empty source: no objects to move.
        Require(Tracked::live.empty(), "clear retained live objects");
    }
    Require(Tracked::live.empty(), "array destruction leaked objects");
    Require(Tracked::constructions == Tracked::destructions, "unbalanced construction/destruction");
    Require(Tracked::assignments == 0, "relocation used assignment instead of construction");
    std::puts("PASS: zero-filled growth, spare capacity, repeated growth, shrink, empty relocation, ownership and exact destruction");
}
