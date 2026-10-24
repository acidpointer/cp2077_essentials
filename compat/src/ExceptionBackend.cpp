#ifdef _WIN32
#include <compat/ExceptionBackend.hpp>
#include <compat/InstructionPlan.hpp>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>

namespace compat
{
namespace
{
std::atomic<ExceptionBackend*> backend{};
thread_local bool handling{};
LONG CALLBACK Dispatch(EXCEPTION_POINTERS* pointers)
{
    auto* current = backend.load(std::memory_order_acquire);
    return current ? current->Handle(pointers) : EXCEPTION_CONTINUE_SEARCH;
}
size_t ReadCode(uintptr_t address, std::byte* output) noexcept
{
    size_t count = 0;
    // Isolate the code probe from objects with destructors. A truncated mapping
    // is a decode rejection; never assume 15 readable bytes past an instruction.
    __try
    {
        for (; count < 15; ++count) output[count] = *reinterpret_cast<volatile const std::byte*>(address + count);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
    return count;
}
}
ExceptionBackend::ExceptionBackend(ViewArena& arena) : arena_(arena)
{
    ExceptionBackend* empty{};
    if (!backend.compare_exchange_strong(empty, this)) throw Failure(Error::Busy);
    handler_ = AddVectoredExceptionHandler(1, Dispatch);
    if (!handler_) { backend.store(nullptr); throw Failure(Error::UnsupportedAccess); }
}
ExceptionBackend::~ExceptionBackend()
{
    RemoveVectoredExceptionHandler(handler_);
    backend.store(nullptr, std::memory_order_release);
}
void ExceptionBackend::Record(const FaultDiagnostic& diagnostic) noexcept
{
    auto& cell = diagnostics_[cursor_.fetch_add(1, std::memory_order_relaxed) % diagnostics_.size()];
    unsigned free = 0;
    if (!cell.state.compare_exchange_strong(free, 1, std::memory_order_acquire))
    { dropped_.fetch_add(1, std::memory_order_relaxed); return; }
    cell.diagnostic = diagnostic;
    cell.state.store(2, std::memory_order_release);
}
bool ExceptionBackend::Pop(FaultDiagnostic& diagnostic) noexcept
{
    for (auto& cell : diagnostics_)
    {
        unsigned ready = 2;
        if (!cell.state.compare_exchange_strong(ready, 1, std::memory_order_acquire)) continue;
        diagnostic = cell.diagnostic;
        cell.state.store(0, std::memory_order_release);
        return true;
    }
    return false;
}
long ExceptionBackend::Handle(void* opaque) noexcept
{
    auto* pointers = static_cast<EXCEPTION_POINTERS*>(opaque);
    const auto& exception = *pointers->ExceptionRecord;
    if (handling || exception.ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        exception.NumberParameters < 2 || exception.ExceptionInformation[0] > 1 ||
        !arena_.Owns(exception.ExceptionInformation[1])) return EXCEPTION_CONTINUE_SEARCH;
    handling = true;
    __try { return HandleOwned(opaque); }
    __finally { handling=false; }
}
long ExceptionBackend::HandleOwned(void* opaque) noexcept
{
    auto* pointers=static_cast<EXCEPTION_POINTERS*>(opaque);
    const auto& exception=*pointers->ExceptionRecord;
    auto& ctx = *pointers->ContextRecord;
    FaultDiagnostic diagnostic{Error::None, ctx.Rip, exception.ExceptionInformation[1], {}, 0,
                               exception.ExceptionInformation[0] == 1};
    diagnostic.code_size = static_cast<uint8_t>(ReadCode(ctx.Rip, diagnostic.code.data()));
    InstructionPlan plan;
    auto error = InstructionPlan::Decode({diagnostic.code.data(), diagnostic.code_size}, plan);
    MachineContext machine{{ctx.Rax, ctx.Rcx, ctx.Rdx, ctx.Rbx, ctx.Rsp, ctx.Rbp, ctx.Rsi, ctx.Rdi,
                            ctx.R8, ctx.R9, ctx.R10, ctx.R11, ctx.R12, ctx.R13, ctx.R14, ctx.R15}, {}, ctx.Rip, ctx.EFlags};
    static_assert(sizeof(machine.xmm) == sizeof(ctx.Xmm0) * 16);
    std::memcpy(machine.xmm.data(), &ctx.Xmm0, sizeof(machine.xmm));
    if (error == Error::None) error = plan.Execute(arena_.memory(), machine, diagnostic.address, diagnostic.write);
    if (error == Error::None)
    {
        ctx.Rax = machine.gpr[0]; ctx.Rcx = machine.gpr[1]; ctx.Rdx = machine.gpr[2]; ctx.Rbx = machine.gpr[3];
        ctx.Rsp = machine.gpr[4]; ctx.Rbp = machine.gpr[5]; ctx.Rsi = machine.gpr[6]; ctx.Rdi = machine.gpr[7];
        ctx.R8 = machine.gpr[8]; ctx.R9 = machine.gpr[9]; ctx.R10 = machine.gpr[10]; ctx.R11 = machine.gpr[11];
        ctx.R12 = machine.gpr[12]; ctx.R13 = machine.gpr[13]; ctx.R14 = machine.gpr[14]; ctx.R15 = machine.gpr[15];
        ctx.Rip = machine.ip; ctx.EFlags = static_cast<DWORD>(machine.flags);
        std::memcpy(&ctx.Xmm0, machine.xmm.data(), sizeof(machine.xmm));
        handled_.fetch_add(1, std::memory_order_relaxed);
    }
    else { diagnostic.error = error; Record(diagnostic); }
    return error == Error::None ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}
}
#endif
