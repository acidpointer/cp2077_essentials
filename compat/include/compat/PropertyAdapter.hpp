#pragma once
#ifdef _WIN32
#include <compat/FrameAdapters.hpp>
#include <compat/ViewArena.hpp>

namespace compat
{
struct PropertyServices
{
    // Return an already constructed, neutral native CProperty. This explicit
    // service owns allocator choice and C++ lifetime; raw zeroed bytes do not
    // satisfy the contract. Failure must clean its own partial construction.
    std::function<void*()> construct;
    std::function<void(void*)> destroy;
};
class PropertyAdapter
{
public:
    // Called only from the reviewed RTTIProperty pool allocation boundary.
    // Equal-sized allocations from other pools do not establish this type.
    static PropertyAdapter Create(Domain::Ptr domain,ViewArena& arena,
                                  SharedPointers pointers,PropertyServices services);
    PropertyAdapter(PropertyAdapter&&) noexcept = default;
    PropertyAdapter(const PropertyAdapter&) = delete;
    ~PropertyAdapter();
    void* guest() const noexcept;
    void* native() const noexcept;
    // Call BEFORE returning the allocation to old inline construction. It can
    // immediately escape through a class property array without another hook.
    void Publish() noexcept;
private:
    struct State;
    explicit PropertyAdapter(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};
}
#endif
