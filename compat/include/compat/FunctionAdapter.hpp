#pragma once
#ifdef _WIN32
#include <compat/FrameAdapters.hpp>
#include <compat/ViewArena.hpp>

namespace compat
{
// Native class/static function operations, with the reviewed Win64 pointer /
// CName(u64) / Flags(u32) ABI. The integration supplies verified engine entries.
struct FunctionServices
{
    std::function<void*(size_t,size_t)> allocate;
    std::function<void(void*)> free_unconstructed;
    std::function<void*(void*,void*,uint64_t,uint64_t,ScriptEntry,uint32_t)> construct;
    // Optional, reviewed host capability. Success must mean no native reference
    // or callback remains and every in-flight native invocation has drained.
    // The retail game has no established detach capability; leave these empty.
    std::function<bool(void*,ScriptEntry)> detach;
    std::function<void(void*)> destroy;
};
class FunctionAdapter
{
public:
    static FunctionAdapter Create(Domain::Ptr domain, ViewArena& arena, SharedPointers pointers,
        FunctionServices services, void* guest, void* guestParent, uint64_t fullName,
        uint64_t shortName, ScriptEntry legacy, uint32_t flags, ScalarResult result = {});
    ~FunctionAdapter();
    FunctionAdapter(FunctionAdapter&&) noexcept = default;
    FunctionAdapter& operator=(FunctionAdapter&&) = delete;
    FunctionAdapter(const FunctionAdapter&) = delete;
    FunctionAdapter& operator=(const FunctionAdapter&) = delete;
    void* guest() const noexcept;
    void* native() const noexcept;
    // A missing/failed detach pins the binding and backing for the session.
    Error Retire() noexcept;
private:
    struct State;
    explicit FunctionAdapter(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};
}
#endif
