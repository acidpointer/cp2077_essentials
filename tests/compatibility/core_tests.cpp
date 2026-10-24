#include <compat/AddressCatalog.hpp>
#include <compat/BorrowedValue.hpp>
#include <compat/CallbackBridge.hpp>
#include <compat/CallRouter.hpp>
#include <compat/DomainRegistry.hpp>
#include <compat/ObjectRegistry.hpp>
#include <compat/Profile.hpp>
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace compat;
static void Require(bool value, const char* message)
{ if (!value) throw std::runtime_error(message); }
template<class F> static void Reject(Error expected, F&& action)
{
    try { action(); } catch (const Failure& error) { Require(error.code() == expected, "wrong rejection"); return; }
    throw std::runtime_error("missing rejection");
}
static Domain::Ptr DomainFor(uint32_t id)
{ return Domain::Create(id, std::string(profile::Core.source), std::string(profile::Core.target)); }

static void SchemaTests()
{
    Require(profile::Core.Validate() == Error::None, "profile validation");
    auto schema = profile::Core;
    auto types = profile::Types;
    auto fields = profile::StackFrameFields;
    types[0].fields = fields; schema.types = types;
    fields[1].guest_offset = 0;
    Require(schema.Validate() == Error::InvalidSchema, "overlapping fields");
    fields = profile::StackFrameFields;
    fields[1].pointee = 999;
    Require(schema.Validate() == Error::InvalidSchema, "unknown pointee");
    fields = profile::StackFrameFields;
    types[0].fields = std::span(fields).first(fields.size() - 1);
    Require(schema.Validate() == Error::InvalidSchema, "unclassified VALUE padding");
    auto operations = profile::Operations;
    schema = profile::Core; schema.operations = operations;
    operations[1].primary = operations[0].primary;
    Require(schema.Validate() == Error::InvalidSchema, "ambiguous primary");

    auto domain = DomainFor(50);
    AddressCatalog catalog(profile::Core, domain);
    Reject(Error::IdentityMismatch, [&] { AddressCatalog wrong(profile::Core, Domain::Create(51, "unknown", "unknown")); });
    const auto& first = profile::Operations[0];
    Require(catalog.Bind(first.id, first.primary, "wrong", first.kind, first.signature, 1) == Error::IdentityMismatch, "secondary check");
    Require(catalog.Bind(first.id, first.primary, first.secondary, first.kind, "wrong", 1) == Error::IdentityMismatch, "signature check");
    Require(catalog.Seal() == Error::UnknownOperation, "incomplete catalog");
    for (const auto& operation : profile::Operations)
        Check(catalog.Bind(operation.id, operation.primary, operation.secondary, operation.kind, operation.signature, 100 + operation.primary, operation.table_size));
    Check(catalog.Seal());
    Reject(Error::IdentityMismatch, [&] { catalog.Resolve(first.primary, first.kind); });
    Domain::Scope scope(*domain);
    Require(catalog.Resolve(first.primary, first.kind) == 100 + first.primary, "bound adapter");
    Reject(Error::WrongKind, [&] { catalog.Resolve(first.primary, AddressKind::Table); });
    Reject(Error::UnknownOperation, [&] { catalog.Resolve(1, AddressKind::Function); });
}

struct MixedResult { uint64_t sum; double floating; uint64_t domain; };
struct MixedTag;
using MixedRouter = CallRouter<MixedResult(uint64_t, double, uint64_t, double, uint64_t), MixedTag, 4>;
struct StressTag;
using StressRouter = CallRouter<int(int), StressTag, 2>;
struct PublishedTag;
using PublishedRouter = CallRouter<void(), PublishedTag, 1>;
struct CleanupTag;
using CleanupRouter = CallRouter<void(), CleanupTag, 1>;

