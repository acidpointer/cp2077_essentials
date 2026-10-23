#include "frida/frida-gum.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

struct Mapping { void* guest; void* host; size_t guest_size; size_t old_offset; size_t new_offset; };
struct Operation { bool write; intptr_t displacement; };
static Mapping mapping;
static GumMemoryRange guest_code;
static volatile LONG translated = 0;
static volatile LONG entered = 0;
static thread_local uintptr_t saved_rcx;
static GumStalker* active_stalker;
static GumStalkerTransformer* active_transformer;
static long (*increment)(void*);

static void remap(GumCpuContext* cpu, gpointer data)
{
    auto* operation = static_cast<Operation*>(data);
    saved_rcx = cpu->rcx;
    if (cpu->rcx == reinterpret_cast<uintptr_t>(mapping.guest) &&
        operation->displacement == mapping.old_offset)
    {
        cpu->rcx = reinterpret_cast<uintptr_t>(mapping.host) + mapping.new_offset - operation->displacement;
        InterlockedIncrement(&translated);
    }
}
static void restore(GumCpuContext* cpu, gpointer) { cpu->rcx = saved_rcx; }

static void apply(GumCpuContext* cpu, gpointer data)
{
    auto* operation = static_cast<Operation*>(data);
    uintptr_t address = cpu->rcx + operation->displacement;
    auto begin = reinterpret_cast<uintptr_t>(mapping.guest);
    if (cpu->rcx == begin && operation->displacement == mapping.old_offset)
    {
        address = reinterpret_cast<uintptr_t>(mapping.host) + mapping.new_offset;
        InterlockedIncrement(&translated);
    }
    if (operation->write)
    {
        uint32_t value = static_cast<uint32_t>(cpu->rdx);
        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
    }
    else
    {
        uint32_t value;
        std::memcpy(&value, reinterpret_cast<void*>(address), sizeof(value));
        cpu->rax = value;
    }
}

static void count(GumCpuContext*, gpointer) { InterlockedIncrement(&entered); }

static void transform(GumStalkerIterator* iterator, GumStalkerOutput*, gpointer data)
{
    const cs_insn* instruction;
    const bool translate = data != nullptr;
    while (gum_stalker_iterator_next(iterator, &instruction))
    {
        bool replaced = false;
        if (instruction->address >= guest_code.base_address &&
            instruction->address < guest_code.base_address + guest_code.size)
        {
            gum_stalker_iterator_put_callout(iterator, count, nullptr, nullptr);
            if (translate && instruction->id == X86_INS_XADD && instruction->detail)
            {
                auto& x = instruction->detail->x86;
                if (x.op_count == 2 && x.operands[0].type == X86_OP_MEM &&
                    x.operands[0].size == 4 && x.operands[0].mem.base == X86_REG_RCX &&
                    x.operands[0].mem.index == X86_REG_INVALID &&
                    x.operands[0].mem.segment == X86_REG_INVALID &&
                    x.operands[1].type == X86_OP_REG && x.operands[1].reg == X86_REG_EAX)
                {
                    auto* operation = new Operation{true,x.operands[0].mem.disp};
                    gum_stalker_iterator_put_callout(iterator,remap,operation,
                        [](gpointer p) { delete static_cast<Operation*>(p); });
                    gum_stalker_iterator_keep(iterator);
                    gum_stalker_iterator_put_callout(iterator,restore,nullptr,nullptr);
                    replaced = true;
                }
            }
            if (translate && instruction->id == X86_INS_MOV && instruction->detail)
            {
                auto& x = instruction->detail->x86;
                if (x.op_count == 2)
                {
                    bool load = x.operands[0].type == X86_OP_REG && x.operands[0].reg == X86_REG_EAX &&
                        x.operands[1].type == X86_OP_MEM;
                    bool store = x.operands[1].type == X86_OP_REG && x.operands[1].reg == X86_REG_EDX &&
                        x.operands[0].type == X86_OP_MEM;
                    if (load || store)
                    {
                        auto& memory = x.operands[load ? 1 : 0];
                        if (memory.size == 4 && memory.mem.base == X86_REG_RCX &&
                            memory.mem.index == X86_REG_INVALID && memory.mem.segment == X86_REG_INVALID)
                        {
                            auto* operation = new Operation{store, memory.mem.disp};
                            gum_stalker_iterator_put_callout(iterator, apply, operation,
                                [](gpointer p) { delete static_cast<Operation*>(p); });
                            replaced = true;
                        }
                    }
                }
            }
        }
        if (!replaced) gum_stalker_iterator_keep(iterator);
    }
}

