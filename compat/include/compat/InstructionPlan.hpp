#pragma once
#include <compat/LiveMemory.hpp>
#include <array>

namespace compat
{
// Architectural register numbering (RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
// R8..R15), not CONTEXT member order. No pending register remaps/single steps.
struct MachineContext
{
    std::array<uint64_t, 16> gpr{};
    std::array<std::array<std::byte, 16>, 16> xmm{};
    uint64_t ip{}, flags{};
};
class InstructionPlan
{
public:
    enum class Op { Move, ZeroExtend, SignExtend, VectorMove, Add, Or, And, Sub,
                    Xor, Compare, Test, Inc, Dec, Exchange, Xadd, CompareExchange, ConditionalMove };
    static Error Decode(std::span<const std::byte> code, InstructionPlan& output) noexcept;
    uintptr_t Address(const MachineContext& context) const noexcept;
    Error Execute(LiveMemory& memory, MachineContext& context, uintptr_t fault, bool writeFault) const noexcept;
    size_t length() const noexcept { return length_; }
    size_t width() const noexcept { return width_; }
private:
    Op op_{};
    uint8_t length_{}, width_{}, register_width_{}, reg_{}, base_{}, index_{}, scale_{}, condition_{};
    bool has_base_{}, has_index_{}, rip_relative_{}, store_{}, immediate_{}, high_byte_{}, locked_{}, aligned_{};
    bool memory_left_{};
    uint64_t immediate_value_{};
    int64_t displacement_{};
};
}
