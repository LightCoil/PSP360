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

// Коды исключений MIPS32 (Cause.ExcCode, биты 6..2).
enum class ExcCode : uint32_t {
    Int  = 0,   // прерывание
    AdEL = 4,   // ошибка адреса при чтении
    AdES = 5,   // ошибка адреса при записи
    Sys  = 8,   // syscall
    Bp   = 9,   // break
    RI   = 10,  // недопустимая инструкция
    Ov   = 12,  // арифметическое переполнение
};

// Номера регистров CP0, используемые моделью (Hitmen, MIPS32 Vol. III).
enum Cp0Reg : uint32_t {
    CP0_BADVADDR = 8,
    CP0_COUNT    = 9,
    CP0_COMPARE  = 11,
    CP0_STATUS   = 12,
    CP0_CAUSE    = 13,
    CP0_EPC      = 14,
    CP0_PRID     = 15,
    CP0_CONFIG   = 16,
    CP0_EBASE    = 25,
};

// Битовые позиции полей в регистрах CP0 (MIPS32 Vol. III).
constexpr uint32_t CP0_STATUS_IE  = 0x00000001u;  // Interrupt Enable
constexpr uint32_t CP0_STATUS_EXL = 0x00000002u;  // Exception Level
constexpr uint32_t CP0_STATUS_ERL = 0x00000004u;  // Error Level
constexpr uint32_t CP0_STATUS_IM  = 0x0000FF00u;  // Interrupt Mask, биты 15..8
constexpr uint32_t CP0_STATUS_BEV = 0x00400000u;  // Boot Exception Vector, бит 22
constexpr uint32_t CP0_CAUSE_EXC  = 0x0000007Cu;  // ExcCode, биты 6..2
constexpr uint32_t CP0_CAUSE_BD   = 0x80000000u;  // Branch Delay

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
    //
    // Отдельного регистра Interrupt Mask на Allegrex нет: маска прерываний
    // IM[7:0] занимает биты 15..8 регистра Status. Поэтому она хранится
    // именно там, а доступна через InterruptMask()/SetInterruptMask(),
    // иначе гостевой код не смог бы включить прерывания.
    uint32_t cp0_Status;
    uint32_t cp0_Cause;
    uint32_t cp0_EPC;
    uint32_t cp0_EBase;   // CP0 25: база векторов исключений
    uint32_t cp0_PRid;

    // LL/SC (атомарность, критично для многопоточного кода PSP)
    bool     llBit;
    uint32_t llAddr;

    // ---- одиночный шаг для тестов и отладки ----
    uint32_t scratchInst;
    uint32_t scratchPC;

    // ---- флаги JIT-анализатора (какие GPR живут в регистрах CPU) ----
    bool gprInUse[32];

    // ---- прерывания ----
    bool interruptPending;
    // Возбуждено синхронное исключение, PC должен быть перенаправлен.
    bool exceptionPending;
    // Текущая инструкция находится в слоте задержки.
    bool inDelaySlot;

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

    // ---- маска прерываний: поле IM в Status, биты 15..8 ----
    uint32_t InterruptMask() const {
        return (cp0_Status & CP0_STATUS_IM) >> 8;
    }
    void SetInterruptMask(uint32_t mask) {
        cp0_Status = (cp0_Status & ~CP0_STATUS_IM) |
                     ((mask & 0xFFu) << 8);
    }

    // ---- исключения ----
    // epc — адрес для EPC. Если исключение возникло в слоте задержки,
    // epc обязан указывать на инструкцию ветвления, а BD устанавливается.
    void RaiseException(ExcCode code, uint32_t epc, bool inDelaySlot);
    bool ExceptionPending() const { return exceptionPending; }
    void ClearExceptionPending() { exceptionPending = false; }
    // MIPS32 Vol. III: "EBase register for exceptions that occur when
    // Status.BEV equals 0". При BEV = 1 (загрузка; таблица векторов ещё
    // не выставлена) используются фиксированные загрузочные векторы:
    //   общее исключение — 0x00000180 (0x80000180 при Status.BE = 1)
    //   прерывание        — 0x00000380 + irq * 0x20
    // Бит 31 (Boot Exception) на PSP равен 0: Allegrex работает в
    // little-endian, поэтому используется младший адрес.
    uint32_t ExceptionVector(ExcCode code) const {
        if (cp0_Status & CP0_STATUS_BEV) {
            return 0x00000180u;
        }
        return (cp0_EBase + 0x100u) + (static_cast<uint32_t>(code) << 5);
    }
    uint32_t InterruptVector(int irq) const {
        if (cp0_Status & CP0_STATUS_BEV) {
            return 0x00000380u + (static_cast<uint32_t>(irq) << 5);
        }
        return (cp0_EBase + 0x100u) + (static_cast<uint32_t>(irq) << 5);
    }

    // Истинно, если инструкция по текущему pc находится в слоте задержки.
    bool InDelaySlot() const { return inDelaySlot; }
    void SetInDelaySlot(bool v) { inDelaySlot = v; }

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