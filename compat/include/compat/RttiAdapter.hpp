#pragma once
#ifdef _WIN32
#include <compat/FrameAdapters.hpp>
#include <compat/ViewArena.hpp>
#include <functional>

namespace compat
{
struct RttiObject
{
    void* native{};
    TypeId type{}; // Explicit Class or RttiMetadata; never inferred from size.
};
struct TypeMetadata
{
    uint64_t name{}, computedName{};
    uint32_t size{}, alignment{};
    uint8_t kind{};
};
struct RttiServices
{
    std::function<RttiObject(uint64_t)> getType, getClass, getScriptClass;
    std::function<RttiObject(uint32_t)> getAsyncType;
    std::function<TypeMetadata(void*)> metadata;
    std::function<void(uint64_t,uint64_t)> registerScriptName;
    std::function<uint64_t(uint64_t,bool)> convertName;
    // Registration accepts only previously established guest/native type
    // identities. The service may publish before returning or throwing.
    // Native ownership and any reverse virtual dispatch must already be bound.
    std::function<void(void*,uint32_t)> registerType;
    // Sink must construct the native 64-byte SDK callback from this closure.
    // It may store or invoke it before returning (or throwing).
    std::function<void(std::function<void()>,bool)> addCallback;
};

// The old virtual table is compiler-built with every historical signature,
// including the by-value 40-byte callback and old destructor position. Only
// the explicit services above are admitted; unknown slots fail closed.
class RttiAdapter
{
public:
    RttiAdapter(Domain::Ptr domain, ViewArena& arena, SharedPointers pointers,
                RttiServices services, void* nativeSystem);
    ~RttiAdapter();
    RttiAdapter(const RttiAdapter&) = delete;
    RttiAdapter& operator=(const RttiAdapter&) = delete;
    void* guest() const noexcept;
    void* View(RttiObject object);
    // Callback publication retains the adapter for the process lifetime. No
    // engine removal/drain capability is assumed from a void registration API.
    Error Retire() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
#endif
