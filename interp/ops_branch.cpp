// interp/ops_branch.cpp — ветвления и переходы (с корректным слотом задержки)
//
// Механизм слота задержки: за счёт сдвига pc/nextPC в Interpreter::Step()
// инструкция в слоте задержки всегда выполняется ПОСЛЕ инструкции ветвления
// и ДО целевого адреса. Здесь мы лишь выставляем nextPC = цель.
//
// АДРЕС ВОЗВРАТА JAL/JALR берётся из адреса самой инструкции (d.pc + 8),
// а НЕ из изменяемого cpu.nextPC. Именно это делает семантику одинаковой
// для всех путей исполнения (Step, StepOne, Run): к моменту диспетчеризации
// в Step() значение nextPC уже сдвинуто до PC+8, и вычисление от него
// давало PC+12.
#include "interpreter.h"

namespace ppsspp {
namespace interp {

using core::Decoded;

bool Interpreter::ExecBranch(Decoded& d, StepResult& r) {
    core::CPUState& cpu = cpu_;

    if (d.opcode == core::Op::SPECIAL) {
        switch (d.funct) {
        case core::Funct::JR:
            cpu.nextPC = cpu.Reg(d.rs);
            r = StepResult::Jump;
            return true;
        case core::Funct::JALR: {
            // Адрес возврата — инструкция после слота задержки: d.pc + 8.
            const uint32_t link = d.pc + 8u;
            cpu.nextPC = cpu.Reg(d.rs);
            if (d.rd != 0) cpu.SetReg(d.rd, link);
            r = StepResult::Jump;
            return true;
        }
        default:
            return false;
        }
    }

    switch (d.opcode) {
    case core::Op::J:
        cpu.nextPC = d.target;
        r = StepResult::Jump;
        return true;

    case core::Op::JAL: {
        // Адрес возврата — инструкция после слота задержки: d.pc + 8.
        const uint32_t link = d.pc + 8u;
        cpu.nextPC = d.target;
        cpu.SetReg(31, link);
        r = StepResult::Jump;
        return true;
    }

    case core::Op::BEQ:
        if (cpu.Reg(d.rs) == cpu.Reg(d.rt)) {
            cpu.nextPC = d.branchTarget;
            r = StepResult::Branch;
        }
        return true;

    case core::Op::BNE:
        if (cpu.Reg(d.rs) != cpu.Reg(d.rt)) {
            cpu.nextPC = d.branchTarget;
            r = StepResult::Branch;
        }
        return true;

    case core::Op::BLEZ:
        if (static_cast<int32_t>(cpu.Reg(d.rs)) <= 0) {
            cpu.nextPC = d.branchTarget;
            r = StepResult::Branch;
        }
        return true;

    case core::Op::BGTZ:
        if (static_cast<int32_t>(cpu.Reg(d.rs)) > 0) {
            cpu.nextPC = d.branchTarget;
            r = StepResult::Branch;
        }
        return true;

    case core::Op::REGIMM: {
        // rt определяет условие: 0x00 BLTZ, 0x01 BGEZ
        const int32_t value = static_cast<int32_t>(cpu.Reg(d.rs));
        bool take = false;
        switch (d.rt) {
        case 0x00: take = (value < 0);  break;   // BLTZ
        case 0x01: take = (value >= 0); break;   // BGEZ
        case 0x10: take = (value < 0);  break;   // BLTZAL
        case 0x11: take = (value >= 0); break;   // BGEZAL
        default: break;
        }
        if (take) {
            cpu.nextPC = d.branchTarget;
            r = StepResult::Branch;
        }
        return true;
    }

    default:
        return false;
    }
}

} // namespace interp
} // namespace ppsspp