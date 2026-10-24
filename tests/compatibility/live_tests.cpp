// Independent x86 hardware oracle plus mapping/reclamation regressions.
// The oracle executes the instruction bytes; it does not reuse decoder logic.
#include <compat/InstructionPlan.hpp>
#include <compat/Profile.hpp>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <compat/ExceptionBackend.hpp>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

using namespace compat;
static void Require(bool value, const char* message)
{ if (!value) throw std::runtime_error(message); }
static constexpr Field scalarFields[]{{"bytes", 0, 0, 32, FieldKind::Scalar}};
static constexpr Type scalarTypes[]{{1, "Scalar", Policy::LiveView, 32, 32, 8, scalarFields}};
static constexpr Schema scalarSchema{"test-old", "test-new", scalarTypes, {}};
struct alignas(16) Words { uint64_t value[4]; };

class Code
{
public:
    explicit Code(const std::vector<uint8_t>& code)
    {
        Require(code.size() <= 4080, "oracle size");
#ifdef _WIN32
        allocation_ = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        Require(allocation_, "oracle allocation");
        address=static_cast<std::byte*>(allocation_)+16;
        std::memcpy(address, code.data(), code.size());
        DWORD old{}; Require(VirtualProtect(allocation_, 4096, PAGE_EXECUTE_READ, &old), "oracle protection");
        Require(FlushInstructionCache(GetCurrentProcess(), address, code.size()), "oracle cache flush");
#else
        allocation_ = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        Require(allocation_ != MAP_FAILED, "oracle allocation");
        // Sanitizers may inspect the prefix before an uninstrumented assembly
        // entry. Keep that prefix readable and zero, rather than a guard page.
        address=static_cast<std::byte*>(allocation_)+16;
        std::memcpy(address, code.data(), code.size());
        Require(mprotect(allocation_, 4096, PROT_READ | PROT_EXEC) == 0, "oracle protection");
        __builtin___clear_cache(static_cast<char*>(address), static_cast<char*>(address) + code.size());
#endif
    }
    ~Code()
    {
#ifdef _WIN32
        VirtualFree(allocation_, 0, MEM_RELEASE);
#else
        munmap(allocation_, 4096);
#endif
    }
    Code(const Code&) = delete;
    Code& operator=(const Code&) = delete;
    void* address{};
private:
    void* allocation_{};
};
static std::vector<uint8_t> Oracle(std::span<const uint8_t> instruction)
{
    std::vector<uint8_t> code;
    const auto bytes = [&](std::initializer_list<uint8_t> values) { code.insert(code.end(), values); };
    const auto displacement = [&](size_t value) {
        for (unsigned i = 0; i < 4; ++i) code.push_back(static_cast<uint8_t>(value >> (i * 8)));
    };
    bytes({0x53}); // Save RBX; it holds the context pointer, never an instruction operand.
#ifdef _WIN32
    bytes({0x48, 0x89, 0xCB}); // RBX = RCX (Windows argument).
#else
    bytes({0x48, 0x89, 0xFB}); // RBX = RDI (System V argument).
#endif
    bytes({0xFF, 0xB3}); displacement(offsetof(MachineContext, flags)); bytes({0x9D}); // POPFQ.
    constexpr uint8_t regs[]{0, 1, 2, 8, 9, 10, 11};
    for (auto reg : regs)
    {
        bytes({static_cast<uint8_t>(reg >= 8 ? 0x4C : 0x48), 0x8B, static_cast<uint8_t>(0x83 | ((reg & 7) << 3))});
        displacement(offsetof(MachineContext, gpr) + reg * 8);
    }
    for (unsigned reg = 0; reg < 6; ++reg)
    { bytes({0xF3, 0x0F, 0x6F, static_cast<uint8_t>(0x83 | (reg << 3))}); displacement(offsetof(MachineContext, xmm) + reg * 16); }
    code.insert(code.end(), instruction.begin(), instruction.end());
    bytes({0x9C, 0x8F, 0x83}); displacement(offsetof(MachineContext, flags)); // PUSHFQ / POP [RBX+flags].
    for (auto reg : regs)
    {
        bytes({static_cast<uint8_t>(reg >= 8 ? 0x4C : 0x48), 0x89, static_cast<uint8_t>(0x83 | ((reg & 7) << 3))});
        displacement(offsetof(MachineContext, gpr) + reg * 8);
    }
    for (unsigned reg = 0; reg < 6; ++reg)
    { bytes({0xF3, 0x0F, 0x7F, static_cast<uint8_t>(0x83 | (reg << 3))}); displacement(offsetof(MachineContext, xmm) + reg * 16); }
    bytes({0x5B, 0xC3}); // Restore RBX / RET. This oracle cannot be unwound through.
    return code;
}
struct Case { const char* name; std::vector<uint8_t> code; uint64_t definedFlags{0x8D5}; bool changesBase{}; };
static void Hardware()
{
    std::vector<Case> cases;
    const auto add = [&](const char* name, std::initializer_list<uint8_t> bytes, uint64_t flags = 0x8D5, bool base = false) {
        cases.push_back({name, bytes, flags, base});
    };
    add("mov64", {0x48,0x8B,0x01}); add("mov32", {0x8B,0x01}); add("mov16", {0x66,0x8B,0x01});
    add("mov8", {0x8A,0x01}); add("mov-ah", {0x8A,0x21}); add("mov-r8", {0x4C,0x8B,0x01});
    add("mov-overlap", {0x48,0x8B,0x09},0x8D5,true);
    add("store64", {0x48,0x89,0x11}); add("store32", {0x89,0x11}); add("store16", {0x66,0x89,0x11});
    add("store8", {0x88,0x11}); add("store-ah", {0x88,0x21});
    add("immediate64", {0x48,0xC7,0x01,0xFF,0xFF,0xFF,0xFF}); add("immediate8", {0xC6,0x01,0xA5});
    add("zx8", {0x0F,0xB6,0x01}); add("zx16", {0x48,0x0F,0xB7,0x01});
    add("sx8", {0x48,0x0F,0xBE,0x01}); add("sx16", {0x0F,0xBF,0x01}); add("sx32", {0x48,0x63,0x01});
    for (auto width : {1,2,4,8})
    {
        std::vector<uint8_t> prefix;
        if (width == 2) prefix.push_back(0x66);
        if (width == 8) prefix.push_back(0x48);
        for (uint8_t op : {0x00,0x08,0x20,0x28,0x30,0x38})
            for (unsigned direction = 0; direction < 2; ++direction)
            {
                auto bytes = prefix; bytes.push_back(static_cast<uint8_t>(op + (width != 1) + direction * 2));
                bytes.push_back(0x11); // RDX and [RCX].
                const bool logical = op == 0x08 || op == 0x20 || op == 0x30;
                cases.push_back({"arithmetic", bytes, logical ? 0x8C5u : 0x8D5u});
                if (direction == 0 && op != 0x38) { bytes.insert(bytes.begin(),0xF0); cases.push_back({"locked-arithmetic",bytes,logical ? 0x8C5u : 0x8D5u}); }
            }
        for (uint8_t op : {0xC0,0xB0})
            for (bool locked : {false,true})
            {
                auto bytes = prefix; bytes.push_back(0x0F); bytes.push_back(static_cast<uint8_t>(op + (width != 1))); bytes.push_back(0x11);
                if (locked) bytes.insert(bytes.begin(),0xF0);
                cases.push_back({op == 0xC0 ? "xadd" : "cmpxchg",bytes});
            }
        for (uint8_t extension : {0x01,0x09})
        {
            auto bytes = prefix; bytes.push_back(width == 1 ? 0xFE : 0xFF); bytes.push_back(extension);
            cases.push_back({"inc-dec",bytes}); bytes.insert(bytes.begin(),0xF0); cases.push_back({"locked-inc-dec",bytes});
        }
        auto bytes = prefix; bytes.push_back(width == 1 ? 0x86 : 0x87); bytes.push_back(0x11);
        cases.push_back({"xchg",bytes});
    }
    for (uint8_t condition = 0x40; condition < 0x50; ++condition)
    {
        cases.push_back({"cmov32",{0x0F,condition,0x01}});
        cases.push_back({"cmov64",{0x48,0x0F,condition,0x01}});
    }
    add("cmp-imm", {0x48,0x83,0x39,0xFF}); add("test", {0x48,0x85,0x11},0x8C5);
    add("test-imm", {0xF7,0x01,0xAB,0xCD,0x00,0x80},0x8C5);
    add("add-imm", {0x48,0x83,0x01,0xFF});
    add("movups", {0x0F,0x10,0x01}); add("movups-store", {0x0F,0x11,0x01});
    add("movaps", {0x0F,0x28,0x01}); add("movaps-store", {0x0F,0x29,0x01});
    add("movdqu", {0xF3,0x0F,0x6F,0x01}); add("movdqa", {0x66,0x0F,0x6F,0x01});
    add("movdqu-store", {0xF3,0x0F,0x7F,0x01}); add("movdqa-store", {0x66,0x0F,0x7F,0x01});
    add("movss", {0xF3,0x0F,0x10,0x01}); add("movsd", {0xF2,0x0F,0x10,0x01});
    add("movss-store", {0xF3,0x0F,0x11,0x01}); add("movsd-store", {0xF2,0x0F,0x11,0x01});
    Words native{}, mediated{};
    LiveMemory memory(scalarSchema, 8);
    constexpr uintptr_t guest = 0x100000;
    Check(memory.Add({1,1,&scalarTypes[0],guest,reinterpret_cast<uintptr_t>(&mediated)}));
    uint64_t random = 0x284765837;
    const auto next = [&] { random ^= random << 13; random ^= random >> 7; random ^= random << 17; return random; };
    size_t total{};
    for (const auto& test : cases)
    {
        Code oracle(Oracle(test.code)); InstructionPlan plan;
        Check(InstructionPlan::Decode(std::as_bytes(std::span(test.code)),plan));
        Require(plan.length() == test.code.size(), "decoded length");
        for (unsigned trial = 0; trial < 96; ++trial)
        {
            for (auto& word : native.value) word = next();
            const uint64_t edges[]{0,1,UINT64_MAX,0x7F,0x80,0x7FFF,0x8000,0x7FFFFFFF,0x80000000,0x7FFFFFFFFFFFFFFF,0x8000000000000000};
            if (trial < std::size(edges)) native.value[0] = edges[trial];
            mediated = native;
            MachineContext actual;
            for (auto& reg : actual.gpr) reg = next();
            for (auto& reg : actual.xmm) for (auto& byte : reg) byte = std::byte(next());
            actual.gpr[0] = trial % 2 ? native.value[0] : next(); // CMPXCHG match and mismatch, including dirty upper RAX.
            if (trial % 4 == 1) actual.gpr[0] = (actual.gpr[0] & 0xFFFFFFFF) | 0xABCD123400000000ull;
            actual.gpr[2] = trial < std::size(edges) ? edges[std::size(edges)-trial-1] : next();
            actual.flags = 0x202 | (next() & 0x8D5); actual.ip = 0x200000;
            auto expected = actual;
            actual.gpr[1] = guest; expected.gpr[1] = reinterpret_cast<uintptr_t>(&native);
            reinterpret_cast<void (*)(MachineContext*)>(oracle.address)(&expected);
            if (!test.changesBase) expected.gpr[1] = guest;
            const auto error = plan.Execute(memory,actual,guest,false);
            if (error != Error::None || actual.gpr != expected.gpr || actual.xmm != expected.xmm ||
                ((actual.flags ^ expected.flags) & test.definedFlags) || std::memcmp(&native,&mediated,sizeof(native)))
            {
                std::fprintf(stderr,"oracle mismatch %s trial=%u error=%u flags=%llx/%llx RAX=%llx/%llx RDX=%llx/%llx bytes=",
                    test.name,trial,unsigned(error),static_cast<unsigned long long>(actual.flags),static_cast<unsigned long long>(expected.flags),
                    static_cast<unsigned long long>(actual.gpr[0]),static_cast<unsigned long long>(expected.gpr[0]),
                    static_cast<unsigned long long>(actual.gpr[2]),static_cast<unsigned long long>(expected.gpr[2]));
                for (auto byte : test.code) std::fprintf(stderr,"%02x",unsigned(byte));
                std::fputc('\n',stderr); throw std::runtime_error("hardware differential");
            }
            Require(actual.ip == 0x200000 + test.code.size(), "instruction progress"); ++total;
        }
    }
    std::printf("PASS hardware oracle: %zu encodings %zu cases, memory/GPR/XMM/defined flags\n",cases.size(),total);
}

