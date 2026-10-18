// interp/ops_cop.cpp — сопроцессоры COP0/COP1/COP2 (VFPU)/COP3
#include "interpreter.h"
#include "../core/logger.h"
#include "../vfpu/vfpu_handler.h"

namespace ppsspp {
namespace interp {

using core::Decoded;

bool Interpreter::ExecCop(Decoded& d, StepResult& r) {
    core::CPUState& cpu = cpu_;
    (void)r;   // исключения COP0 обрабатываются через cpu_.RaiseException

    switch (d.opcode) {
    case core::Op::COP0: {
        // Раскладка (см. core/decoder.h): rs5 = биты 25..21 — селектор
        // по PPSPP, биты 4..0 — поле MIPS32. Вид операции уже определён
        // декодером с проверкой зарезервированных битов (copKind), поэтому
        // здесь достаточно перейти по нему. Использовать d.rt нельзя:
        // это номер GPR, а не идентификатор операции.
        switch (d.copKind) {
        case core::COP0_MFC0: {
            if (d.rt == 0) return true;      // запись в $0 игнорируется
            uint32_t v;
            switch (d.rd) {
            case core::CP0_STATUS: v = cpu.cp0_Status; break;
            case core::CP0_CAUSE:  v = cpu.cp0_Cause;  break;
            case core::CP0_EPC:    v = cpu.cp0_EPC;    break;
            case core::CP0_EBASE:  v = cpu.cp0_EBase;  break;
            case core::CP0_PRID:   v = cpu.cp0_PRid;   break;
            case core::CP0_BADVADDR:
            case core::CP0_COUNT:
            case core::CP0_COMPARE:
            case core::CP0_CONFIG:
                // Регистры существуют, но модель их не реализует.
                v = 0;
                LOG_DEBUG("COP0: чтение нереализованного регистра %u в PC=0x%08X",
                          d.rd, d.pc);
                break;
            default:
                LOG_DEBUG("COP0: чтение несуществующего регистра %u в PC=0x%08X",
                          d.rd, d.pc);
                return false;
            }
            cpu.SetReg(d.rt, v);
            return true;
        }

        case core::COP0_MTC0: {
            const uint32_t v = cpu.Reg(d.rt);
            switch (d.rd) {
            case core::CP0_STATUS: cpu.cp0_Status = v; break;
            case core::CP0_CAUSE:  cpu.cp0_Cause  = v; break;
            case core::CP0_EPC:    cpu.cp0_EPC    = v; break;
            case core::CP0_EBASE:  cpu.cp0_EBase  = v; break;
            default:
                LOG_DEBUG("COP0: запись в регистр %u не поддерживается "
                          "в PC=0x%08X", d.rd, d.pc);
                return false;
            }
            return true;
        }

        default:
            // cfc0/ctc0, TLB-инструкции (rs5 = 1..15), eret/wait и любые
            // нераспознанные комбинации битов. Ничего не выполняем.
            LOG_DEBUG("COP0: операция rs5=%u low5=0x%02X не поддерживается "
                      "в PC=0x%08X", d.copRs, d.copOp, d.pc);
            return false;
        }
    }

    case core::Op::COP1:
        // FPU, не совместимая с MIPS I/II. На PSP используется VFPU (COP2).
        LOG_DEBUG("COP1 в PC=0x%08X не поддерживается (нужен VFPU)", d.pc);
        return false;

    case core::Op::COP2: {
        // Перенос регистров VFPU (lv.s, sv.s, mftv, cfti ...) — не
        // реализован. Сами векторные операции живут в опкодах 0x18/0x19/
        // 0x1B/0x34 и обрабатываются в ExecVfpu.
        LOG_DEBUG("Перенос регистров VFPU (COP2) в PC=0x%08X не поддерживается", d.pc);
        return false;
    }

    case core::Op::COP3:
        return false;

    default:
        return false;
    }
}

// Векторные операции VFPU (опкоды 0x18, 0x19, 0x1B, 0x34).
bool Interpreter::ExecVfpu(core::Decoded& d, StepResult& r) {
    (void)r;
    return vfpu::VFPUHandler::Execute(cpu_, d);
}

} // namespace interp
} // namespace ppsspp