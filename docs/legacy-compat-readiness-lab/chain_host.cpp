// A single-threaded fake-engine lifecycle. Explicit barriers are assumptions
// under test, not evidence about when the actual game observes metadata.
#define RED4EXT_STATIC_LIB
#include <RED4ext/Scripting/Functions.hpp>
#include <RED4ext/Scripting/Stack.hpp>
#include <RED4ext/Callback.hpp>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>

static void Require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(42); }
}
struct Counts { int live, copies, calls; };
static Counts counts{};
using OldCallback = RED4ext::Callback<void (*)(), 32>;
using NewCallback = RED4ext::Callback<void (*)(), 64>;
static std::unique_ptr<OldCallback> retained;
static std::unique_ptr<NewCallback> native_callback;
struct Array { void** data; uint32_t capacity, size; };
alignas(16) static unsigned char guest_class[0x2D0];
alignas(16) static unsigned char native_class[0x2D0];
static void* guest_entries[4];
static void* native_entries[4];
static void* guest_function;
alignas(16) static unsigned char native_function[0x100];
using Script = void (*)(void*, void*, int*, int64_t);
static Script guest_script;
static Script engine_script;
static unsigned published_callbacks;
static unsigned publication_barriers;
static unsigned destroyed_frames;
static bool inject_failure;
static unsigned nested_invocations;

static void Publish()
{
    auto& from = *reinterpret_cast<Array*>(guest_class + 0x58);
    Require(from.size <= 4, "bounded class array");
    // Validate first, publish last; this test has one known function identity.
    for (uint32_t i = 0; i < from.size; ++i)
        Require(from.data[i] == guest_function, "unknown guest pointer rejected before publication");
    // Only schema-approved fields; never overwrite the current-only tail.
    std::memcpy(native_function + 8, static_cast<char*>(guest_function) + 8, 16);
    auto& target = *reinterpret_cast<Array*>(native_class + 0x58);
    for (uint32_t i = 0; i < from.size; ++i) target.data[i] = native_function;
    target.size = from.size;
    ++publication_barriers;
}

struct OldFrame
{
    alignas(8) unsigned char prefix[0x68];
    uint64_t canary;
};
struct Binding { RED4ext::CStackFrame* native; OldFrame* guest; Binding* prior; };
static thread_local Binding* bindings;
static void CopyPrefix(void* dst, const void* src)
{
    // This fixture populates shared bytecode, func and parent; the latter two
    // are converted below. A real schema must classify every pointer field.
    // Do not copy old padding at +0x64..+0x67.
    std::memcpy(dst, src, 0x64);
}
static void ToGuest(Binding& binding)
{
    CopyPrefix(binding.guest->prefix, binding.native);
    auto* old = reinterpret_cast<RED4ext::CStackFrame*>(binding.guest->prefix);
    if (binding.native->func)
    {
        Require(binding.native->func == reinterpret_cast<RED4ext::CBaseFunction*>(native_function), "known native function");
        old->func = reinterpret_cast<RED4ext::CBaseFunction*>(guest_function);
    }
    if (binding.native->parent)
    {
        auto* owner = bindings;
        while (owner && owner->native != binding.native->parent) owner = owner->prior;
        Require(owner != nullptr, "native parent belongs to an active binding");
        old->parent = reinterpret_cast<RED4ext::CStackFrame*>(owner->guest->prefix);
    }
}
static void ToNative(Binding& binding)
{
    CopyPrefix(binding.native, binding.guest->prefix);
    auto* native = binding.native;
    if (native->func)
    {
        Require(native->func == reinterpret_cast<RED4ext::CBaseFunction*>(guest_function), "known guest function");
        native->func = reinterpret_cast<RED4ext::CBaseFunction*>(native_function);
    }
    if (native->parent)
    {
        auto* owner = bindings;
        while (owner && owner->guest->prefix != reinterpret_cast<void*>(native->parent)) owner = owner->prior;
        Require(owner != nullptr, "guest parent belongs to an active binding");
        native->parent = owner->native;
    }
}
static void NestedInvoke();
static void Opcode(uint8_t opcode, void* old, void* out)
{
    Require(bindings && old == bindings->guest->prefix, "canonical active frame binding");
    auto& native = *bindings->native;
    auto tail = native.unk98;
    ToNative(*bindings);
    Require(native.unk98 == tail, "native-owned tail survives copy-in");
    if (!inject_failure && opcode == 1 && !bindings->prior) NestedInvoke();
    if (inject_failure && opcode == 2) throw std::runtime_error("modeled opcode failure");
    *static_cast<int*>(out) = opcode == 1 ? 19 : 23;
    ToGuest(*bindings);
}
struct FrameScope
{
    Binding binding;
    explicit FrameScope(RED4ext::CStackFrame* native, OldFrame* guest) : binding{native, guest, bindings}
    { bindings = &binding; ToGuest(binding); }
    ~FrameScope()
    { ToNative(binding); bindings = binding.prior; }
};
static void ScriptAdapter(void* context, void* frame, int* result, int64_t extra)
{
    auto* native = static_cast<RED4ext::CStackFrame*>(frame);
    OldFrame old{};
    old.canary = 0x123456789ABCDEF0;
    FrameScope scope(native, &old);
    guest_script(context, old.prefix, result, extra);
    Require(old.canary == 0x123456789ABCDEF0, "old frame bounded storage");
}

