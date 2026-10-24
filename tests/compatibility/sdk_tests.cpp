// Actual historical SDK entry points execute against a fake engine. The host
// uses the runtime library for routing, callbacks, pointers and frame conversion.
#define RED4EXT_STATIC_LIB
#include <RED4ext/Scripting/Functions.hpp>
#include <RED4ext/Scripting/Stack.hpp>
#include <RED4ext/Callback.hpp>
#include <compat/AddressCatalog.hpp>
#include <compat/BorrowedValue.hpp>
#include <compat/CallbackAdapter.hpp>
#include <compat/FunctionAdapter.hpp>
#include <compat/CallRouter.hpp>
#include <compat/ObjectRegistry.hpp>
#include <compat/ExceptionBackend.hpp>
#include <compat/Profile.hpp>
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>

using namespace compat;
static void Require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
struct Counts { int live, copies, calls; };
struct Array { uintptr_t* data; uint32_t capacity, size; };
using OldCallback = RED4ext::Callback<void (*)(), 32>;
using NativeCallback = RED4ext::Callback<void (*)(), 64>;
using Script = ScriptEntry;
using Opcode = void (*)(void*, void*, void*, void*);
struct ResolveTag; struct AllocTag; struct ConstructorTag;
using Resolver = CallRouter<uintptr_t(uint32_t), ResolveTag>;
using Allocator = CallRouter<void(void*, RED4ext::Memory::AllocationResult*, uint64_t), AllocTag>;
using Constructor = CallRouter<void*(void*, void*, uint64_t, uint64_t, Script, uint32_t), ConstructorTag>;
static Counts counts{};
static auto domain = Domain::Create(1, std::string(profile::Core.source), std::string(profile::Core.target));
struct Environment
{
    Domain::Ptr domain;
    ObjectRegistry objects{profile::Core,16};
    ViewArena arena{profile::Core,32};
    ExceptionBackend backend{arena};
    explicit Environment(Domain::Ptr value) : domain(std::move(value)) {}
};
static std::shared_ptr<Environment> environment;
alignas(8) static std::array<std::byte, 0x2D0> native_class{};
static void* guest_class;
alignas(8) static std::array<std::byte, 0x100> native_function{};
static std::array<uintptr_t, 4> native_entries{};
static void* guest_entries;
static void* guest_function;
static Script engine_script;
static std::optional<FunctionAdapter> function_adapter;
static std::unique_ptr<NativeCallback> native_callback;
static unsigned publications{}, frame_destructions{}, nested_calls{}, native_destructions{};
static bool fail_opcode;

template<class T> static void Put(void* memory, size_t offset, T value)
{ std::memcpy(static_cast<std::byte*>(memory) + offset, &value, sizeof(value)); }
template<class T> static T Get(const void* memory, size_t offset)
{ T value; std::memcpy(&value, static_cast<const std::byte*>(memory) + offset, sizeof(value)); return value; }

static Error Translate(void* context, TypeId type, Direction direction, uintptr_t source, uintptr_t& target)
{
    auto& state=*static_cast<Environment*>(context);
    auto error = state.arena.memory().Translate(state.domain->id(), type, direction, source, target);
    if (error != Error::UnknownPointer) return error;
    auto read = state.objects.Read(); return read.Translate(state.domain->id(), type, direction, source, target);
}

struct NativeFrame
{
    alignas(8) std::array<std::byte, 0xA8> bytes{};
    explicit NativeFrame(char* code, void* parent = nullptr)
    {
        Put(bytes.data(), 0, code); Put(bytes.data(), 8, native_function.data());
        Put(bytes.data(), 72, parent);
        bytes[100] = parent ? std::byte{} : std::byte{1};
        Put(bytes.data(), 152, parent ? Get<void*>(parent, 152) : bytes.data() + 104);
    }
    ~NativeFrame()
    {
        const auto parent = Get<void*>(bytes.data(), 72);
        Require(Get<void*>(bytes.data(), 152) == (parent ? Get<void*>(parent, 152) : bytes.data() + 104), "native map owner");
        ++frame_destructions;
    }
};
static char code[] = {1, 2, 0};
static void RunOpcode(unsigned opcode, void* native, void* output)
{
    if (opcode == 1 && !Get<void*>(native, 72) && !fail_opcode)
    {
        NativeFrame child(code, native);
        int result{}; engine_script(nullptr, child.bytes.data(), &result, 0);
        Require(result == 42, "nested callback result"); ++nested_calls;
    }
    if (opcode == 2 && fail_opcode) throw std::runtime_error("engine opcode failure");
    *static_cast<int*>(output) = opcode == 1 ? 19 : 23;
}

