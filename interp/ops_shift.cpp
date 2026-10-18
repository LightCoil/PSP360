// interp/ops_shift.cpp — сдвиги, SYSCALL, BREAK
#include "interpreter.h"
#include "../core/logger.h"

namespace ppsspp {
namespace interp {

using core::Decoded;

bool Interpreter::ExecShift(Decoded& d, StepResult& r) {
    core::CPUState& cpu = cpu_;

    if (d.opcode == core::Op::SPECIAL) {
        switch (d.funct) {
        case core::Funct::SLL:
            // NOP-форма (sll $0,$0,0)
            if (d.rd != 0) cpu.SetReg(d.rd,cpu.Reg(d.rt) << d.shamt);
            return true;
        case core::Funct::SRL:
            if (d.rd != 0) cpu.SetReg(d.rd,cpu.Reg(d.rt) >> d.shamt);
            return true;
        case core::Funct::SRA:
            if (d.rd != 0) {
                cpu.SetReg(d.rd,static_cast<uint32_t>(
                    static_cast<int32_t>(cpu.Reg(d.rt)) >> d.shamt));
            }
            return true;
        case core::Funct::SLLV:
            if (d.rd != 0) {
                cpu.SetReg(d.rd,cpu.Reg(d.rt) << (cpu.Reg(d.rs) & 31));
            }
            return true;
        case core::Funct::SRLV:
            if (d.rd != 0) {
                cpu.SetReg(d.rd,cpu.Reg(d.rt) >> (cpu.Reg(d.rs) & 31));
            }
            return true;
        case core::Funct::SRAV:
            if (d.rd != 0) {
                cpu.SetReg(d.rd,static_cast<uint32_t>(
                    static_cast<int32_t>(cpu.Reg(d.rt)) >> (cpu.Reg(d.rs) & 31)));
            }
            return true;

        case core::Funct::SYSCALL:
            // Номер вызова передаётся в $v0 (gpr[2])
            LOG_DEBUG("SYSCALL 0x%08X в PC=0x%08X", cpu.Reg(2), d.pc);
            r = StepResult::Syscall;
            return true;

        case core::Funct::BREAK:
            LOG_DEBUG("BREAK в PC=0x%08X", d.pc);
            return true;

        default:
            return false;
        }
    }

    return false;
}

} // namespace interp
} // namespace ppsspp