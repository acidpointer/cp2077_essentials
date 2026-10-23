#include "frida/frida-gum.h"
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

// A restricted proof of mechanism, not a complete x64 instruction translator.
struct Instruction { uintptr_t pc; uintptr_t next; intptr_t displacement; };
static Instruction instructions[32];
static size_t instruction_count;
static void* view;
alignas(16) static unsigned char native_object[0x30];
static volatile LONG faults, steps;
struct Pending { bool active; uintptr_t original_rcx; uintptr_t next; bool original_tf; };
static thread_local Pending pending;
static long (*increment)(void*);
static int (*reader)(void*);
struct CallbackContext { void* object; int observed; };
static BOOL CALLBACK locale_callback(LPWSTR, DWORD, LPARAM data)
{
    auto* context=reinterpret_cast<CallbackContext*>(data);
    context->observed=reader(context->object);
    return FALSE;
}

static LONG CALLBACK exception_handler(EXCEPTION_POINTERS* exception)
{
    auto* cpu=exception->ContextRecord;
    auto* record=exception->ExceptionRecord;
    if(pending.active)
    {
        if(record->ExceptionCode==EXCEPTION_SINGLE_STEP && cpu->Rip==pending.next)
        {
            cpu->Rcx=pending.original_rcx;
            if(!pending.original_tf) cpu->EFlags &= ~0x100u;
            pending.active=false;
            InterlockedIncrement(&steps);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        cpu->Rcx=pending.original_rcx;
        if(!pending.original_tf) cpu->EFlags &= ~0x100u;
        pending.active=false;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if(record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || record->NumberParameters<2 ||
        record->ExceptionInformation[0]>1 ||
        record->ExceptionInformation[1] != reinterpret_cast<uintptr_t>(view)+0x10 ||
        cpu->Rcx!=reinterpret_cast<uintptr_t>(view)) return EXCEPTION_CONTINUE_SEARCH;
    for(size_t i=0;i<instruction_count;++i)
    {
        const auto& instruction=instructions[i];
        if(cpu->Rip!=instruction.pc) continue;
        pending={true,cpu->Rcx,instruction.next,(cpu->EFlags&0x100u)!=0};
        cpu->Rcx=reinterpret_cast<uintptr_t>(native_object)+0x20-instruction.displacement;
        cpu->EFlags |= 0x100u;
        InterlockedIncrement(&faults);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool decode(csh decoder, void* function)
{
    cs_insn* decoded=nullptr;
    size_t n=cs_disasm(decoder,static_cast<const uint8_t*>(function),48,
                      reinterpret_cast<uintptr_t>(function),0,&decoded);
    for(size_t i=0;i<n;++i)
    {
        const auto& instruction=decoded[i];
        if(instruction.id==X86_INS_RET) break;
        if(!instruction.detail) continue;
        const auto& x=instruction.detail->x86;
        if(x.op_count!=2) continue;
        bool load=instruction.id==X86_INS_MOV && x.operands[0].type==X86_OP_REG &&
            x.operands[0].reg==X86_REG_EAX && x.operands[1].type==X86_OP_MEM;
        bool store=instruction.id==X86_INS_MOV && x.operands[0].type==X86_OP_MEM &&
            x.operands[1].type==X86_OP_REG && x.operands[1].reg==X86_REG_EDX;
        bool atomic=instruction.id==X86_INS_XADD && x.operands[0].type==X86_OP_MEM &&
            x.operands[1].type==X86_OP_REG && x.operands[1].reg==X86_REG_EAX;
        if(!load && !store && !atomic) continue;
        const auto& memory=x.operands[load?1:0];
        if(memory.size!=4 || memory.mem.base!=X86_REG_RCX || memory.mem.index!=X86_REG_INVALID ||
            memory.mem.segment!=X86_REG_INVALID || memory.mem.disp!=0x10) continue;
        if(instruction_count>=32) { cs_free(decoded,n); return false; }
        instructions[instruction_count++]={instruction.address,instruction.address+instruction.size,memory.mem.disp};
    }
    cs_free(decoded,n);
    return true;
}

static DWORD WINAPI worker(void* object)
{
    for(int i=0;i<10000;++i) increment(object);
    return 0;
}

static __declspec(noinline) int unrelated_av(void* inaccessible)
{
    __try { return *static_cast<volatile int*>(inaccessible); }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
    { return 1; }
}

struct Marker { int* count; __declspec(noinline) ~Marker() { ++*count; } };
static __declspec(noinline) int cpp_probe()
{
    int destroyed=0;
    try { Marker marker{&destroyed}; throw 17; }
    catch(int value) { return value==17 && destroyed==1; }
}

static __declspec(noinline) int seh_probe()
{
    __try { RaiseException(0xE1234567,0,0,nullptr); }
    __except(GetExceptionCode()==0xE1234567?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
    { return 1; }
    return 0;
}

int main()
{
    gum_init_embedded(); // Initializes the bundled Capstone allocator; no Stalker is used.
    auto module=LoadLibraryW(L"guest_fields.dll");
    if(!module) return 1;
    auto read=reinterpret_cast<int (*)(void*)>(GetProcAddress(module,"read_value"));
    reader=read;
    auto write=reinterpret_cast<void (*)(void*,int)>(GetProcAddress(module,"write_value"));
    increment=reinterpret_cast<long (*)(void*)>(GetProcAddress(module,"atomic_increment"));
    if(!read || !write || !increment) return 2;
    csh decoder;
    if(cs_open(CS_ARCH_X86,CS_MODE_64,&decoder)!=CS_ERR_OK) return 3;
    cs_option(decoder,CS_OPT_DETAIL,CS_OPT_ON);
    bool decoded=decode(decoder,reinterpret_cast<void*>(read)) &&
        decode(decoder,reinterpret_cast<void*>(write)) && decode(decoder,reinterpret_cast<void*>(increment));
    cs_close(&decoder);
    if(!decoded || instruction_count!=3) return 4;
    view=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    if(!view) return 5;
    auto handler=AddVectoredExceptionHandler(1,exception_handler);
    if(!handler) return 6;
    int current=42;
    std::memcpy(native_object+0x20,&current,4);
    int legacy_sentinel=7;
    std::memcpy(native_object+0x10,&legacy_sentinel,4);
    int raw_bypass=read(native_object);
    std::printf("BYPASS raw=%d current=%d faults=%ld\n",raw_bypass,current,faults);
    if(raw_bypass!=7 || raw_bypass==current || faults!=0) return 12;
    int first=read(view);
    write(view,99);
    int second=read(view);
    CallbackContext callback{view,-1};
    EnumSystemLocalesEx(locale_callback,LOCALE_ALL,reinterpret_cast<LPARAM>(&callback),nullptr);
    auto unrelated=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    int caught=seh_probe(), native_av=unrelated_av(unrelated), cpp=cpp_probe();
    VirtualFree(unrelated,0,MEM_RELEASE);
    std::printf("FIELDS first=%d second=%d callback=%d faults=%ld steps=%ld SEH=%d unrelated_AV=%d CXX=%d\n",first,second,callback.observed,faults,steps,caught,native_av,cpp);
    std::fflush(stdout);
    if(first!=42 || second!=99 || callback.observed!=99 || caught!=1 || native_av!=1 || cpp!=1 || faults!=4 || steps!=4) return 7;
    current=0; std::memcpy(native_object+0x20,&current,4);
    LARGE_INTEGER start,end,frequency;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
    HANDLE baseline[2]={CreateThread(nullptr,0,worker,native_object+0x10,0,nullptr),
                        CreateThread(nullptr,0,worker,native_object+0x10,0,nullptr)};
    if(!baseline[0] || !baseline[1]) return 10;
    WaitForMultipleObjects(2,baseline,TRUE,INFINITE);
    QueryPerformanceCounter(&end);
    CloseHandle(baseline[0]); CloseHandle(baseline[1]);
    std::memcpy(&current,native_object+0x20,4);
    std::printf("BASELINE native=%d elapsed_ms=%.3f\n",current,
        1000.0*static_cast<double>(end.QuadPart-start.QuadPart)/static_cast<double>(frequency.QuadPart));
    if(current!=20000) return 11;
    current=0; std::memcpy(native_object+0x20,&current,4);
    QueryPerformanceCounter(&start);
    HANDLE threads[2]={CreateThread(nullptr,0,worker,view,0,nullptr),CreateThread(nullptr,0,worker,view,0,nullptr)};
    if(!threads[0] || !threads[1]) return 8;
    WaitForMultipleObjects(2,threads,TRUE,INFINITE);
    QueryPerformanceCounter(&end);
    CloseHandle(threads[0]); CloseHandle(threads[1]);
    std::memcpy(&current,native_object+0x20,4);
    std::printf("ATOMIC native=%d faults=%ld steps=%ld elapsed_ms=%.3f\n",current,faults,steps,
        1000.0*static_cast<double>(end.QuadPart-start.QuadPart)/static_cast<double>(frequency.QuadPart));
    std::fflush(stdout);
    bool pass=current==20000 && faults==20004 && steps==20004 && seh_probe()==1;
    RemoveVectoredExceptionHandler(handler);
    VirtualFree(view,0,MEM_RELEASE);
    FreeLibrary(module);
    gum_deinit_embedded();
    return pass?0:9;
}
