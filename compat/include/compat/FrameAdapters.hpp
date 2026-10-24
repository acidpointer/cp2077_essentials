#pragma once
#include <compat/BorrowedValue.hpp>
#include <compat/CallRouter.hpp>
#include <vector>

namespace compat
{
// Explicit lifetime anchor for translator context, view backing and engine
// services used by callbacks. No adapter may capture an unowned stack context.
struct SharedPointers
{
    PointerTranslator translator;
    std::shared_ptr<void> lifetime;
};
// Initial operation contract: synchronous scalar results, never owning handles
// or pointers. Context is a registered Instance identity/view; aux must be null.
struct ScalarResult
{
    size_t size{4}, alignment{4};
    bool Valid() const noexcept;
    void CheckOutput(const void* output) const;
};
using ScriptEntry = void (*)(void*, void*, void*, int64_t);
using OpcodeEntry = void (*)(void*, void*, void*, void*);
struct ScriptAdapterTag;
using ScriptRoute = CallRouter<void(void*, void*, void*, int64_t), ScriptAdapterTag, 256>;
ScriptRoute::Registration BindScript(Domain::Ptr domain, ScriptEntry legacy,
                                     SharedPointers pointers, ScalarResult result = {});

class OpcodeTable
{
public:
    struct Operation
    {
        uint8_t opcode;
        OpcodeEntry native;
        ScalarResult result;
    };
    OpcodeTable(Domain::Ptr domain, SharedPointers pointers, std::span<const Operation> operations);
    ~OpcodeTable();
    OpcodeTable(const OpcodeTable&) = delete;
    OpcodeTable& operator=(const OpcodeTable&) = delete;
    const OpcodeEntry* data() const noexcept { return entries_->data(); }
    void Publish() noexcept;
    Error Retire() noexcept;
private:
    struct OpcodeAdapterTag;
    using Route = CallRouter<void(void*, void*, void*, void*), OpcodeAdapterTag, 2048>;
    std::array<OpcodeEntry,256>* entries_{}; // Bounded, never-reused table bank; safe stale table loads.
    std::vector<Route::Registration> routes_;
    bool published_{};
};
}
