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
    cp0_IM     = 0;
    cp0_PRid   = 0;

    llBit = false;
    llAddr = 0;

    scratchInst = 0;
    scratchPC = newPC;

    interruptPending = false;
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
    const bool IE  = (cp0_Status & 1u) != 0;  // Interrupt Enable
    const bool EXL = (cp0_Status & 2u) != 0;  // Exception Level
    const bool ERL = (cp0_Status & 4u) != 0;  // Error Level

    if (!IE || EXL || ERL) return false;

    const uint32_t pending = (cp0_Cause & 0x0000FF00u) >> 8;
    return (pending & cp0_IM) != 0;
}

uint32_t CPUState::TakeInterruptVector() {
    if (!ShouldTakeInterrupt()) return 0;

    const uint32_t pending = (cp0_Cause & 0x0000FF00u) >> 8;
    uint32_t irq = 0;
    for (int i = 0; i < 8; ++i) {
        if (pending & (1u << i)) { irq = static_cast<uint32_t>(i); break; }
    }

    // Стандартная последовательность входа в обработчик прерывания MIPS:
    // устанавливаем EXL и сбрасываем IE.
    cp0_EPC    = pc;
    cp0_Status = (cp0_Status | 0x00000002u) & ~0x00000001u;

    // Базовый вектор прерывания PSP
    constexpr uint32_t kBaseVector = 0x00000100u;
    return kBaseVector + irq * 0x20u;
}

} // namespace core
} // namespace ppsspp