static void DomainTests()
{
    auto a = DomainFor(1), b = DomainFor(2);
    DomainRegistry modules(3);
    Check(modules.Add(100, 20, a));
    Check(modules.Add(200, 20, b));
    Require(modules.Add(110, 20, b) == Error::Overlap && !modules.Find(120), "module range ambiguity/boundary");
    Require(modules.Find(210) == b, "caller module domain");
    auto inner = MixedRouter::Bind(b, [](uint64_t x, double y, uint64_t z, double w, uint64_t last) {
        if (x == 999) throw std::runtime_error("guest throw");
        return MixedResult{x + z + last, y + w, Domain::Current()->id()};
    });
    auto outer = MixedRouter::Bind(a, [entry = inner.entry()](uint64_t x, double y, uint64_t z, double w, uint64_t last) {
        auto result = entry(x, y, z, w, last);
        Require(Domain::Current()->id() == 1 && result.domain == 2, "nested domain restoration");
        return result;
    });
    auto result = outer.entry()(1, 2.5, 3, 4.5, 5);
    Require(result.sum == 9 && result.floating == 7 && !Domain::Current(), "aggregate and mixed args");
    bool caught = false;
    try { outer.entry()(999, 0, 0, 0, 0); } catch (const std::runtime_error&) { caught = true; }
    Require(caught && !Domain::Current(), "exception domain cleanup");
    std::atomic<bool> worker_ok{};
    std::thread worker([&] { worker_ok = inner.entry()(1, 2, 3, 4, 5).domain == 2 && !Domain::Current(); });
    worker.join(); Require(worker_ok, "captured domain on new thread");
    auto cached = inner.entry();
    Check(inner.Retire());
    Reject(Error::Retired, [&] { cached(1, 2, 3, 4, 5); });
    Check(outer.Retire());
    a->Close(); b->Close(); Require(a->CanUnload() && b->CanUnload(), "owner cleanup");

    auto c = DomainFor(3);
    std::atomic<bool> entered{}, release{}, finished{};
    auto held = StressRouter::Bind(c, [&](int value) {
        entered.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        return value;
    });
    auto held_entry = held.entry();
    std::thread running([&] { finished = held_entry(7) == 7; });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    c->Close(); Check(held.Retire());
    Require(!c->CanUnload(), "active call owns retired binding");
    release.store(true, std::memory_order_release); running.join();
    Require(finished && c->CanUnload(), "retirement drains active call");
    Reject(Error::Closed, [&] { c->Retain(); });

    auto draining = DomainFor(7);
    {
        Domain::Scope admitted_call(*draining);
        draining->Close();
        Domain::Scope continuation(*draining);
        Require(Domain::Current() == draining.get() && !draining->CanUnload(), "in-flight nested work can drain");
    }
    Require(draining->CanUnload(), "nested drain releases active counts");

    auto cleanup_domain = DomainFor(8);
    bool cleaned = false;
    struct Cleanup
    {
        Domain::Ptr domain;
        bool* cleaned;
        ~Cleanup() { Require(Domain::Current() == domain.get() && !domain->CanUnload(), "callable destructor domain/lifetime"); *cleaned = true; }
    };
    auto payload = std::shared_ptr<Cleanup>(new Cleanup{cleanup_domain, &cleaned});
    auto cleanup_entry = CleanupRouter::Bind(cleanup_domain, [payload] {});
    payload.reset(); cleanup_domain->Close(); Check(cleanup_entry.Retire());
    Require(cleaned && cleanup_domain->CanUnload(), "callable destructor precedes owner release");

    // Start callers together and race terminal Close against atomic admission.
    auto racing = DomainFor(4);
    std::barrier start(5);
    std::atomic<unsigned> admitted{}, denied{};
    std::vector<std::thread> callers;
    for (unsigned i = 0; i < 4; ++i) callers.emplace_back([&] {
        start.arrive_and_wait();
        for (unsigned n = 0; n < 5000; ++n)
        {
            try { Domain::Scope scope(*racing); ++admitted; Require(!racing->CanUnload(), "unload with active scope"); }
            catch (const Failure& error) { Require(error.code() == Error::Closed, "admission failure"); ++denied; }
        }
    });
    start.arrive_and_wait(); racing->Close();
    for (auto& caller : callers) caller.join();
    Require(admitted + denied == 20000 && racing->CanUnload(), "admission stress");

    auto published = DomainFor(5);
    struct MustRemainPublished
    {
        ~MustRemainPublished() { std::fputs("FAIL published callable destroyed at shutdown\n", stderr); std::abort(); }
    };
    auto resident = std::make_shared<MustRemainPublished>();
    auto permanent = PublishedRouter::Bind(published, [resident] {});
    resident.reset();
    permanent.Publish(); published->Close();
    Require(permanent.Retire() == Error::Published && !published->CanUnload(), "published owner cannot roll back");
    Reject(Error::Capacity, [&] { PublishedRouter::Bind(DomainFor(6), [] {}); });
}

