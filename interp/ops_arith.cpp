// interp/ops_arith.cpp — арифметика и перемещение регистров
#include "interpreter.h"
#include "../core/logger.h"

namespace ppsspp {
namespace interp {

using core::Decoded;

bool Interpreter::ExecArith(Decoded& d, StepResult& r) {
    core::CPUState& cpu = cpu_;
    const int32_t simm = static_cast<int16_t>(d.imm);

    // R-тип
    if (d.opcode == core::Op::SPECIAL) {
        switch (d.funct) {
        case core::Funct::ADD:
        case core::Funct::ADDU: {
            // Регистр $0 всегда равен нулю.
            const uint32_t dst = d.rd;
            const uint32_t res = cpu.Reg(d.rs) + cpu.Reg(d.rt);
            if (dst != 0) cpu.SetReg(dst,res);
            // Арифметическое переполнение для ADD
            if (d.funct == core::Funct::ADD) {
                const int32_t a = static_cast<int32_t>(cpu.Reg(d.rs));
                const int32_t b = static_cast<int32_t>(cpu.Reg(d.rt));
                const int32_t s = static_cast<int32_t>(res);
                if (((a ^ s) & (b ^ s)) < 0) {
                    LOG_WARN("Переполнение ADD в PC=0x%08X", d.pc);
                    cpu.pc = core::PC_HALTED;
                    r = StepResult::Halted;
                }
            }
            return true;
        }
        case core::Funct::SUB:
        case core::Funct::SUBU: {
            const uint32_t dst = d.rd;
            const uint32_t res = cpu.Reg(d.rs) - cpu.Reg(d.rt);
            if (dst != 0) cpu.SetReg(dst,res);
            return true;
        }
        case core::Funct::MFHI: if (d.rd != 0) cpu.SetReg(d.rd,cpu.hi); return true;
        case core::Funct::MTHI: cpu.hi = cpu.Reg(d.rs); return true;
        case core::Funct::MFLO: if (d.rd != 0) cpu.SetReg(d.rd,cpu.lo); return true;
        case core::Funct::MTLO: cpu.lo = cpu.Reg(d.rs); return true;

        // Условное перемещение
        case core::Funct::MOVZ:
            if (cpu.Reg(d.rt) == 0 && d.rd != 0) cpu.SetReg(d.rd,cpu.Reg(d.rs));
            return true;
        case core::Funct::MOVN:
            if (cpu.Reg(d.rt) != 0 && d.rd != 0) cpu.SetReg(d.rd,cpu.Reg(d.rs));
            return true;
        case core::Funct::SYNC:
            return true;  //nop на однопоточном исполнении

        default:
            return false;
        }
    }

    // I-тип
    switch (d.opcode) {
    case core::Op::ADDI:
    case core::Op::ADDIU: {
        const uint32_t dst = d.rt;
        const uint32_t res = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        if (dst != 0) cpu.SetReg(dst,res);
        return true;
    }
    case core::Op::SLTI:
        if (d.rt != 0) {
            cpu.SetReg(d.rt,(static_cast<int32_t>(cpu.Reg(d.rs)) < simm) ? 1u : 0u);
        }
        return true;
    case core::Op::SLTIU:
        if (d.rt != 0) {
            cpu.SetReg(d.rt,(cpu.Reg(d.rs) < static_cast<uint32_t>(simm)) ? 1u : 0u);
        }
        return true;
    default:
        return false;
    }
}

bool Interpreter::ExecMultDiv(Decoded& d, StepResult& r) {
    core::CPUState& cpu = cpu_;
    (void)r;

    switch (d.funct) {
    case core::Funct::MULT: {
        const int64_t res = static_cast<int64_t>(static_cast<int32_t>(cpu.Reg(d.rs))) *
                            static_cast<int64_t>(static_cast<int32_t>(cpu.Reg(d.rt)));
        cpu.lo = static_cast<uint32_t>(res);
        cpu.hi = static_cast<uint32_t>(static_cast<uint64_t>(res) >> 32);
        return true;
    }
    case core::Funct::MULTU: {
        const uint64_t res = static_cast<uint64_t>(cpu.Reg(d.rs)) *
                             static_cast<uint64_t>(cpu.Reg(d.rt));
        cpu.lo = static_cast<uint32_t>(res);
        cpu.hi = static_cast<uint32_t>(res >> 32);
        return true;
    }
    case core::Funct::DIV: {
        const int32_t a = static_cast<int32_t>(cpu.Reg(d.rs));
        const int32_t b = static_cast<int32_t>(cpu.Reg(d.rt));
        if (b == 0) {
            // Деление на ноль: результат не определён, но выполнение
            // продолжается (аппаратное поведение Allegrex не генерирует исключение).
            cpu.lo = 0;
            cpu.hi = 0;
        } else if (a == INT32_MIN && b == -1) {
            cpu.lo = static_cast<uint32_t>(a);
            cpu.hi = 0;
        } else {
            cpu.lo = static_cast<uint32_t>(a / b);
            cpu.hi = static_cast<uint32_t>(a % b);
        }
        return true;
    }
    case core::Funct::DIVU: {
        const uint32_t a = cpu.Reg(d.rs);
        const uint32_t b = cpu.Reg(d.rt);
        if (b == 0) {
            cpu.lo = 0;
            cpu.hi = 0;
        } else {
            cpu.lo = a / b;
            cpu.hi = a % b;
        }
        return true;
    }
    default:
        return false;
    }
}

} // namespace interp
} // namespace ppsspp