static void Mappings()
{
    LiveMemory memory(profile::Core, 32);
    alignas(16) std::array<std::byte,0x100> function{};
    alignas(16) std::array<std::byte,0x2D0> cls{};
    std::array<uintptr_t,4> entries{};
    constexpr uintptr_t guestFunction = 0x10000, guestClass = 0x20000, guestEntries = 0x30000;
    Check(memory.Add({1,1,profile::Core.Find(profile::Function),guestFunction,reinterpret_cast<uintptr_t>(function.data())}));
    Check(memory.Add({1,1,profile::Core.Find(profile::Class),guestClass,reinterpret_cast<uintptr_t>(cls.data())}));
    Check(memory.Add({1,1,profile::Core.Find(profile::FunctionPointer),guestEntries,reinterpret_cast<uintptr_t>(entries.data()),4}));
    uintptr_t pointer = guestFunction, read{};
    Check(memory.Write(guestEntries,std::as_bytes(std::span(&pointer,1))));
    Require(entries[0] == reinterpret_cast<uintptr_t>(function.data()),"pointer element publication");
    Check(memory.Read(guestEntries,std::as_writable_bytes(std::span(&read,1)))); Require(read == pointer,"pointer read translation");
    Check(memory.Translate(1,profile::FunctionPointer,Direction::ToNative,guestEntries+16,read));
    Require(read == reinterpret_cast<uintptr_t>(&entries[2]),"array interior identity");
    Require(memory.Write(guestEntries,std::as_bytes(std::span(&pointer,1)).first(4)) == Error::UnsupportedAccess,"partial pointer store rejected");
    std::array<uintptr_t,2> source{guestFunction,0xDEADBEEF};
    entries[0] = entries[1] = 0;
    Require(memory.Write(guestEntries,std::as_bytes(std::span(source))) == Error::UnknownPointer,"unknown pointer rejected");
    Require(!entries[0] && !entries[1],"whole instruction preflight");
    Check(memory.Retire(1,guestFunction,1));
    Require(memory.Read(guestFunction+8,std::as_writable_bytes(std::span(&read,1))) == Error::Retired,"stale view rejected");
    Require(memory.Add({1,2,profile::Core.Find(profile::Function),guestFunction,reinterpret_cast<uintptr_t>(function.data())}) == Error::Overlap,"guest quarantine");
    Require(memory.Add({1,1,profile::Core.Find(profile::Function),0x40000,reinterpret_cast<uintptr_t>(function.data())}) == Error::IdentityMismatch,"native generation");
    Check(memory.Add({1,2,profile::Core.Find(profile::Function),0x40000,reinterpret_cast<uintptr_t>(function.data())}));
    std::puts("PASS live mappings: pointers arrays preflight quarantine native reuse");
}
static void Retirement()
{
    Words backing{}; LiveMemory memory(scalarSchema,8); constexpr uintptr_t guest=0x10000;
    Check(memory.Add({1,1,&scalarTypes[0],guest,reinterpret_cast<uintptr_t>(&backing)}));
    std::atomic<unsigned> ready{}, bad{}; std::atomic<uint64_t> completed{};
    std::vector<std::thread> workers;
    for (unsigned i=0;i<4;++i) workers.emplace_back([&] {
        ++ready;
        for (;;) {
            uint64_t previous{};
            const auto result=memory.Atomic(guest,8,LiveMemory::AtomicOp::Add,1,0,previous);
            if (result==Error::Retired) break;
            if (result==Error::Busy) continue;
            if (result!=Error::None) { ++bad; break; }
            ++completed;
        }
    });
    while (ready.load()!=4 || completed.load()<10000) std::this_thread::yield();
    Check(memory.Retire(1,guest,1));
    const auto retiredValue=backing.value[0];
    for(auto& worker:workers)worker.join();
    Require(!bad && retiredValue==completed && backing.value[0]==retiredValue,"retirement drains atomic accesses");
    std::puts("PASS concurrent native atomics and retirement");
}
static void CompoundMappings()
{
    LiveMemory memory(profile::Core,6);
    alignas(8) std::array<std::byte,0x2D0> cls{};
    std::array<uintptr_t,2> entries{};
    const auto* element=profile::Core.Find(profile::FunctionPointer);
    const auto* tail=profile::Core.Find(profile::ArrayAllocator);
    LiveMemory::Mapping parts[]{
        {1,1,element,0x20000,reinterpret_cast<uintptr_t>(entries.data()),1},
        {1,1,tail,0x20000,reinterpret_cast<uintptr_t>(&entries[1]),1}};
    Require(memory.AddBatch(parts)==Error::Overlap,"compound overlap rejected");
    uintptr_t translated{};
    Require(memory.Translate(1,element->id,Direction::ToGuest,reinterpret_cast<uintptr_t>(entries.data()),translated)==Error::UnknownPointer,
            "failed batch publishes no prefix");
    parts[1].guest+=8;Check(memory.AddBatch(parts));
    Check(memory.Add({1,1,profile::Core.Find(profile::Class),0x10000,reinterpret_cast<uintptr_t>(cls.data())}));
    Check(memory.Add({1,1,profile::Core.Find(profile::AllocatorVtable),0x30000,0x40000,1,true}));
    struct Header { uintptr_t data;uint32_t capacity,size; };
    Header empty{0x40000,0,0};std::memcpy(cls.data()+0x58,&empty,16);entries[1]=0x40000;
    Check(memory.Read(0x10058,std::as_writable_bytes(std::span(&translated,1))));
    Require(translated==0x30000,"empty data slot means allocator vtable");
    Check(memory.Read(0x20008,std::as_writable_bytes(std::span(&translated,1))));
    Require(translated==0x30000,"tail allocator translation");
    Header populated{0x20000,1,0};
    Check(memory.Write(0x10058,std::as_bytes(std::span(&populated,1))));
    Header native{};std::memcpy(&native,cls.data()+0x58,16);
    Require(native.data==reinterpret_cast<uintptr_t>(entries.data()) && native.capacity==1,"vector publication uses new capacity");
    empty.data=0x30000;Check(memory.Write(0x10058,std::as_bytes(std::span(&empty,1))));
    std::memcpy(&native,cls.data()+0x58,16);
    Require(native.data==0x40000 && !native.capacity,"vector clearing restores allocator union");
    LiveMemory exhausted(profile::Core,1);
    Require(exhausted.AddBatch(parts)==Error::Capacity,"batch capacity rejected atomically");
    Check(exhausted.Add(parts[0]));
    std::puts("PASS compound mappings: atomic publication, allocator discriminator and vector transitions");
}
static void Rejections()
{
    const std::vector<std::vector<uint8_t>> invalid{
        {},{0x48},{0x48,0x8B},{0xF0,0x48,0x89,0x01}, // Truncated / illegal LOCK MOV.
        {0x67,0x8B,0x01},{0x65,0x48,0x8B,0x01}, // Unreviewed address size / GS base.
        {0x0F,0x6F,0x01},{0xF2,0x0F,0x6F,0x01}, // MMX / invalid mandatory prefix.
        {0xC5,0xF8,0x10,0x01},{0xF3,0xA4}, // AVX / REP MOVSB require separate semantics.
        {0x48,0xFF,0x11},{0x48,0x8B,0xC1}, // Indirect CALL / nonmemory instruction.
        std::vector<uint8_t>(16,0x66)};
    for(const auto& bytes:invalid)
    { InstructionPlan plan; Require(InstructionPlan::Decode(std::as_bytes(std::span(bytes)),plan)==Error::UnsupportedInstruction,"decode rejection"); }
    Words backing{{11,22,33,44}}; LiveMemory memory(scalarSchema,8);
    Check(memory.Add({1,1,&scalarTypes[0],0x10000,reinterpret_cast<uintptr_t>(&backing)}));
    InstructionPlan plan;
    const uint8_t store[]{0x48,0x89,0x11};
    Check(InstructionPlan::Decode(std::as_bytes(std::span(store)),plan));
    MachineContext context; context.gpr[1]=0x10000;context.gpr[2]=99;context.flags=0x302;
    auto original=context;
    Require(plan.Execute(memory,context,0x10000,true)==Error::UnsupportedAccess,"debug single step not consumed");
    Require(context.gpr==original.gpr && context.flags==original.flags && backing.value[0]==11,"rejected instruction has no effects");
    context.flags=0x202;
    Require(plan.Execute(memory,context,0x20000,true)==Error::UnsupportedAccess,"fault/operand mismatch");
    const uint8_t sib[]{0x4A,0x8B,0x44,0xD1,0x08}; // RAX <- [RCX + R10*8 + 8].
    Check(InstructionPlan::Decode(std::as_bytes(std::span(sib)),plan));context.gpr[10]=1;
    Require(plan.Address(context)==0x10010,"extended SIB address");
    Check(plan.Execute(memory,context,0x10010,false));Require(context.gpr[0]==33,"extended SIB load");
    const uint8_t rip[]{0x48,0x8B,0x05,0xF9,0xFF,0xFF,0xFF};
    Check(InstructionPlan::Decode(std::as_bytes(std::span(rip)),plan));context.ip=0x10000;
    Require(plan.Address(context)==context.ip,"RIP displacement sign and next instruction");
    std::puts("PASS instruction rejection, unchanged failure state and addressing");
}
#ifdef _WIN32
static std::atomic<unsigned> observedFaults{}, observedSteps{};
static LONG CALLBACK Observer(EXCEPTION_POINTERS* info)
{
    if(info->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION)++observedFaults;
    if(info->ExceptionRecord->ExceptionCode==EXCEPTION_SINGLE_STEP)++observedSteps;
    return EXCEPTION_CONTINUE_SEARCH;
}
static bool TryLoad(const volatile uint64_t* pointer) noexcept
{
    __try { (void)*pointer; }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
    return true;
}
static bool TryStore(volatile uint64_t* pointer) noexcept
{
    __try { *pointer=55; }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
    return true;
}
static bool TryUnsupported(void* function, void* pointer) noexcept
{
    __try { reinterpret_cast<void (*)(void*,uint64_t)>(function)(pointer,123); }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
    return true;
}
static bool TryStep() noexcept
{
    __try { RaiseException(EXCEPTION_SINGLE_STEP,0,0,nullptr); }
    __except(GetExceptionCode()==EXCEPTION_SINGLE_STEP ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
static void WindowsViews()
{
    auto domain=Domain::Create(3,"test-old","test-new");
    Words backing{{1,2,3,4}};
    ViewArena arena(scalarSchema,16); ExceptionBackend backend(arena);
    auto* guest=static_cast<volatile uint64_t*>(arena.Borrow(domain,1,&backing));
    void* observer=AddVectoredExceptionHandler(0,Observer);Require(observer,"observer installation");
    *guest=7;Require(backing.value[0]==7 && *guest==7,"live access");
    bool rejected=false;
    try { (void)arena.Borrow(domain,1,&backing,2); }
    catch(const Failure& error) { rejected=error.code()==Error::InvalidRange; }
    Require(rejected,"canonical borrow cannot extend beyond registered backing");
    auto* provisional=arena.Allocate(domain,sizeof(backing));
    *static_cast<uint64_t*>(provisional)=19;
    Words promoted{{19,2,3,4}};Check(arena.Promote(provisional,1,&promoted));
    Require(*static_cast<volatile uint64_t*>(provisional)==19,"allocation address promotion");
    std::vector<std::thread> threads;std::atomic<unsigned> good{};
    for(unsigned i=0;i<4;++i)threads.emplace_back([&] {
        for(unsigned j=0;j<100;++j) { if(*guest!=7) return; }
        try { (void)*guest;throw std::runtime_error("guest unwind"); }
        catch(const std::runtime_error&) { ++good; }
    });
    for(auto& thread:threads)thread.join();Require(good==4,"thread views and C++ unwind");
    Require(!observedFaults,"handled owned faults stop dispatch");
    Code unsupported({0x48,0x0F,0xC3,0x11,0xC3}); // MOVNTI [RCX],RDX; RET: unwindable leaf, unsupported by backend.
    Require(!TryUnsupported(unsupported.address,const_cast<uint64_t*>(guest)),"unsupported instruction preserves AV dispatch");
    FaultDiagnostic diagnostic;
    Require(backend.Pop(diagnostic) && diagnostic.error==Error::UnsupportedInstruction && backing.value[0]==7,"bounded failure diagnostic");
    auto* foreign=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);Require(foreign,"foreign page");
    Require(!TryLoad(static_cast<volatile uint64_t*>(foreign)),"foreign AV dispatch");VirtualFree(foreign,0,MEM_RELEASE);
    Require(TryStep() && observedSteps==1,"foreign single-step preserved");
    Check(arena.Retire(const_cast<uint64_t*>(guest)));
    Require(!TryLoad(guest),"retired view dispatch");
    Require(backend.Pop(diagnostic) && diagnostic.error==Error::Retired,"retired diagnostic");
    Require(observedFaults==3 && !backend.Pop(diagnostic),"only unsupported, foreign and retired AV reached observer");
    // A native protection violation must not strand a lease/TLS handling flag.
    auto* damaged=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);Require(damaged,"native backing page");
    new(damaged) Words{{1,2,3,4}};
    auto* damagedView=static_cast<volatile uint64_t*>(arena.Borrow(domain,1,damaged));
    DWORD old{};Require(VirtualProtect(damaged,4096,PAGE_READONLY,&old),"native read-only page");
    Require(!TryStore(damagedView),"native write fault forwarded");
    Require(backend.Pop(diagnostic) && diagnostic.error==Error::NativeFault,"native-fault diagnostic");
    uint64_t previous{};
    Require(arena.memory().Atomic(reinterpret_cast<uintptr_t>(damagedView),8,LiveMemory::AtomicOp::Add,1,0,previous)==Error::NativeFault,"native atomic fault releases lease");
    Require(VirtualProtect(damaged,4096,PAGE_READWRITE,&old),"restore native backing");
    Require(TryStore(damagedView) && *damagedView==55,"handler recovers after nested native fault");
    Check(arena.Retire(const_cast<uint64_t*>(damagedView)));VirtualFree(damaged,0,MEM_RELEASE);
    for(unsigned i=0;i<70;++i)Require(!TryUnsupported(unsupported.address,provisional),"diagnostic overflow control");
    unsigned reports=0;
    while(backend.Pop(diagnostic)) { Require(diagnostic.error==Error::UnsupportedInstruction,"diagnostic identity");++reports; }
    Require(reports==64 && backend.dropped()==6,"diagnostics remain bounded on overflow");
    domain->Close();Require(!domain->CanUnload(),"promoted view retains owner");
    Check(arena.Retire(provisional));Require(domain->CanUnload(),"retirement releases domain");
    Require(!backend.Pop(diagnostic),"no leftover diagnostics");
    RemoveVectoredExceptionHandler(observer);
    std::printf("PASS Windows views: %llu handled, worker/unwind/SEH/foreign/retirement controls\n",static_cast<unsigned long long>(backend.handled()));
}
#endif
int main()
{
    try { Hardware(); Mappings(); CompoundMappings(); Retirement(); Rejections();
#ifdef _WIN32
        WindowsViews();
#endif
    }
    catch(const Failure& error) { std::fprintf(stderr,"FAIL contract %u\n",unsigned(error.code()));return 1; }
    catch(const std::exception& error) { std::fprintf(stderr,"FAIL %s\n",error.what());return 1; }
}