static void FakeAlloc(void*, RED4ext::Memory::AllocationResult* result, uint64_t size)
{
    Require(size == 0xC0, "actual old function allocation");
    guest_function = std::calloc(1, static_cast<size_t>(size));
    Require(guest_function != nullptr, "guest allocation");
    *result = {guest_function, static_cast<size_t>(size)};
}
static void* FakeCtor(void* object, void* parent, uint64_t full, uint64_t short_name, Script callback, uint32_t)
{
    Require(object == guest_function && parent == guest_class, "typed constructor context");
    guest_script = callback;
    engine_script = ScriptAdapter; // Native constructor publishes a prepared adapter immediately.
    ++published_callbacks;
    Require(reinterpret_cast<Array*>(native_class + 0x58)->size == 0, "callback publication precedes class publication");
    std::memcpy(static_cast<char*>(object) + 8, &full, 8);
    std::memcpy(static_cast<char*>(object) + 16, &short_name, 8);
    uint32_t flags = 3, index = 0xFF;
    std::memcpy(static_cast<char*>(object) + 0xA8, &flags, 4);
    std::memcpy(static_cast<char*>(object) + 0xB0, &parent, 8);
    std::memcpy(static_cast<char*>(object) + 0xB8, &index, 4);
    std::memcpy(native_function + 8, &full, 8);
    std::memcpy(native_function + 16, &short_name, 8);
    std::memcpy(native_function + 0xA8, &flags, 4);
    void* native_parent = native_class;
    std::memcpy(native_function + 0xF0, &native_parent, 8);
    std::memcpy(native_function + 0xF8, &index, 4);
    return native_function;
}
static void Unexpected() { Require(false, "unexpected SDK operation"); }
using OpcodeEntry = void (*)(void*, void*, void*, void*);
template<unsigned Index> static void OpcodeAdapter(void*, void* frame, void* out, void*)
{ Opcode(Index, frame, out); }
static OpcodeEntry guest_opcodes[256];
static uintptr_t Resolve(uint32_t hash)
{
    if (hash == RED4ext::Detail::AddressHashes::Memory_Vault_Alloc) return reinterpret_cast<uintptr_t>(&FakeAlloc);
    if (hash == RED4ext::Detail::AddressHashes::CClassStaticFunction_ctor) return reinterpret_cast<uintptr_t>(&FakeCtor);
    if (hash == RED4ext::Detail::AddressHashes::OpcodeHandlers) return reinterpret_cast<uintptr_t>(guest_opcodes);
    return reinterpret_cast<uintptr_t>(&Unexpected);
}
static void AcceptCallback(OldCallback source)
{
    retained = std::make_unique<OldCallback>();
    retained->handler = source.handler;
    source.handler->copy(retained->buffer, source.buffer);
    native_callback = std::make_unique<NewCallback>([] { (*retained)(); });
}

