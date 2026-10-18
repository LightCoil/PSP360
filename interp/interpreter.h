// interp/interpreter.h — интерпретатор инструкций PSP (MIPS32)
//
// Сохранена архитектура из снапшота 10.06.2025 (oneInst + pc/nextPC),
// но диспетчеризация разбита по группам инструкций, как в более поздней
// версии проекта, и добавлено корректное выполнение слотов задержки.
#pragma once

#include <cstdint>

#include "../core/cpu_state.h"
#include "../core/decoder.h"
#include "../core/memory.h"

namespace ppsspp {
namespace interp {

// Результат выполнения шага.
enum class StepResult : uint32_t {
    Normal = 0,
    Branch,
    Jump,
    Syscall,
    Halted,
    Unimplemented,
    BusError,
    Exception,   // возбуждено синхронное исключение, PC перенаправлен
};

class Interpreter {
public:
    Interpreter(core::CPUState& cpu, core::Memory& mem);

    // Выполняет одну инструкцию по модели pc/nextPC (слот задержки).
    StepResult Step();

    // Выполняет одну инструкцию из явно заданных pc/word (для тестов).
    StepResult StepInstruction(uint32_t pc, uint32_t word);

    // Загружает инструкцию в scratch-поля и выполняет её.
    // Возвращает адрес следующей инструкции (аналог oneInstInterpreter
    // из снапшота 10.06.2025).
    uint32_t StepOne(uint32_t pc, uint32_t word);

    // Основной цикл. maxInstructions==0 — без ограничения.
    // Возвращает число выполненных инструкций.
    uint64_t Run(uint64_t maxInstructions = 0);

    void SetTracing(bool on) { trace_ = on; }
    void Reset();

    uint64_t unimplementedCount() const { return unimplementedCount_; }

private:
    // Группы операций. Каждая возвращает true, если инструкция обработана.
    bool ExecArith(core::Decoded& d, StepResult& r);
    bool ExecLogic(core::Decoded& d, StepResult& r);
    bool ExecShift(core::Decoded& d, StepResult& r);
    bool ExecBranch(core::Decoded& d, StepResult& r);
    bool ExecMemory(core::Decoded& d, StepResult& r);
    bool ExecCop(core::Decoded& d, StepResult& r);
    bool ExecVfpu(core::Decoded& d, StepResult& r);
    bool ExecMultDiv(core::Decoded& d, StepResult& r);
    // Регистрирует исключение арифметического переполнения.
    void RaiseOverflow(const core::Decoded& d, int32_t a, int32_t b);

    void Trace(const core::Decoded& d, StepResult r);

    core::CPUState& cpu_;
    core::Memory& mem_;
    bool trace_ = false;
    uint64_t unimplementedCount_ = 0;
    // Истинно, если последняя ошибка выборки инструкции перенаправила PC
    // в неотображённый обработчик: дальнейшее исполнение бессмысленно.
    bool fetchFaultLoop_ = false;
};

} // namespace interp
} // namespace ppsspp