struct Payload
{
    int* live;
    explicit Payload(int* count) : live(count) { ++*live; }
    Payload(const Payload& other) : live(other.live) { ++*live; }
    ~Payload() { --*live; }
};
static void CallbackTests()
{
    auto domain = DomainFor(9);
    int live = 0;
    CallbackHandler<int(int)> handler{
        [](const void*, int&& value) { Require(Domain::Current()->id() == 9, "callback domain"); return value + 2; },
        [](void* out, void* in) { std::construct_at(static_cast<Payload*>(out), *static_cast<Payload*>(in)); },
        [](void*, void*) {},
        [](void* target) { Require(Domain::Current()->id() == 9, "cleanup enters retained domain"); std::destroy_at(static_cast<Payload*>(target)); }
    };
    std::unique_ptr<CallbackBridge<int(int), 32>> callback;
    {
        alignas(8) std::array<std::byte, 32> storage{};
        auto* source = std::construct_at(reinterpret_cast<Payload*>(storage.data()), &live);
        callback = CallbackBridge<int(int), 32>::Copy(domain, storage, &handler);
        std::destroy_at(source);
    }
    Require(live == 1 && (*callback)(40) == 42, "retained callback survives source destruction");
    domain->Close(); Require(!domain->CanUnload(), "callback owner prevents unload");
    Reject(Error::Closed, [&] { (*callback)(1); });
    callback.reset(); Require(live == 0 && domain->CanUnload() && !Domain::Current(), "cleanup after close");

    auto failing = DomainFor(10);
    handler.copy = [](void*, void*) { throw std::runtime_error("copy failed"); };
    std::array<std::byte, 32> source{};
    bool caught = false;
    try { CallbackBridge<int(int), 32>::Copy(failing, source, &handler); }
    catch (const std::runtime_error&) { caught = true; }
    failing->Close(); Require(caught && failing->CanUnload(), "failed callback copy releases provisional owner");
}

struct TranslationContext { ObjectRegistry* registry; uint32_t domain; };
static Error Translate(void* context, TypeId type, Direction direction, uintptr_t from, uintptr_t& to)
{
    auto& ctx = *static_cast<TranslationContext*>(context);
    auto read = ctx.registry->Read();
    return read.Translate(ctx.domain, type, direction, from, to);
}
template<class T> static void Put(void* data, size_t offset, T value)
{ std::memcpy(static_cast<std::byte*>(data) + offset, &value, sizeof(value)); }
template<class T> static T Get(const void* data, size_t offset)
{ T value; std::memcpy(&value, static_cast<const std::byte*>(data) + offset, sizeof(value)); return value; }