static void AcceptCallback(OldCallback source)
{
    static_assert(sizeof(RED4ext::CallbackHandler<void>) == sizeof(CallbackHandler<void()>));
    static_assert(sizeof(OldCallback) == 0x28 && sizeof(NativeCallback) == 0x48);
    native_callback = std::make_unique<NativeCallback>(AdaptRegistrationCallback<NativeCallback>(
        domain,std::as_writable_bytes(std::span(source.buffer)),source.handler));
}

static void Run(const wchar_t* fixture)
{
    Require(sizeof(RED4ext::CClass) == 0x2D0 && sizeof(RED4ext::CStackFrame) == 0xA8 &&
            sizeof(RED4ext::CClassStaticFunction) == 0x100, "current SDK sizes");
    environment=std::make_shared<Environment>(domain);
    auto& arena=environment->arena;auto& backend=environment->backend;
    SharedPointers pointers{{environment.get(),Translate},environment};
    guest_entries = arena.Borrow(domain, profile::FunctionPointer, native_entries.data(), native_entries.size());
    Put(native_class.data(), 0x58, Array{native_entries.data(), 4, 0});
    guest_class = arena.Borrow(domain, profile::Class, native_class.data());
    Check(environment->objects.Add({1, profile::Bytecode, 1, reinterpret_cast<uintptr_t>(code),
                       reinterpret_cast<uintptr_t>(code), sizeof(code), sizeof(code), true, true}));
    const OpcodeTable::Operation operations[]{
        {1,[](void*,void* frame,void* out,void*) { RunOpcode(1,frame,out); },{}},
        {2,[](void*,void* frame,void* out,void*) { RunOpcode(2,frame,out); },{}}};
    OpcodeTable opcodes(domain,pointers,operations);
    FunctionServices services;
    services.allocate=[](size_t size,size_t alignment) -> void* {
        Require(size==sizeof(native_function) && alignment==8,"native allocation shape");
        return native_function.data();
    };
    services.free_unconstructed=[](void*) { throw std::runtime_error("unexpected preparation failure"); };
    services.construct=[](void* object,void* parent,uint64_t full,uint64_t short_name,Script callback,uint32_t) -> void* {
        Require(object==native_function.data() && parent==native_class.data(),"native-only constructor arguments");
        uintptr_t guest{};
        Check(environment->arena.memory().Translate(domain->id(),profile::Function,Direction::ToGuest,reinterpret_cast<uintptr_t>(object),guest));
        Require(guest==reinterpret_cast<uintptr_t>(guest_function),"view prepared before callback publication");
        engine_script=callback;++publications;
        Require(Get<Array>(native_class.data(),0x58).size==0,"constructor publishes before class registration");
        Put(object,8,full);Put(object,16,short_name);Put(object,0xA8,uint32_t{3});
        Put(object,0xF0,parent);Put(object,0xF8,uint32_t{0xFF});
        return object;
    };
    services.detach=[](void* object,Script callback) {
        Require(object==native_function.data() && callback==engine_script,"fake-engine detach identity");
        engine_script=nullptr;return true;
    };
    services.destroy=[](void* object) { Require(object==native_function.data(),"native destruction identity");++native_destructions; };
    auto allocate = Allocator::Bind(domain, [&](void*, RED4ext::Memory::AllocationResult* out, uint64_t size) {
        Require(size == 0xC0 && !guest_function, "old SDK allocation contract");
        guest_function = arena.Allocate(domain, static_cast<size_t>(size));
        *out = {guest_function, static_cast<size_t>(size)};
    });
    auto constructor = Constructor::Bind(domain, [&](void* object,void* parent,uint64_t full,uint64_t short_name,Script callback,uint32_t flags) -> void* {
        function_adapter.emplace(FunctionAdapter::Create(domain,arena,pointers,services,object,parent,full,short_name,callback,flags));
        return function_adapter->guest();
    });
    AddressCatalog catalog(profile::Core, domain);
    std::array<uintptr_t, 3> entries{reinterpret_cast<uintptr_t>(allocate.entry()),
        reinterpret_cast<uintptr_t>(constructor.entry()), reinterpret_cast<uintptr_t>(opcodes.data())};
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& op = profile::Operations[i];
        Check(catalog.Bind(op.id, op.primary, op.secondary, op.kind, op.signature, entries[i], op.table_size));
    }
    Check(catalog.Seal());
    auto resolve = Resolver::Bind(domain, [&](uint32_t hash) {
        for (const auto& op : profile::Operations) if (op.primary == hash) return catalog.Resolve(hash, op.kind);
        throw Failure(Error::UnknownOperation);
    });
    auto module = LoadLibraryW(fixture); Require(module, "load historical SDK fixture");
    auto configure = reinterpret_cast<void (*)(Resolver::Entry, void*, Counts*)>(GetProcAddress(module, "Configure"));
    auto install = reinterpret_cast<void (*)(void*)>(GetProcAddress(module, "Install"));
    auto lookup = reinterpret_cast<void* (*)()>(GetProcAddress(module, "Lookup"));
    auto change = reinterpret_cast<void (*)()>(GetProcAddress(module, "ChangeName"));
    Require(configure && install && lookup && change, "exports");
    configure(resolve.entry(), guest_class, &counts);
    install(reinterpret_cast<void*>(&AcceptCallback));
    Require(counts.live == 1, "callback retained"); (*native_callback)();
    Require(publications == 1 && lookup() == guest_function &&
            native_entries[0] == reinterpret_cast<uintptr_t>(native_function.data()), "canonical registration");
    int result{};
    { NativeFrame frame(code); engine_script(nullptr, frame.bytes.data(), &result, 0);
      Require(result == 42 && Get<uint8_t>(frame.bytes.data(), 98) == 2 && Get<char*>(frame.bytes.data(), 0) == code + 3, "old GetParameter"); }
    fail_opcode = true;
    bool caught = false;
    try { NativeFrame frame(code); engine_script(nullptr, frame.bytes.data(), &result, 0); }
    catch (const std::runtime_error&) { caught = true; }
    Require(caught && !Domain::Current() && frame_destructions == 3 && nested_calls == 1, "nested unwind cleanup");
    change();
    Require(Get<uint64_t>(guest_function, 8) == Get<uint64_t>(native_function.data(), 8), "immediate retained write coherence");
    Put(native_function.data(), 16, uint64_t{0x1234});
    Require(Get<uint64_t>(guest_function, 16) == 0x1234, "native changes observed through retained guest pointer");
    FaultDiagnostic diagnostic;
    Require(!backend.Pop(diagnostic) && backend.handled() > 0, "no rejected view accesses");

    // The fake engine has an explicit detach contract. Production does not.
    domain->Close(); Require(!domain->CanUnload(), "retained owners prevent unload");
    native_callback.reset(); Require(counts.live == 0, "old callback destructor");
    Check(resolve.Retire()); Check(constructor.Retire()); Check(allocate.Retire());
    const auto* cachedTable=opcodes.data();
    Check(function_adapter->Retire());function_adapter.reset();Require(native_destructions==1,"native function destroyed once after detach");
    Check(opcodes.Retire());
    bool stale=false;try { cachedTable[1](nullptr,nullptr,nullptr,nullptr); }catch(const Failure& error) { stale=error.code()==Error::Retired; }
    Require(stale,"cached table safely reaches retired entry");
    Require(!domain->CanUnload(), "live views retain domain");
    Check(arena.Retire(guest_entries)); Check(arena.Retire(guest_class));
    Require(domain->CanUnload(), "module kept until all callbacks, views and routes drain");
    FreeLibrary(module);
    std::printf("PASS SDK chain: callbacks=%d copies=%d live=%d frames=%u nested=%u mediated=%llu\n",
                counts.calls, counts.copies, counts.live, frame_destructions, nested_calls, static_cast<unsigned long long>(backend.handled()));
}

int main(int argc, char**)
{
    try { Run(argc > 1 ? L"legacy_guest_unoptimized.dll" : L"legacy_guest.dll"); }
    catch (const Failure& error) { std::fprintf(stderr, "FAIL core contract %u\n", unsigned(error.code())); return 1; }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL %s\n", error.what()); return 1; }
}
