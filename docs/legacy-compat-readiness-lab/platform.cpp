#include <windows.h>
#include <winternl.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <stdexcept>

static void Require(bool ok, const char* text)
{
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", text); std::exit(42); }
}

struct Event { int module; int phase; };
static Event events[64];
static volatile LONG event_count;
extern "C" __declspec(dllexport) void RecordEvent(int module, int phase)
{
    LONG slot = InterlockedIncrement(&event_count) - 1;
    if (slot < 64) events[slot] = {module, phase};
}

struct NotificationData
{
    ULONG flags;
    const UNICODE_STRING* full;
    const UNICODE_STRING* base;
    void* image;
    ULONG size;
};
// No external function calls, allocation, decoder, logging, or loader calls here.
static void CALLBACK Notification(ULONG reason, const NotificationData* data, void*)
{
    if (reason != 1 || !data->base) return;
    int module = 0;
    const wchar_t* names[] = {L"dependency.dll", L"initializer.dll"};
    for (int candidate = 0; candidate != 2; ++candidate)
    {
        unsigned i = 0;
        while (i < data->base->Length / 2 && names[candidate][i] &&
               names[candidate][i] == data->base->Buffer[i]) ++i;
        if (i == data->base->Length / 2 && names[candidate][i] == 0) module = candidate + 1;
    }
    if (module)
    {
        LONG slot = InterlockedIncrement(&event_count) - 1;
        if (slot < 64) events[slot] = {module, 1};
    }
}