static void ValueTests()
{
    ObjectRegistry registry(profile::Core, 16);
    Check(registry.Add({1, profile::Function, 1, 0x1000, 0x2000, 192, 256}));
    Check(registry.Add({2, profile::Function, 1, 0x3000, 0x2000, 192, 256}));
    Check(registry.Add({1, profile::Bytecode, 1, 0x5000, 0x6000, 8, 8, true, true}));
    Check(registry.Add({1, profile::Class, 1, 0x10000, 0x20000, 720, 720, true}));
    TranslationContext context{&registry, 1};
    PointerTranslator translator{&context, Translate};
    uintptr_t pointer{};
    Require(Translate(&context,profile::RttiType,Direction::ToNative,0x10000,pointer)==Error::None && pointer==0x20000,
            "canonical zero-offset class/type base");
    Require(Translate(&context,profile::RttiType,Direction::ToNative,0x10008,pointer)==Error::UnknownPointer,
            "base alias does not inherit a concrete linear-bytes permission");
    Require(Translate(&context, profile::Function, Direction::ToGuest, 0x2000, pointer) == Error::None && pointer == 0x1000, "domain canonical identity");
    Require(Translate(&context, profile::Function, Direction::ToNative, 0x1008, pointer) == Error::UnknownPointer, "unreviewed interior rejected");
    std::array<uintptr_t, 3> input{0x1000, 0, 0xDEAD}, output{9, 9, 9};
    Require(ConvertPointers(profile::Function, Direction::ToNative, input, output, translator) == Error::UnknownPointer &&
            output == std::array<uintptr_t, 3>{9, 9, 9}, "array preflight atomicity");
    input[2] = 0x1000; Check(ConvertPointers(profile::Function, Direction::ToNative, input, output, translator));
    Require(output[0] == 0x2000 && output[1] == 0 && output[2] == 0x2000, "typed array publication");

    ValueMarshaller plan(profile::Core, profile::StackFrame);
    std::array<std::byte, 168> native{};
    Put(native.data(), 0, uintptr_t{0x6000}); Put(native.data(), 8, uintptr_t{0x2000});
    native[100] = std::byte{1}; native[152] = std::byte{0xAB};
    {
        BorrowedValue root(1, plan, native, translator);
        Require(Get<uintptr_t>(root.guest(), 8) == 0x1000, "function pointer copy-in");
        std::array<std::byte, 168> child{};
        Put(child.data(), 0, uintptr_t{0x6000}); Put(child.data(), 72, reinterpret_cast<uintptr_t>(native.data()));
        {
            BorrowedValue nested(1, plan, child, translator);
            Require(Get<uintptr_t>(nested.guest(), 72) == reinterpret_cast<uintptr_t>(root.guest()), "canonical parent frame");
            nested.CallGuest([&](void* guest) {
                Put(guest, 0, uintptr_t{0x5002});
                nested.CallNative([](void* frame) { Put(frame, 98, uint8_t{3}); });
                Require(Get<uint8_t>(guest, 98) == 3, "nested output propagation");
            });
        }
        bool caught = false;
        try { root.CallGuest([&](void*) { root.CallNative([](void* p) { Put(p, 98, uint8_t{7}); throw 12; }); }); }
        catch (int value) { caught = value == 12; }
        Require(caught && Get<uint8_t>(native.data(), 98) == 7, "exception copy-out preserves native changes");
        auto before = native;
        Put(root.guest(), 8, uintptr_t{0xBAD}); Put(root.guest(), 98, uint8_t{9});
        Require(root.ToNative() == Error::UnknownPointer && native == before, "failed conversion leaves target unchanged");
        Put(root.guest(), 8, uintptr_t{0x1000}); Put(root.guest(), 32, uint64_t{1});
        Require(root.ToNative() == Error::UnsupportedValue && native == before, "unknown nonzero field rejected");
    }
    Require(native[100] == std::byte{1} && native[152] == std::byte{0xAB}, "native-owned tail preserved");
    Check(registry.Retire(1, 0x1000, 1));
    Require(registry.Add({1, profile::Function, 2, 0x1000, 0x2000, 192, 256}) == Error::Overlap, "guest tombstone not reused");
    Require(registry.Add({1, profile::Function, 1, 0x8000, 0x2000, 192, 256}) == Error::IdentityMismatch, "generation must advance");
    Check(registry.Add({1, profile::Function, 2, 0x8000, 0x2000, 192, 256}));
    Require(Translate(&context, profile::Function, Direction::ToNative, 0x1000, pointer) == Error::UnknownPointer, "stale alias stays retired");
    Require(Translate(&context, profile::Function, Direction::ToGuest, 0x2000, pointer) == Error::None && pointer == 0x8000, "native reuse has new guest identity");
}

int main()
{
    try { SchemaTests(); DomainTests(); CallbackTests(); ValueTests(); std::puts("PASS core: schema identities domains lifetime callbacks routing frames containers retirement"); }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL %s\n", error.what()); return 1; }
}
