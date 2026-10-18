// core/cpu_state.h — модель процессора PSP (Allegrex / MIPS32r2)
//
// Сохранено из снапшота проекта от 10.06.2025 (D:\Халявы Не Работает\PSP\PSP360),
// где модель была полной: GPR + HI/LO + VFPU-регистры + COP0 + LL/SC.
// В отличие от более поздней версии, поля публичные — это позволяет
// тестам и интерпретатору работать без геттеров и без потери состояния.
#pragma once

#include <cstdint>
#include <cstring>

namespace ppsspp {
namespace core {

// Размеры векторного регистра Allegrex: 128 регистров x 4 float.
constexpr int VFPU_REGS = 128;
constexpr int VFPU_LANES = 4;

// Базовые адреса адресного пространства PSP (пользовательский режим).
constexpr uint32_t PSP_RAM_BASE   = 0x08800000u;  // 32 МБ оперативной памяти
constexpr uint32_t PSP_VRAM_BASE  = 0x04000000u;  // 2 МБ видеопамяти
constexpr uint32_t PSP_SCRATCH_BASE = 0x10000000u; // 16 КБ scratchpad
constexpr uint32_t PSP_STACK_TOP  = 0x08A00000u;

// Специальное значение PC, означающее «процесс остановлен».
constexpr uint32_t PC_HALTED = 0xFFFFFFFFu;

class Memory;

class CPUState {
public:
    // ---- основные регистры ----
    uint32_t gpr[32];
    uint32_t hi, lo;
    uint32_t pc;       // адрес выполняемой сейчас инструкции
    uint32_t nextPC;   // адрес следующей инструкции (слот задержки учтён)

    // ---- векторные регистры VFPU ----
    float vpr[VFPU_REGS][VFPU_LANES];

    // ---- сопроцессор COP0 (управление состоянием) ----
    uint32_t cp0_Status;
    uint32_t cp0_Cause;
    uint32_t cp0_EPC;
    uint32_t cp0_IM;      // Interrupt Mask
    uint32_t cp0_PRid;

    // ---- LL/SC (атомарность, критично для многопоточного кода PSP) ----
    bool     llBit;
    uint32_t llAddr;

    // ---- одиночный шаг для тестов и отладки ----
    uint32_t scratchInst;
    uint32_t scratchPC;

    // ---- флаги JIT-анализатора (какие GPR живут в регистрах CPU) ----
    bool gprInUse[32];

    // ---- прерывания ----
    bool interruptPending;

    // ---- привязка к подсистеме памяти ----
    Memory* memory;

    // ---- счётчики ----
    uint64_t instructionsExecuted;

    explicit CPUState(uint32_t initialPC = 0);
    ~CPUState() = default;

    void Reset(uint32_t newPC = 0);

    // Запись инструкции для пошагового исполнения oneInst().
    void StepOne(uint32_t pc, uint32_t instructionWord);

    // ---- прерывания ----
    void RaiseInterrupt(int irq);
    void ClearInterrupt(int irq);
    bool ShouldTakeInterrupt() const;
    // Возвращает вектор обработчика прерывания или 0, если прерывать нельзя.
    uint32_t TakeInterruptVector();

    // ---- доступ с проверкой индекса ----
    // Регистр $0 на реальном оборудовании всегда читается как ноль,
    // даже если в поле хранения осталось мусорное значение.
    uint32_t Reg(int index) const {
        const int i = index & 31;
        return (i == 0) ? 0u : gpr[i];
    }
    void SetReg(int index, uint32_t v) {
        const int i = index & 31;
        if (i != 0) gpr[i] = v;
    }
    uint32_t GetGPR(int index) const { return Reg(index); }
    void    SetGPR(int index, uint32_t v) { SetReg(index, v); }

private:
    void ResetVFPU();
};

} // namespace core
} // namespace ppsspp