static void LoaderProbe()
{
    using Register = LONG (NTAPI*)(ULONG, decltype(&Notification), void*, void**);
    using Unregister = LONG (NTAPI*)(void*);
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    auto reg = reinterpret_cast<Register>(GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    auto unreg = reinterpret_cast<Unregister>(GetProcAddress(ntdll, "LdrUnregisterDllNotification"));
    Require(reg && unreg, "loader notification API");
    void* cookie;
    Require(reg(0, Notification, nullptr, &cookie) == 0, "register notification");
    auto module = LoadLibraryW(L"initializer.dll");
    Require(module != nullptr, "load initializer and dependency");
    auto query = reinterpret_cast<int (*)()>(GetProcAddress(module, "QueryFixture"));
    Require(query && query() == 17, "query follows initialization");
    for (int id = 1; id <= 2; ++id)
    {
        int positions[4] = {-1, -1, -1, -1};
        for (LONG i = 0; i < event_count; ++i)
            if (events[i].module == id && events[i].phase <= 3) positions[events[i].phase] = i;
        Require(positions[1] >= 0 && positions[1] < positions[2] && positions[2] < positions[3],
                "notification before C++ initializer before DllMain");
    }
    Require(unreg(cookie) == 0, "unregister notification");
    FreeLibrary(module);
    std::printf("LOADER events=");
    for (LONG i = 0; i < event_count; ++i) std::printf("%d:%d,", events[i].module, events[i].phase);
    std::puts(" PASS dependency and parent ordering");
}

struct Result { uint64_t a, b, c; };
struct Domain { int id; int live; int calls; };
static Domain domains[] = {{11, 0, 0}, {22, 0, 0}};
static thread_local int active_domain;
struct Scope
{
    Domain& domain;
    int prior;
    explicit Scope(Domain& value) : domain(value), prior(active_domain)
    { active_domain = domain.id; InterlockedIncrement(reinterpret_cast<LONG*>(&domain.live)); }
    ~Scope()
    { active_domain = prior; InterlockedDecrement(reinterpret_cast<LONG*>(&domain.live)); }
};
template<unsigned Slot>
__declspec(noinline) Result Adapter(uint64_t a, double b, uint64_t c, double d, uint64_t e)
{
    Scope scope(domains[Slot]);
    Require(active_domain == domains[Slot].id, "bound domain");
    ++domains[Slot].calls;
    if (a == 999) throw std::runtime_error("unwind through typed adapter");
    if constexpr (Slot == 0)
    {
        auto child = Adapter<1>(a, b, c, d, e);
        Require(active_domain == 11 && child.c == 22, "nested domain restore");
    }
    return {a + c + e, static_cast<uint64_t>(b * 10 + d * 10), static_cast<uint64_t>(active_domain)};
}
static DWORD WINAPI Worker(void*)
{
    auto result = Adapter<1>(1, 2.5, 3, 4.5, 5);
    return result.a == 9 && result.b == 70 && result.c == 22 && active_domain == 0 ? 0 : 1;
}
static void AdapterProbe()
{
    using Fn = Result (*)(uint64_t, double, uint64_t, double, uint64_t);
    Fn volatile entry = Adapter<0>;
    auto result = entry(1, 2.5, 3, 4.5, 5);
    Require(result.a == 9 && result.b == 70 && result.c == 11 && active_domain == 0,
            "hidden aggregate return and mixed register/stack arguments");
    int caught = 0;
    try { entry(999, 0, 0, 0, 0); } catch (const std::runtime_error&) { caught = 1; }
    Require(caught && active_domain == 0 && domains[0].live == 0 && domains[1].live == 0,
            "exception restores domain and active-call counters");
    auto worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    Require(worker != nullptr && WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, "worker completion");
    DWORD status;
    Require(GetExitCodeThread(worker, &status) && status == 0, "worker bound domain");
    CloseHandle(worker);
    std::puts("ADAPTER PASS sret mixed_arguments nested_domains worker CXX_cleanup");
}

static __declspec(noinline) void ThrowFromCompiled() { throw 71; }
static __declspec(noinline) void RaiseFromCompiled() { RaiseException(0xE1231234, 0, 0, nullptr); }
static __declspec(noinline) int CatchSeh(void (*generated)(void (*)()), void (*operation)())
{
    __try { generated(operation); }
    __except(GetExceptionCode() == 0xE1231234 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return 1; }
    return 0;
}
static void UnwindProbe()
{
    // sub rsp,28h; call rcx; add rsp,28h; ret. Real call/return, no return-address spoofing.
    const unsigned char code[] = {0x48,0x83,0xEC,0x28,0xFF,0xD1,0x48,0x83,0xC4,0x28,0xC3};
    auto memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(memory != nullptr, "generated code allocation");
    std::memcpy(memory, code, sizeof(code));
    // UNWIND_INFO v1, prologue=4, one UWOP_ALLOC_SMALL with OpInfo=4 (40 bytes).
    const unsigned char unwind[] = {1,4,1,0,4,0x42,0,0};
    std::memcpy(memory + 64, unwind, sizeof(unwind));
    RUNTIME_FUNCTION table{0, static_cast<DWORD>(sizeof(code)), 64};
    Require(RtlAddFunctionTable(&table, 1, reinterpret_cast<DWORD64>(memory)), "register unwind");
    DWORD previous;
    Require(VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &previous), "W then X");
    Require(FlushInstructionCache(GetCurrentProcess(), memory, sizeof(code)), "flush generated code");
    auto generated = reinterpret_cast<void (*)(void (*)())>(memory);
    int caught = 0;
    try { generated(ThrowFromCompiled); } catch (int value) { caught = value == 71; }
    Require(caught && CatchSeh(generated, RaiseFromCompiled), "unwind through generated bridge");
    // Exercise virtual unwinding at a body instruction with a model stack.
    DWORD64 stack[8]{};
    stack[5] = 0x12345678;
    CONTEXT context{};
    context.Rip = reinterpret_cast<DWORD64>(memory) + 4;
    context.Rsp = reinterpret_cast<DWORD64>(stack);
    void* data = nullptr;
    DWORD64 establisher = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, reinterpret_cast<DWORD64>(memory), context.Rip, &table,
                     &context, &data, &establisher, nullptr);
    Require(context.Rip == 0x12345678 && context.Rsp == reinterpret_cast<DWORD64>(stack + 6), "virtual unwind body");
    Require(RtlDeleteFunctionTable(&table), "remove unwind after quiescence");
    VirtualFree(memory, 0, MEM_RELEASE);
    std::puts("UNWIND PASS registered_generated_code CXX SEH virtual_stack_walk");
}

int main()
{
    LoaderProbe();
    AdapterProbe();
    UnwindProbe();
    std::puts("PASS platform mechanisms; no game or production plugin loaded");
}
