#include "cpu_state.h"
#include "memory.h"

namespace ppsspp {
namespace core {

CPUState::CPUState(uint32_t initialPC) {
    memory = nullptr;
    Reset(initialPC);
}

void CPUState::ResetVFPU() {
    std::memset(vpr, 0, sizeof(vpr));
}

void CPUState::Reset(uint32_t newPC) {
    std::memset(gpr, 0, sizeof(gpr));
    std::memset(gprInUse, 0, sizeof(gprInUse));

    hi = lo = 0;
    pc = newPC;
    nextPC = (newPC == PC_HALTED) ? PC_HALTED : newPC + 4;

    ResetVFPU();

    cp0_Status = 0;
    cp0_Cause  = 0;
    cp0_EPC    = 0;
    cp0_EBase  = 0;
    cp0_PRid   = 0;

    llBit = false;
    llAddr = 0;

    scratchInst = 0;
    scratchPC = newPC;

    interruptPending = false;
    exceptionPending = false;
    inDelaySlot = false;
    instructionsExecuted = 0;
}

void CPUState::StepOne(uint32_t stepPC, uint32_t instructionWord) {
    scratchPC = stepPC;
    scratchInst = instructionWord;
}

// ---------------------------------------------------------------- прерывания

void CPUState::RaiseInterrupt(int irq) {
    // В cp0_Cause биты 8..15 соответствуют аппаратным линиям 0..7
    cp0_Cause |= (1u << (8 + irq));
    interruptPending = true;
}

void CPUState::ClearInterrupt(int irq) {
    cp0_Cause &= ~(1u << (8 + irq));
    if ((cp0_Cause & 0x0000FF00u) == 0) {
        interruptPending = false;
    }
}

bool CPUState::ShouldTakeInterrupt() const {
    const bool IE  = (cp0_Status & CP0_STATUS_IE) != 0;
    const bool EXL = (cp0_Status & CP0_STATUS_EXL) != 0;
    const bool ERL = (cp0_Status & CP0_STATUS_ERL) != 0;

    if (!IE || EXL || ERL) return false;

    const uint32_t pending = (cp0_Cause & 0x0000FF00u) >> 8;
    return (pending & InterruptMask()) != 0;
}

uint32_t CPUState::TakeInterruptVector() {
    if (!ShouldTakeInterrupt()) return 0;

    const uint32_t pending = (cp0_Cause & 0x0000FF00u) >> 8;
    const uint32_t masked  = pending & InterruptMask();
    int irq = 0;
    for (int i = 0; i < 8; ++i) {
        if (masked & (1u << i)) { irq = i; break; }
    }

    // Стандартная последовательность входа MIPS: записываем EPC,
    // устанавливаем EXL и сбрасываем IE.
    cp0_EPC    = pc;
    cp0_Status = (cp0_Status | CP0_STATUS_EXL) & ~CP0_STATUS_IE;
    // Прерывание не помечается как находящееся в слоте задержки.
    cp0_Cause &= ~CP0_CAUSE_BD;

    return InterruptVector(irq);
}

// ---------------------------------------------------------------- исключения

void CPUState::RaiseException(ExcCode code, uint32_t epc, bool delaySlot) {
    cp0_EPC = epc;

    // ExcCode занимает биты 6..2, BD — бит 31.
    cp0_Cause = (cp0_Cause & ~(CP0_CAUSE_EXC | CP0_CAUSE_BD)) |
                (static_cast<uint32_t>(code) << 2) |
                (delaySlot ? CP0_CAUSE_BD : 0u);

    cp0_Status |=  CP0_STATUS_EXL;   // EXL = 1
    cp0_Status &= ~CP0_STATUS_IE;    // IE  = 0

    exceptionPending = true;
}

} // namespace core
} // namespace ppsspp