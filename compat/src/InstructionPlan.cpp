#include <compat/InstructionPlan.hpp>
#include <bit>
#include <cstring>

namespace compat
{
namespace
{
struct Reader
{
    std::span<const std::byte> code;
    size_t offset{};
    bool valid{true};
    uint8_t Byte() noexcept
    {
        if (offset >= code.size() || offset >= 15) { valid = false; return 0; }
        return std::to_integer<uint8_t>(code[offset++]);
    }
    uint64_t Number(size_t count) noexcept
    { uint64_t value{}; for (size_t i = 0; i < count; ++i) value |= uint64_t{Byte()} << (i * 8); return value; }
};
uint64_t Mask(size_t width) noexcept { return width == 8 ? UINT64_MAX : (uint64_t{1} << (width * 8)) - 1; }
uint64_t SignExtend(uint64_t value, size_t width) noexcept
{ const auto sign = uint64_t{1} << (width * 8 - 1); return (value ^ sign) - sign; }
constexpr uint64_t CF = 1, PF = 4, AF = 16, ZF = 64, SF = 128, OF = 2048;
uint64_t Flags(uint64_t flags, uint64_t left, uint64_t right, uint64_t result, size_t width, bool sub, bool logical) noexcept
{
    const auto mask = Mask(width), sign = uint64_t{1} << (width * 8 - 1);
    left &= mask; right &= mask; result &= mask;
    auto out = flags & ~(CF | PF | AF | ZF | SF | OF);
    if (!result) out |= ZF;
    if (result & sign) out |= SF;
    if (!(std::popcount(static_cast<uint8_t>(result)) & 1)) out |= PF;
    if (!logical)
    {
        if (sub ? left < right : result < left) out |= CF;
        if ((left ^ right ^ result) & 16) out |= AF;
        if ((sub ? (left ^ right) : ~(left ^ right)) & (left ^ result) & sign) out |= OF;
    }
    // AF is architecturally undefined for logical operations; choose zero.
    return out;
}
bool Condition(unsigned condition, uint64_t flags) noexcept
{
    const bool c = flags & CF, z = flags & ZF, s = flags & SF, o = flags & OF, p = flags & PF;
    const bool values[]{o, !o, c, !c, z, !z, c || z, !c && !z, s, !s, p, !p, s != o, s == o, z || s != o, !z && s == o};
    return values[condition];
}
}

Error InstructionPlan::Decode(std::span<const std::byte> code, InstructionPlan& output) noexcept
{
    Reader reader{code}; InstructionPlan plan;
    uint8_t rex{}, repeat{}; bool operand16{};
    auto opcode = reader.Byte();
    // Deliberately reject address-size/segment overrides, VEX/EVEX, duplicate
    // prefixes and any form without reviewed execution semantics.
    while (opcode == 0x66 || opcode == 0xF0 || opcode == 0xF2 || opcode == 0xF3)
    {
        if (opcode == 0x66) { if (operand16) return Error::UnsupportedInstruction; operand16 = true; }
        else if (opcode == 0xF0) { if (plan.locked_) return Error::UnsupportedInstruction; plan.locked_ = true; }
        else { if (repeat) return Error::UnsupportedInstruction; repeat = opcode; }
        opcode = reader.Byte();
    }
    if ((opcode & 0xF0) == 0x40) { rex = opcode; opcode = reader.Byte(); }
    plan.width_ = plan.register_width_ = rex & 8 ? 8 : operand16 ? 2 : 4;
    bool group{}, vector{}, byte{};
    size_t immediateWidth{};
    if (opcode == 0x0F)
    {
        opcode = reader.Byte();
        switch (opcode)
        {
        case 0xB6: case 0xB7: case 0xBE: case 0xBF:
            plan.op_ = opcode < 0xBE ? Op::ZeroExtend : Op::SignExtend;
            plan.width_ = (opcode & 1) ? 2 : 1; break;
        case 0xC0: case 0xC1: plan.op_ = Op::Xadd; plan.store_ = true; byte = opcode == 0xC0; break;
        case 0xB0: case 0xB1: plan.op_ = Op::CompareExchange; plan.store_ = true; byte = opcode == 0xB0; break;
        case 0x10: case 0x11: case 0x28: case 0x29: case 0x6F: case 0x7F:
            vector = true; plan.op_ = Op::VectorMove;
            plan.store_ = opcode == 0x11 || opcode == 0x29 || opcode == 0x7F;
            if (opcode == 0x10 || opcode == 0x11)
                plan.width_ = repeat == 0xF3 ? 4 : repeat == 0xF2 ? 8 : 16;
            else
            {
                if ((opcode == 0x6F || opcode == 0x7F) && !operand16 && repeat != 0xF3)
                    return Error::UnsupportedInstruction; // MMX is a distinct register file.
                if (repeat && (opcode == 0x28 || opcode == 0x29 || repeat != 0xF3)) return Error::UnsupportedInstruction;
                plan.width_ = 16;
            }
            plan.aligned_ = opcode == 0x28 || opcode == 0x29 || ((opcode == 0x6F || opcode == 0x7F) && operand16);
            break;
        default:
            if (opcode < 0x40 || opcode > 0x4F) return Error::UnsupportedInstruction;
            plan.op_ = Op::ConditionalMove; plan.condition_ = opcode & 15;
        }
    }
    else switch (opcode)
    {
    case 0x88: case 0x89: case 0x8A: case 0x8B:
        plan.op_ = Op::Move; plan.store_ = !(opcode & 2); byte = !(opcode & 1); break;
    case 0xC6: case 0xC7:
        plan.op_ = Op::Move; plan.store_ = plan.immediate_ = true; byte = opcode == 0xC6;
        immediateWidth = byte ? 1 : operand16 && !(rex & 8) ? 2 : 4; group = true; break;
    case 0x63:
        if (!(rex & 8)) return Error::UnsupportedInstruction;
        plan.op_ = Op::SignExtend; plan.width_ = 4; break;
    case 0x80: case 0x81: case 0x83:
        group = plan.immediate_ = plan.store_ = true; byte = opcode == 0x80;
        immediateWidth = opcode == 0x81 ? (operand16 && !(rex & 8) ? 2 : 4) : 1; break;
    case 0x84: case 0x85: plan.op_ = Op::Test; byte = opcode == 0x84; break;
    case 0x86: case 0x87: plan.op_ = Op::Exchange; plan.store_ = true; plan.locked_ = true; byte = opcode == 0x86; break;
    case 0xFE: case 0xFF: group = plan.store_ = true; byte = opcode == 0xFE; break;
    case 0xF6: case 0xF7: group = plan.immediate_ = true; byte = opcode == 0xF6;
        immediateWidth = byte ? 1 : operand16 && !(rex & 8) ? 2 : 4; plan.op_ = Op::Test; break;
    default:
        if (opcode > 0x3B || (opcode & 7) > 3 || (opcode & 0xF8) == 0x10 || (opcode & 0xF8) == 0x18)
            return Error::UnsupportedInstruction;
        const Op operations[]{Op::Add, Op::Or, Op::Add, Op::Add, Op::And, Op::Sub, Op::Xor, Op::Compare};
        plan.op_ = operations[opcode >> 3]; plan.store_ = !(opcode & 2); byte = !(opcode & 1);
    }
    if (repeat && !vector) return Error::UnsupportedInstruction;
    if (vector && (rex & 8)) return Error::UnsupportedInstruction;
    if (vector && repeat && operand16) return Error::UnsupportedInstruction;
    if (byte) plan.width_ = plan.register_width_ = 1;
    const auto modrm = reader.Byte();
    const auto mod = modrm >> 6, reg = (modrm >> 3) & 7, rm = modrm & 7;
    if (mod == 3) return Error::UnsupportedInstruction;
    plan.reg_ = static_cast<uint8_t>(reg | ((rex & 4) ? 8 : 0));
    if (group)
    {
        if (rex & 4) return Error::UnsupportedInstruction;
        if (opcode == 0x80 || opcode == 0x81 || opcode == 0x83)
        {
            const Op operations[]{Op::Add, Op::Or, Op::Add, Op::Add, Op::And, Op::Sub, Op::Xor, Op::Compare};
            if (reg == 2 || reg == 3) return Error::UnsupportedInstruction;
            plan.op_ = operations[reg];
        }
        else if (opcode == 0xFE || opcode == 0xFF)
        { if (reg > 1) return Error::UnsupportedInstruction; plan.op_ = reg ? Op::Dec : Op::Inc; }
        else if (reg) return Error::UnsupportedInstruction;
    }
    if (plan.register_width_ == 1 && !rex && reg >= 4 && !group)
    { plan.high_byte_ = true; plan.reg_ -= 4; }
    if (rm == 4)
    {
        const auto sib = reader.Byte();
        plan.scale_ = sib >> 6;
        plan.index_ = ((sib >> 3) & 7) | ((rex & 2) ? 8 : 0);
        plan.has_index_ = plan.index_ != 4;
        plan.base_ = (sib & 7) | ((rex & 1) ? 8 : 0);
        plan.has_base_ = mod != 0 || (sib & 7) != 5;
        if (!plan.has_base_) plan.displacement_ = static_cast<int32_t>(reader.Number(4));
    }
    else if (mod == 0 && rm == 5)
    { plan.rip_relative_ = true; plan.displacement_ = static_cast<int32_t>(reader.Number(4)); }
    else { plan.has_base_ = true; plan.base_ = static_cast<uint8_t>(rm | ((rex & 1) ? 8 : 0)); }
    if (mod == 1) plan.displacement_ = static_cast<int8_t>(reader.Byte());
    if (mod == 2) plan.displacement_ = static_cast<int32_t>(reader.Number(4));
    if (immediateWidth)
    {
        plan.immediate_value_ = reader.Number(immediateWidth);
        if (opcode == 0x83 || (plan.width_ == 8 && immediateWidth == 4))
            plan.immediate_value_ = SignExtend(plan.immediate_value_, immediateWidth);
    }
    plan.memory_left_ = plan.store_ || plan.op_ == Op::Test;
    if (plan.op_ == Op::Compare || plan.op_ == Op::Test) plan.store_ = false;
    if (plan.locked_ && (!plan.store_ || plan.op_ == Op::Move || vector)) return Error::UnsupportedInstruction;
    if (!reader.valid || reader.offset > 15) return Error::UnsupportedInstruction;
    plan.length_ = static_cast<uint8_t>(reader.offset); output = plan;
    return Error::None;
}
uintptr_t InstructionPlan::Address(const MachineContext& context) const noexcept
{
    return (has_base_ ? context.gpr[base_] : rip_relative_ ? context.ip + length_ : 0) +
        (has_index_ ? context.gpr[index_] << scale_ : 0) + static_cast<uint64_t>(displacement_);
}
Error InstructionPlan::Execute(LiveMemory& memory, MachineContext& context, uintptr_t fault, bool writeFault) const noexcept
{
    const auto address = Address(context);
    if (!length_ || fault < address || fault - address >= width_ || (writeFault && !store_) ||
        (aligned_ && address % 16) || (context.flags & 0x100u)) return Error::UnsupportedAccess;
    auto result = context;
    const auto regValue = [&] { return context.gpr[reg_] >> (high_byte_ ? 8 : 0); };
    const auto setReg = [&](uint64_t value) {
        auto& reg = result.gpr[reg_];
        if (register_width_ == 8) reg = value;
        else if (register_width_ == 4) reg = static_cast<uint32_t>(value);
        else { const auto mask = Mask(register_width_) << (high_byte_ ? 8 : 0);
               reg = (reg & ~mask) | ((value << (high_byte_ ? 8 : 0)) & mask); }
    };
    if (op_ == Op::VectorMove)
    {
        if (!store_ && width_ < 16) result.xmm[reg_].fill(std::byte{}); // Memory MOVSS/MOVSD clears the remaining XMM bits.
        const auto error = store_ ? memory.Write(address, {context.xmm[reg_].data(), width_}) :
                                   memory.Read(address, {result.xmm[reg_].data(), width_});
        if (error != Error::None) return error;
    }
    else
    {
        uint64_t old{}, value = (immediate_ ? immediate_value_ : regValue()) & Mask(width_);
        auto bytes = std::span(reinterpret_cast<std::byte*>(&old), width_);
        if (op_ == Op::Move && store_)
        { if (auto error = memory.Write(address, {reinterpret_cast<const std::byte*>(&value), width_}); error != Error::None) return error; }
        else if (op_ == Op::Move || op_ == Op::ZeroExtend || op_ == Op::SignExtend || op_ == Op::ConditionalMove)
        {
            if (auto error = memory.Read(address, bytes); error != Error::None) return error;
            if (op_ != Op::ConditionalMove || Condition(condition_, context.flags))
                setReg(op_ == Op::SignExtend ? SignExtend(old, width_) : old);
            else if (register_width_ == 4) setReg(regValue()); // A false CMOV r32 still clears the upper half.
        }
        else
        {
            if (op_ == Op::Inc || op_ == Op::Dec) value = 1;
            if (locked_)
            {
                auto operation = LiveMemory::AtomicOp::Add;
                auto operand = value;
                switch (op_)
                {
                case Op::Or: operation = LiveMemory::AtomicOp::Or; break;
                case Op::And: operation = LiveMemory::AtomicOp::And; break;
                case Op::Xor: operation = LiveMemory::AtomicOp::Xor; break;
                case Op::Exchange: operation = LiveMemory::AtomicOp::Exchange; break;
                case Op::CompareExchange: operation = LiveMemory::AtomicOp::CompareExchange; break;
                case Op::Sub: case Op::Dec: operand = uint64_t{} - value; break;
                default: break;
                }
                if (auto error = memory.Atomic(address, width_, operation, operand, context.gpr[0], old); error != Error::None) return error;
            }
            else if (auto error = memory.Read(address, bytes); error != Error::None) return error;
            auto left = old, right = value;
            // Register-destination arithmetic has the opposite operand order.
            if (!memory_left_) { left = value; right = old; }
            uint64_t calculated{};
            const bool sub = op_ == Op::Sub || op_ == Op::Dec || op_ == Op::Compare || op_ == Op::CompareExchange;
            const bool logical = op_ == Op::And || op_ == Op::Or || op_ == Op::Xor || op_ == Op::Test;
            switch (op_)
            {
            case Op::Add: case Op::Inc: case Op::Xadd: calculated = left + right; break;
            case Op::Sub: case Op::Dec: case Op::Compare: calculated = left - right; break;
            case Op::And: case Op::Test: calculated = left & right; break;
            case Op::Or: calculated = left | right; break;
            case Op::Xor: calculated = left ^ right; break;
            case Op::Exchange: calculated = value; break;
            case Op::CompareExchange: left = context.gpr[0] & Mask(width_); right = old; calculated = left - old; break;
            default: return Error::UnsupportedInstruction;
            }
            if (op_ != Op::Exchange)
                result.flags = Flags(context.flags, left, right, calculated, width_, sub, logical);
            if (op_ == Op::Inc || op_ == Op::Dec) result.flags = (result.flags & ~CF) | (context.flags & CF);
            if (op_ == Op::CompareExchange)
            {
                if (left == old)
                { if (!locked_) { auto error = memory.Write(address, {reinterpret_cast<const std::byte*>(&value), width_}); if (error != Error::None) return error; } }
                else if (width_ == 4) result.gpr[0] = static_cast<uint32_t>(old);
                else result.gpr[0] = (context.gpr[0] & ~Mask(width_)) | old;
            }
            else if (op_ != Op::Compare && op_ != Op::Test)
            {
                if (store_)
                { if (!locked_) { auto error = memory.Write(address, {reinterpret_cast<const std::byte*>(&calculated), width_}); if (error != Error::None) return error; } }
                else setReg(calculated);
                if (op_ == Op::Xadd || op_ == Op::Exchange) setReg(old);
            }
        }
    }
    result.ip += length_; context = result;
    return Error::None;
}
}
