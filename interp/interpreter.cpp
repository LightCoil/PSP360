#include "interpreter.h"

#include "../core/logger.h"
#include "../core/syscall_defs.h"
#include "../vfpu/vfpu_handler.h"

namespace ppsspp {
namespace interp {

using core::Decoded;
using core::Memory;
using core::CPUState;

Interpreter::Interpreter(CPUState& cpu, Memory& mem) : cpu_(cpu), mem_(mem) {
    cpu_.memory = &mem_;
}

void Interpreter::Reset() {
    unimplementedCount_ = 0;
}

StepResult Interpreter::StepInstruction(uint32_t pc, uint32_t word) {
    Decoded d = core::Decode(pc, word);
    StepResult r = StepResult::Normal;

    // Определяем, на какую группу попадает инструкция.
    bool handled = false;
    if (d.opcode == core::Op::SPECIAL) {
        switch (d.funct) {
        case core::Funct::ADD: case core::Funct::ADDU: case core::Funct::SUB: case core::Funct::SUBU:
            handled = ExecArith(d, r);
            break;
        case core::Funct::MULT: case core::Funct::MULTU: case core::Funct::DIV: case core::Funct::DIVU:
            handled = ExecMultDiv(d, r);
            break;
        case core::Funct::AND: case core::Funct::OR: case core::Funct::XOR: case core::Funct::NOR:
        case core::Funct::SLT: case core::Funct::SLTU:
            handled = ExecLogic(d, r);
            break;
        case core::Funct::SLL: case core::Funct::SRL: case core::Funct::SRA:
        case core::Funct::SLLV: case core::Funct::SRLV: case core::Funct::SRAV:
        case core::Funct::SYSCALL: case core::Funct::BREAK:
            handled = ExecShift(d, r);
            break;
        case core::Funct::JR: case core::Funct::JALR:
            handled = ExecBranch(d, r);
            break;
        case core::Funct::MFHI: case core::Funct::MTHI: case core::Funct::MFLO: case core::Funct::MTLO:
        case core::Funct::MOVZ: case core::Funct::MOVN: case core::Funct::SYNC:
            handled = ExecArith(d, r);
            break;
        default:
            break;
        }
    }

    if (!handled) {
        switch (d.opcode) {
        case core::Op::ADDI: case core::Op::ADDIU: case core::Op::SLTI: case core::Op::SLTIU:
            handled = ExecArith(d, r);
            break;
        case core::Op::ANDI: case core::Op::ORI: case core::Op::XORI: case core::Op::LUI:
            handled = ExecLogic(d, r);
            break;
        case core::Op::LB: case core::Op::LH: case core::Op::LW: case core::Op::LBU: case core::Op::LHU:
        case core::Op::SB: case core::Op::SH: case core::Op::SW: case core::Op::LWL: case core::Op::LWR:
        case core::Op::SWL: case core::Op::SWR: case core::Op::PREF:
            handled = ExecMemory(d, r);
            break;
        case core::Op::BEQ: case core::Op::BNE: case core::Op::BLEZ: case core::Op::BGTZ:
        case core::Op::J: case core::Op::JAL: case core::Op::REGIMM:
            handled = ExecBranch(d, r);
            break;
        case core::Op::COP0: case core::Op::COP1: case core::Op::COP2: case core::Op::COP3:
            handled = ExecCop(d, r);
            break;

        // VFPU Allegrex: операции разнесены по нескольким опкодам MIPS.
        case core::Op::VFPU_A:
        case core::Op::VFPU_B:
        case core::Op::VFPU_C:
        case core::Op::VFPU_D:
            handled = ExecVfpu(d, r);
            break;
        default:
            handled = false;
            break;
        }
    }

    if (!handled) {
        ++unimplementedCount_;
        r = StepResult::Unimplemented;
        LOG_WARN("Не реализована инструкция 0x%08X (opcode=0x%02X funct=0x%02X) по PC=0x%08X",
                 d.word, d.opcode, d.funct, d.pc);
    }

    Trace(d, r);
    return r;
}

StepResult Interpreter::Step() {
    if (cpu_.pc == core::PC_HALTED) return StepResult::Halted;

    // Прерывания имеют приоритет над обычной инструкцией.
    if (cpu_.ShouldTakeInterrupt()) {
        const uint32_t vector = cpu_.TakeInterruptVector();
        if (vector != 0) {
            cpu_.pc     = vector;
            cpu_.nextPC = vector + 4;
            cpu_.SetInDelaySlot(false);
            return StepResult::Normal;
        }
    }

    const uint32_t pc = cpu_.pc;
    const uint32_t word = mem_.Read32(pc);

    if (mem_.LastFault() != core::MemFault::None) {
        // Ошибка выборки инструкции. Диагностика снимается СРАЗУ, до
        // любых других обращений к памяти: LastFault хранит результат
        // только последней операции, и любое успешное чтение после
        // ошибки стёрло бы признак.
        LOG_ERROR("Ошибка чтения инструкции по адресу 0x%08X", pc);

        // Ошибка адреса при выборке -> AdEL (ExcCode 4). Раньше здесь
        // возвращался BusError без изменения PC, из-за чего Run()
        // бесконечно перечитывал тот же неотображённый адрес.
        const bool inDelay = cpu_.InDelaySlot();
        cpu_.RaiseException(core::ExcCode::AdEL, inDelay ? (pc - 4u) : pc, inDelay);
        const uint32_t vector = cpu_.ExceptionVector(core::ExcCode::AdEL);
        cpu_.ClearExceptionPending();

        // Если обработчик сам находится в неотображённой памяти, PC не
        // изменится и Run() зациклится. Это фиксируется заранее.
        bool handlerUnmapped = (vector == pc);
        if (!handlerUnmapped) {
            (void)mem_.Read32(vector);      // проверка отображённости вектора
            handlerUnmapped = (mem_.LastFault() != core::MemFault::None);
        }
        cpu_.pc     = vector;
        cpu_.nextPC = vector + 4;
        cpu_.SetInDelaySlot(false);
        fetchFaultLoop_ = handlerUnmapped;
        return StepResult::Exception;
    }
    fetchFaultLoop_ = false;

    // Модель MIPS: слот задержки обеспечивается сдвигом pc/nextPC
    // до выполнения инструкции.
    cpu_.pc = cpu_.nextPC;
    cpu_.nextPC = cpu_.pc + 4;

    StepResult r = StepInstruction(pc, word);
    ++cpu_.instructionsExecuted;

    // Синхронное исключение: перенаправляем PC на вектор обработчика.
    if (cpu_.ExceptionPending()) {
        const uint32_t vector =
            cpu_.ExceptionVector(static_cast<core::ExcCode>(
                (cpu_.cp0_Cause & core::CP0_CAUSE_EXC) >> 2));
        cpu_.ClearExceptionPending();
        cpu_.pc     = vector;
        cpu_.nextPC = vector + 4;
        cpu_.SetInDelaySlot(false);
        return StepResult::Exception;
    }

    // Инструкция по pc+4 окажется в слоте задержки, если текущая была
    // инструкцией ветвления.
    cpu_.SetInDelaySlot(r == StepResult::Branch || r == StepResult::Jump);
    return r;
}

uint64_t Interpreter::Run(uint64_t maxInstructions) {
    uint64_t executed = 0;
    while (cpu_.pc != core::PC_HALTED) {
        if (maxInstructions != 0 && executed >= maxInstructions) break;

        const uint32_t pcBefore = cpu_.pc;
        const StepResult r = Step();

        // Ошибка выборки перебросила PC в неотображённый обработчик:
        // продолжать некуда, иначе цикл не прервётся.
        if (r == StepResult::Exception && fetchFaultLoop_) {
            LOG_ERROR("Обработчик исключения по адресу 0x%08X не отображён; "
                      "выполнение остановлено (исходный PC 0x%08X)",
                      cpu_.pc, pcBefore);
            cpu_.pc = core::PC_HALTED;
            cpu_.nextPC = core::PC_HALTED;
            break;
        }
        ++executed;
    }
    return executed;
}

uint32_t Interpreter::StepOne(uint32_t pc, uint32_t word) {
    cpu_.StepOne(pc, word);
    const uint32_t savedPC = cpu_.pc;
    const uint32_t savedNext = cpu_.nextPC;

    cpu_.pc     = pc;
    cpu_.nextPC = pc + 4;

    StepResult r = StepInstruction(pc, word);
    ++cpu_.instructionsExecuted;

    uint32_t result;
    switch (r) {
    case StepResult::Branch:
    case StepResult::Jump:
        // Для одиночного шага результат — фактический адрес перехода.
        result = cpu_.nextPC;
        break;
    case StepResult::Halted:
        result = core::PC_HALTED;
        break;
    default:
        result = cpu_.nextPC;
        break;
    }

    // Восстанавливаем состояние: одиночный шаг не должен двигать глобальный PC.
    cpu_.pc = savedPC;
    cpu_.nextPC = savedNext;
    return result;
}

void Interpreter::Trace(const Decoded& d, StepResult r) {
    if (!trace_) return;
    LOG_DEBUG("[TRACE] PC=0x%08X word=0x%08X op=0x%02X funct=0x%02X -> %s",
              d.pc, d.word, d.opcode, d.funct,
              (r == StepResult::Normal) ? "ok" : "event");
}

} // namespace interp
} // namespace ppsspp