// Model native frame ownership without invoking any game function. A real
// implementation must call the native constructor/destructor contract.
struct NativeFrame
{
    alignas(8) unsigned char bytes[0xA8]{};
    NativeFrame(char* code)
    {
        auto* p = reinterpret_cast<RED4ext::CStackFrame*>(bytes);
        p->code = code; p->isRoot = !bindings;
        p->parent = bindings ? bindings->native : nullptr;
        p->unk98 = bindings ? bindings->native->unk98 : p->unk68;
        p->func = reinterpret_cast<RED4ext::CBaseFunction*>(native_function);
    }
    ~NativeFrame()
    {
        auto* p = reinterpret_cast<RED4ext::CStackFrame*>(bytes);
        Require(p->parent ? (!p->isRoot && p->unk98 == p->parent->unk98) : (p->isRoot && p->unk98 == p->unk68),
                "native frame ownership remains intact");
        ++destroyed_frames;
    }
};
static void NestedInvoke()
{
    auto* parent = bindings;
    char code[] = {1, 2, 0};
    NativeFrame child(code);
    int result = 0;
    engine_script(nullptr, child.bytes, &result, 0);
    Require(result == 42 && bindings == parent, "nested native/guest frame binding restores parent");
    ++nested_invocations;
}
int main()
{
    static_assert(sizeof(RED4ext::CClass) == sizeof(guest_class));
    *reinterpret_cast<Array*>(guest_class + 0x58) = {guest_entries, 4, 0};
    *reinterpret_cast<Array*>(native_class + 0x58) = {native_entries, 4, 0};
    guest_opcodes[1] = OpcodeAdapter<1>;
    guest_opcodes[2] = OpcodeAdapter<2>;
    auto module = LoadLibraryW(L"chain_guest.dll");
    Require(module != nullptr, "load old-header guest");
    auto configure = reinterpret_cast<void (*)(decltype(&Resolve), void*, Counts*, decltype(&Publish))>(GetProcAddress(module, "Configure"));
    auto install = reinterpret_cast<void (*)(void*)>(GetProcAddress(module, "Install"));
    auto lookup = reinterpret_cast<void* (*)()>(GetProcAddress(module, "Lookup"));
    auto change = reinterpret_cast<void (*)()>(GetProcAddress(module, "ChangeName"));
    Require(configure && install && lookup && change, "guest exports");
    configure(Resolve, guest_class, &counts, Publish);
    install(reinterpret_cast<void*>(&AcceptCallback));
    Require(counts.live == 1, "callback owns one retained payload after guest returns");
    (*native_callback)();
    Require(counts.calls == 1 && published_callbacks == 1 && publication_barriers == 1, "registration stages");
    Require(lookup() == guest_function && native_entries[0] == native_function && guest_entries[0] == guest_function,
            "separate canonical identities in guest and native class arrays");
    int result = 0;
    char code[] = {1, 2, 0};
    {
        NativeFrame frame(code);
        engine_script(nullptr, frame.bytes, &result, 0);
        auto* p = reinterpret_cast<RED4ext::CStackFrame*>(frame.bytes);
        Require(result == 42 && p->currentParam == 2 && p->code == code + 3, "actual old GetParameter propagation");
    }
    inject_failure = true;
    int caught = 0;
    try { NativeFrame frame(code); engine_script(nullptr, frame.bytes, &result, 0); }
    catch (const std::runtime_error&) { caught = 1; }
    Require(caught && !bindings && destroyed_frames == 3 && nested_invocations == 1,
            "unwind through guest and host frame scopes");
    change();
    Require(std::memcmp(native_function + 8, static_cast<char*>(guest_function) + 8, 8) != 0,
            "negative control: retained guest mirror mutation is not automatically synchronized");
    Publish();
    Require(!std::memcmp(native_function + 8, static_cast<char*>(guest_function) + 8, 8), "explicit barrier synchronizes mutation");
    native_callback.reset();
    retained.reset();
    Require(counts.live == 0, "callback destruction before module release");
    // Fake engine explicitly detaches its registry. Real detach is not inferred.
    engine_script = nullptr;
    reinterpret_cast<Array*>(native_class + 0x58)->size = 0;
    std::free(guest_function);
    FreeLibrary(module);
    std::printf("CHAIN PASS callbacks=%d copies=%d live=%d native_frames_destroyed=%u nested=%u barriers=%u\n",
                counts.calls, counts.copies, counts.live, destroyed_frames, nested_invocations, publication_barriers);
    std::puts("MIRROR_NEGATIVE PASS retained mutation stale until explicit barrier; automatic coherence unproved");
}