static DWORD WINAPI worker(void*)
{
    gum_stalker_follow_me(active_stalker,active_transformer,nullptr);
    for(int i=0;i<10000;++i) increment(mapping.guest);
    gum_stalker_unfollow_me(active_stalker);
    return 0;
}

static int seh_probe()
{
    __try { RaiseException(0xE1234567, 0, 0, nullptr); }
    __except(GetExceptionCode() == 0xE1234567 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return 1; }
    return 0;
}

int main(int argc, char** argv)
{
    std::puts("before Gum init"); std::fflush(stdout);
    gum_init_embedded();
    std::printf("Gum initialized; Stalker supported=%d\n", gum_stalker_is_supported());
    std::fflush(stdout);
    if (argc < 2 || std::strcmp(argv[1], "--init") == 0) { gum_deinit_embedded(); return 0; }
    if (std::strcmp(argv[1], "--seh-native") == 0)
    {
        int caught = seh_probe();
        std::printf("native SEH caught=%d\n", caught); std::fflush(stdout);
        gum_deinit_embedded();
        return caught == 1 ? 0 : 14;
    }
    auto module = LoadLibraryW(L"guest_fields.dll");
    if (!module) return 10;
    auto read = reinterpret_cast<int (*)(void*)>(GetProcAddress(module, "read_value"));
    auto write = reinterpret_cast<void (*)(void*,int)>(GetProcAddress(module, "write_value"));
    if (!read || !write) return 11;
    auto* info = gum_process_find_module_by_name("guest_fields.dll");
    if (!info) return 12;
    guest_code = *gum_module_get_range(info);
    g_object_unref(info);
    alignas(16) unsigned char guest[0x20] = {};
    alignas(16) unsigned char host[0x30] = {};
    int old_value=7, current_value=42;
    std::memcpy(guest+0x10,&old_value,4);
    std::memcpy(host+0x20,&current_value,4);
    mapping = {guest,host,sizeof(guest),0x10,0x20};
    bool threads = std::strcmp(argv[1], "--threads") == 0;
    bool translate = std::strcmp(argv[1], "--translate") == 0 || threads;
    bool exception = std::strcmp(argv[1], "--seh") == 0;
    auto* stalker = gum_stalker_new();
    auto* transformer = gum_stalker_transformer_make_from_callback(transform,
        translate ? reinterpret_cast<void*>(1) : nullptr, nullptr);
    if (threads)
    {
        increment = reinterpret_cast<long (*)(void*)>(GetProcAddress(module,"atomic_increment"));
        if(!increment) return 15;
        active_stalker=stalker; active_transformer=transformer;
        current_value=0; std::memcpy(host+0x20,&current_value,4);
        HANDLE workers[2]={CreateThread(nullptr,0,worker,nullptr,0,nullptr),
                           CreateThread(nullptr,0,worker,nullptr,0,nullptr)};
        if(!workers[0] || !workers[1]) return 16;
        WaitForMultipleObjects(2,workers,TRUE,INFINITE);
        CloseHandle(workers[0]); CloseHandle(workers[1]);
        std::memcpy(&current_value,host+0x20,4);
        std::printf("THREADS native_count=%d translated=%ld entered=%ld\n",current_value,translated,entered);
        std::fflush(stdout);
        g_object_unref(transformer);
        while(gum_stalker_garbage_collect(stalker)) Sleep(1);
        g_object_unref(stalker); FreeLibrary(module); gum_deinit_embedded();
        return current_value==20000 && translated==20000 ? 0 : 17;
    }
    gum_stalker_follow_me(stalker,transformer,nullptr);
    int first = read(guest);
    write(guest,99);
    int second = read(guest);
    int caught = exception ? seh_probe() : 1;
    gum_stalker_unfollow_me(stalker);
    gum_stalker_flush(stalker);
    int observed;
    std::memcpy(&observed,host+0x20,4);
    std::printf("RESULT first=%d second=%d host=%d entered=%ld translated=%ld seh=%d\n",
        first,second,observed,entered,translated,caught);
    std::fflush(stdout);
    bool pass=first==(translate?42:7) && second==99 && observed==(translate?99:42) &&
        entered>0 && translated==(translate?3:0) && caught==1;
    g_object_unref(transformer);
    while(gum_stalker_garbage_collect(stalker)) Sleep(1);
    g_object_unref(stalker);
    FreeLibrary(module);
    gum_deinit_embedded();
    return pass?0:13;
}
