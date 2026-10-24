// Restricted allocation-promotion and single-fault MOV experiments.
// Fake engine storage: this does not exercise real function construction or RTTI.
#define RED4EXT_STATIC_LIB
#include <RED4ext/Scripting/Functions.hpp>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

static_assert(sizeof(RED4ext::CClassStaticFunction) == 0x100);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(RED4ext::CClassStaticFunction, parent) == 0xF0);
static_assert(offsetof(RED4ext::CClassStaticFunction, regIndex) == 0xF8);
#pragma clang diagnostic pop

static void Require(bool ok, const char* text)
{
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", text); std::exit(42); }
}

alignas(16) static unsigned char native_object[0x100];
alignas(16) static unsigned char native_parent_a[16], native_parent_b[16];
static void* parent_view_a;
static void* parent_view_b;
static void* allocation;
static void* typed_view;
static bool promote;
static uint64_t allocated_size;
static size_t page_size;
static volatile LONG faults;
static volatile LONG steps;
static bool two_fault_reads;
static thread_local bool pending;
static thread_local DWORD64 saved_rcx;
static thread_local bool saved_tf;
static uintptr_t load_parent_pc, store_parent_pc, load_index_pc;

static void FakeAlloc(void*, RED4ext::Memory::AllocationResult* out, uint64_t size)
{
    allocation = VirtualAlloc(nullptr, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Require(allocation && size <= page_size, "isolated allocation");
    allocated_size = size;
    *out = {allocation, static_cast<size_t>(size)};
}

static void* FakeCtor(void* old, void*, uint64_t, uint64_t, void*, uint32_t)
{
    Require(old == allocation && allocated_size == 0xC0, "real old SDK allocation contract");
    std::memset(native_object, 0, sizeof(native_object));
    void* parent = native_parent_a;
    std::memcpy(native_object + 0xF0, &parent, sizeof(parent));
    uint32_t index = 42;
    std::memcpy(native_object + 0xF8, &index, sizeof(index));
    if (promote)
    {
        // Type is learned at the constructor, not from the 0xC0 allocation size.
        typed_view = old;
        DWORD previous;
        Require(VirtualProtect(old, page_size, PAGE_NOACCESS, &previous), "promote original allocation");
    }
    return native_object; // Old SDK Create deliberately ignores this value.
}

static void Unexpected() { Require(false, "unexpected fake engine call"); }
static uintptr_t Resolve(uint32_t hash)
{
    if (hash == RED4ext::Detail::AddressHashes::Memory_Vault_Alloc)
        return reinterpret_cast<uintptr_t>(&FakeAlloc);
    if (hash == RED4ext::Detail::AddressHashes::CClassStaticFunction_ctor)
        return reinterpret_cast<uintptr_t>(&FakeCtor);
    return reinterpret_cast<uintptr_t>(&Unexpected);
}

static LONG CALLBACK Handle(EXCEPTION_POINTERS* exception)
{
    auto* record = exception->ExceptionRecord;
    auto* cpu = exception->ContextRecord;
    if (pending)
    {
        pending = false;
        cpu->Rcx = saved_rcx;
        if (!saved_tf) cpu->EFlags &= ~0x100u;
        if (record->ExceptionCode != EXCEPTION_SINGLE_STEP || cpu->Rip != load_parent_pc + 7)
            return EXCEPTION_CONTINUE_SEARCH;
        // The benchmark fixes this backing pointer; other pointer values are unsupported.
        if (cpu->Rax != reinterpret_cast<uintptr_t>(native_parent_a))
            return EXCEPTION_CONTINUE_SEARCH;
        cpu->Rax = reinterpret_cast<uintptr_t>(parent_view_a);
        InterlockedIncrement(&steps);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2 ||
        cpu->Rcx != reinterpret_cast<uintptr_t>(typed_view))
        return EXCEPTION_CONTINUE_SEARCH;
    const auto address = record->ExceptionInformation[1];
    const auto access = record->ExceptionInformation[0];
    const auto base = reinterpret_cast<uintptr_t>(typed_view);
    // Prevalidated exact MOV forms. No decoder, allocation, lock, or logging here.
    if (cpu->Rip == load_parent_pc && address == base + 0xB0 && access == 0)
    {
        if (two_fault_reads)
        {
            saved_rcx = cpu->Rcx;
            saved_tf = (cpu->EFlags & 0x100u) != 0;
            pending = true;
            cpu->Rcx = reinterpret_cast<uintptr_t>(native_object) + 0x40;
            cpu->EFlags |= 0x100u;
            InterlockedIncrement(&faults);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        auto value = *reinterpret_cast<void* volatile*>(native_object + 0xF0);
        if (value == native_parent_a) cpu->Rax = reinterpret_cast<uintptr_t>(parent_view_a);
        else if (value == native_parent_b) cpu->Rax = reinterpret_cast<uintptr_t>(parent_view_b);
        else if (!value) cpu->Rax = 0;
        else return EXCEPTION_CONTINUE_SEARCH;
        cpu->Rip += 7;
    }
    else if (cpu->Rip == store_parent_pc && address == base + 0xB0 && access == 1)
    {
        void* value;
        if (cpu->Rdx == reinterpret_cast<uintptr_t>(parent_view_a)) value = native_parent_a;
        else if (cpu->Rdx == reinterpret_cast<uintptr_t>(parent_view_b)) value = native_parent_b;
        else if (!cpu->Rdx) value = nullptr;
        else return EXCEPTION_CONTINUE_SEARCH;
        *reinterpret_cast<void* volatile*>(native_object + 0xF0) = value;
        cpu->Rip += 7;
    }
    else if (cpu->Rip == load_index_pc && address == base + 0xB8 && access == 0)
    {
        // A write to EAX zero-extends into RAX; MOV does not change EFLAGS.
        cpu->Rax = *reinterpret_cast<volatile uint32_t*>(native_object + 0xF8);
        cpu->Rip += 6;
    }
    else return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&faults);
    return EXCEPTION_CONTINUE_EXECUTION;
}

using Read = void* (*)(void*);
using Write = void (*)(void*, void*);
using Index = uint32_t (*)(void*);

static double TimeReads(Read read, void* object, int count, uintptr_t& sink)
{
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    for (int i = 0; i < count; ++i) sink ^= reinterpret_cast<uintptr_t>(read(object));
    QueryPerformanceCounter(&end);
    return 1000.0 * (end.QuadPart - start.QuadPart) / frequency.QuadPart;
}

int main()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    page_size = info.dwPageSize;
    auto module = LoadLibraryW(L"guest.dll");
    Require(module != nullptr, "load guest");
    auto configure = reinterpret_cast<void (*)(uintptr_t (*)(uint32_t))>(GetProcAddress(module, "Configure"));
    auto create = reinterpret_cast<void* (*)()>(GetProcAddress(module, "Create"));
    auto same_size = reinterpret_cast<void* (*)()>(GetProcAddress(module, "AllocateSameSize"));
    auto append = reinterpret_cast<void (*)(void*, void*)>(GetProcAddress(module, "AppendFunction"));
    auto read = reinterpret_cast<Read>(GetProcAddress(module, "ReadParent"));
    auto write = reinterpret_cast<Write>(GetProcAddress(module, "WriteParent"));
    auto index = reinterpret_cast<Index>(GetProcAddress(module, "ReadIndex"));
    Require(configure && create && same_size && append && read && write && index, "guest exports");
    configure(Resolve);

    // This is a tiny verified instruction subset, not a general x64 decoder.
    const unsigned char load_bytes[] = {0x48, 0x8B, 0x81, 0xB0, 0, 0, 0, 0xC3};
    const unsigned char store_bytes[] = {0x48, 0x89, 0x91, 0xB0, 0, 0, 0, 0xC3};
    const unsigned char index_bytes[] = {0x8B, 0x81, 0xB8, 0, 0, 0, 0xC3};
    Require(!std::memcmp(reinterpret_cast<void*>(read), load_bytes, sizeof(load_bytes)), "parent load encoding");
    Require(!std::memcmp(reinterpret_cast<void*>(write), store_bytes, sizeof(store_bytes)), "parent store encoding");
    Require(!std::memcmp(reinterpret_cast<void*>(index), index_bytes, sizeof(index_bytes)), "index load encoding");
    load_parent_pc = reinterpret_cast<uintptr_t>(read);
    store_parent_pc = reinterpret_cast<uintptr_t>(write);
    load_index_pc = reinterpret_cast<uintptr_t>(index);

    auto original = create();
    Require(original == allocation && original != native_object, "constructor return is ignored");
    Require(read(original) == nullptr, "unadapted legacy storage is stale");
    std::puts("RETURN_ONLY allocated=0xc0 kept_original=1 replacement_ignored=1 stale_parent=1");
    VirtualFree(original, 0, MEM_RELEASE);

    parent_view_a = VirtualAlloc(nullptr, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    parent_view_b = VirtualAlloc(nullptr, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    Require(parent_view_a && parent_view_b, "parent identity tokens");
    auto handler = AddVectoredExceptionHandler(1, Handle);
    Require(handler != nullptr, "VEH registration");
    promote = true;
    auto view = create();
    Require(view == allocation && view == typed_view, "address preserved");
    Require(read(view) == parent_view_a && index(view) == 42, "translated fields and native-to-guest pointer");
    write(view, parent_view_b);
    void* published;
    std::memcpy(&published, native_object + 0xF0, sizeof(published));
    Require(published == native_parent_b && read(view) == parent_view_b, "guest-to-native pointer");
    write(view, nullptr);
    Require(read(view) == nullptr, "null round trip");
    write(view, parent_view_a);
    Require(faults == 7, "one exception per mapped instruction");
    std::printf("PROMOTION stable_address=1 translated_parent=1 translated_index=1 reverse_pointer=1 null=1 faults=%ld\n", faults);

    auto untyped = same_size();
    MEMORY_BASIC_INFORMATION information{};
    Require(VirtualQuery(untyped, &information, sizeof(information)) == sizeof(information), "query untyped allocation");
    Require(information.Protect == PAGE_READWRITE && untyped != typed_view, "same size does not imply same type");
    std::puts("UNTYPED same_size=0xc0 remains_writable=1");

    // Accessible raw aliases still bypass the backend, even after this mitigation.
    auto prior_faults = faults;
    Require(read(native_object) == nullptr && faults == prior_faults, "raw native bypass remains");
    std::puts("BYPASS raw_native_parent_is_stale=1 additional_faults=0");

    void* entries[4]{};
    struct ArrayLayout { void** entries; uint32_t capacity; uint32_t size; };
    static_assert(sizeof(ArrayLayout) == sizeof(RED4ext::DynArray<RED4ext::CClassStaticFunction*>));
    ArrayLayout array{entries, 4, 0};
    append(&array, view);
    Require(array.size == 1 && entries[0] == view && entries[0] != native_object && faults == prior_faults,
            "same-layout raw array leaks a guest pointer");
    std::puts("ARRAY_ESCAPE appended_guest_address=1 native_address_required=1 additional_faults=0");

    constexpr int count = 20000;
    uintptr_t sink = 0;
    // +0x40 makes the old +0xB0 read hit native +0xF0, solely as a cost baseline.
    // This biased pointer is not a compatibility design.
    for (int run = 0; run < 5; ++run)
    {
        auto native_ms = TimeReads(read, native_object + 0x40, count, sink);
        auto protected_ms = TimeReads(read, view, count, sink);
        two_fault_reads = true;
        auto two_fault_ms = TimeReads(read, view, count, sink);
        two_fault_reads = false;
        std::printf("READ_BENCH run=%d reads=%d native_ms=%.3f single_fault_ms=%.3f two_fault_ms=%.3f\n",
                    run, count, native_ms, protected_ms, two_fault_ms);
    }
    Require(faults == 7 + count * 10 && steps == count * 5 && !pending, "benchmark fault and step counts");
    std::printf("PASS faults=%ld steps=%ld sink=%llu\n", faults, steps, static_cast<unsigned long long>(sink));
    RemoveVectoredExceptionHandler(handler);
    VirtualFree(untyped, 0, MEM_RELEASE);
    VirtualFree(view, 0, MEM_RELEASE);
    VirtualFree(parent_view_a, 0, MEM_RELEASE);
    VirtualFree(parent_view_b, 0, MEM_RELEASE);
    FreeLibrary(module);
}
