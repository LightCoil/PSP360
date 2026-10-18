// interp/ops_logic.cpp — логические операции и загрузка непосредственного значения
#include "interpreter.h"

namespace ppsspp {
namespace interp {

using core::Decoded;

bool Interpreter::ExecLogic(Decoded& d, StepResult& r) {
    (void)r;
    core::CPUState& cpu = cpu_;

    if (d.opcode == core::Op::SPECIAL) {
        const uint32_t dst = d.rd;
        switch (d.funct) {
        case core::Funct::AND: if (dst) cpu.SetReg(dst,cpu.Reg(d.rs) & cpu.Reg(d.rt)); return true;
        case core::Funct::OR:  if (dst) cpu.SetReg(dst,cpu.Reg(d.rs) | cpu.Reg(d.rt)); return true;
        case core::Funct::XOR: if (dst) cpu.SetReg(dst,cpu.Reg(d.rs) ^ cpu.Reg(d.rt)); return true;
        case core::Funct::NOR:
            if (dst) cpu.SetReg(dst,~(cpu.Reg(d.rs) | cpu.Reg(d.rt)));
            return true;
        case core::Funct::SLT:
            if (dst) {
                cpu.SetReg(dst,(static_cast<int32_t>(cpu.Reg(d.rs)) <
                                static_cast<int32_t>(cpu.Reg(d.rt))) ? 1u : 0u);
            }
            return true;
        case core::Funct::SLTU:
            if (dst) {
                cpu.SetReg(dst,(cpu.Reg(d.rs) < cpu.Reg(d.rt)) ? 1u : 0u);
            }
            return true;
        default:
            return false;
        }
    }

    switch (d.opcode) {
    case core::Op::ANDI:
        // ANDI использует непосредственное значение без знакового расширения.
        if (d.rt != 0) cpu.SetReg(d.rt,cpu.Reg(d.rs) & static_cast<uint32_t>(d.imm));
        return true;
    case core::Op::ORI:
        if (d.rt != 0) cpu.SetReg(d.rt,cpu.Reg(d.rs) | static_cast<uint32_t>(d.imm));
        return true;
    case core::Op::XORI:
        if (d.rt != 0) cpu.SetReg(d.rt,cpu.Reg(d.rs) ^ static_cast<uint32_t>(d.imm));
        return true;
    case core::Op::LUI:
        if (d.rt != 0) cpu.SetReg(d.rt,static_cast<uint32_t>(d.imm) << 16);
        return true;
    default:
        return false;
    }
}

} // namespace interp
} // namespace ppsspp