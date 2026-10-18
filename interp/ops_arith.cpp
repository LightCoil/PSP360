// interp/ops_arith.cpp — арифметика и перемещение регистров
#include "interpreter.h"
#include "../core/logger.h"

namespace ppsspp {
namespace interp {

using core::Decoded;

namespace {
// Если исключение возникло в слоте задержки, EPC обязан указывать на
// инструкцию ветвления, а не на инструкцию в слоте.
inline uint32_t ExceptionEPC(const core::CPUState& cpu, uint32_t pc) {
    return cpu.InDelaySlot() ? (pc - 4u) : pc;
}
} // namespace

// Регистрирует архитектурное исключение Arithmetic Overflow.
// Ранее здесь ставился PC_HALTED, то есть переполнение останавливало
// весь эмулятор; это нарушало и MIPS32, и принцип проекта «эмулятор
// не должен падать на незнакомой ситуации».
void Interpreter::RaiseOverflow(const Decoded& d, int32_t a, int32_t b) {
    (void)a; (void)b;
    cpu_.RaiseException(core::ExcCode::Ov, ExceptionEPC(cpu_, d.pc),
                        cpu_.InDelaySlot());
}

bool Interpreter::ExecArith(Decoded& d, StepResult& r) {
    core::CPUState& cpu = cpu_;
    const int32_t simm = static_cast<int16_t>(d.imm);

    // R-тип
    if (d.opcode == core::Op::SPECIAL) {
        switch (d.funct) {
        case core::Funct::ADD:
        case core::Funct::ADDU: {
            // Переполнение возникает только у ADD: ADDU определён так, что
            // переполнение не возбуждает исключение.
            const int32_t a = static_cast<int32_t>(cpu.Reg(d.rs));
            const int32_t b = static_cast<int32_t>(cpu.Reg(d.rt));
            const int32_t s = static_cast<int32_t>(
                cpu.Reg(d.rs) + cpu.Reg(d.rt));
            const bool ovf = (d.funct == core::Funct::ADD) &&
                          (((a ^ s) & (b ^ s)) < 0);

            if (ovf) {
                // По MIPS32 результат в rd НЕ записывается, а возбуждается
                // исключение Arithmetic Overflow (ExcCode 12).
                LOG_WARN("Переполнение ADD в PC=0x%08X", d.pc);
                RaiseOverflow(d, a, b);
                r = StepResult::Exception;
                return true;
            }
            if (d.rd != 0) cpu.SetReg(d.rd, static_cast<uint32_t>(s));
            return true;
        }
        case core::Funct::SUB:
        case core::Funct::SUBU: {
            const int32_t a = static_cast<int32_t>(cpu.Reg(d.rs));
            const int32_t b = static_cast<int32_t>(cpu.Reg(d.rt));
            const int32_t s = static_cast<int32_t>(
                cpu.Reg(d.rs) - cpu.Reg(d.rt));
            const bool ovf = (d.funct == core::Funct::SUB) &&
                          (((a ^ b) & (a ^ s)) < 0);

            if (ovf) {
                LOG_WARN("Переполнение SUB в PC=0x%08X", d.pc);
                RaiseOverflow(d, a, b);
                r = StepResult::Exception;
                return true;
            }
            if (d.rd != 0) cpu.SetReg(d.rd, static_cast<uint32_t>(s));
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
        // ADDI возбуждает переполнение, ADDIU — нет.
        const uint32_t dst = d.rt;
        const uint32_t res = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        if (d.opcode == core::Op::ADDI) {
            const int32_t a = static_cast<int32_t>(cpu.Reg(d.rs));
            const int32_t s = static_cast<int32_t>(res);
            if (((a ^ s) & (s ^ simm)) < 0) {
                LOG_WARN("Переполнение ADDI в PC=0x%08X", d.pc);
                RaiseOverflow(d, a, simm);
                r = StepResult::Exception;
                return true;
            }
        }
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