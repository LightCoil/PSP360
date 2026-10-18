// interp/ops_cop.cpp — сопроцессоры COP0/COP1/COP2 (VFPU)/COP3
#include "interpreter.h"
#include "../core/logger.h"
#include "../vfpu/vfpu_handler.h"

namespace ppsspp {
namespace interp {

using core::Decoded;

bool Interpreter::ExecCop(Decoded& d, StepResult& r) {
    core::CPUState& cpu = cpu_;

    switch (d.opcode) {
    case core::Op::COP0: {
        switch (d.rt) {
        case core::CopField::MFC0:
            if (d.rt != 0) {
                switch (d.rd) {
                case 12: cpu.SetReg(d.rt,cpu.cp0_Status);  break;
                case 13: cpu.SetReg(d.rt,cpu.cp0_Cause);   break;
                case 14: cpu.SetReg(d.rt,cpu.cp0_EPC);     break;
                case 15: cpu.SetReg(d.rt,cpu.cp0_PRid);    break;
                default:  cpu.SetReg(d.rt,0);              break;
                }
            }
            return true;

        case core::CopField::MTC0:
            switch (d.rd) {
            case 12: cpu.cp0_Status = cpu.Reg(d.rt); break;
            case 13: cpu.cp0_Cause  = cpu.Reg(d.rt); break;
            case 14: cpu.cp0_EPC    = cpu.Reg(d.rt); break;
            case 15: cpu.cp0_PRid   = cpu.Reg(d.rt); break;
            default: break;
            }
            return true;

        default:
            return false;
        }
    }

    case core::Op::COP1:
        // FPU, не совместимая с MIPS I/II. На PSP используется VFPU (COP2).
        LOG_DEBUG("COP1 в PC=0x%08X не поддерживается (нужен VFPU)", d.pc);
        return false;

    case core::Op::COP2: {
        vfpu::VFPUHandler handler;
        return handler.Execute(cpu_, d);
    }

    case core::Op::COP3:
        return false;

    default:
        return false;
    }
}

} // namespace interp
} // namespace ppsspp