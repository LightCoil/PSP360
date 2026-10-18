// host/main.cpp — хостовый раннер для отладки ядра эмуляции.
//
// Запускает встроенный тестовый образ прямо в памяти и печатает
// состояние регистров. Собирается обычным компилятором хоста,
// не требует devkitXenon и не запускается на Xbox 360.
#include <cstdio>
#include <cstring>
#include <string>

#include "Loader/loader.h"
#include "core/cpu_state.h"
#include "core/logger.h"
#include "core/memory.h"
#include "interp/interpreter.h"

using namespace ppsspp;

namespace {

constexpr uint32_t kCodeBase = 0x08804000u;

// Кодировщики инструкций (те же, что в тестах).
inline uint32_t encI(uint32_t op, uint32_t rs, uint32_t rt, int32_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) |
           (static_cast<uint32_t>(imm) & 0xFFFF);
}

// Тестовая программа:
//   1) addiu $2,$0,42
//   2) addiu $3,$2,8        -> 50
//   3) lui   $4,0x1234
//   4) ori   $4,$4,0x5678  -> 0x12345678
//   5) lui   $8,0x0880     -> базовый адрес ОЗУ PSP
//   6) ori   $8,$8,0x2000   -> 0x08802000
//   7) sw    $4,0($8)       -> запись в ОЗУ
//   8) lw    $5,0($8)       -> чтение обратно
//   9) addiu $6,$5,1        -> 0x12345679
const uint32_t kTestProgram[] = {
    encI(0x09, 0, 2, 42),       // addiu $2,$0,42
    encI(0x09, 2, 3, 8),        // addiu $3,$2,8
    encI(0x0F, 0, 4, 0x1234),   // lui   $4,0x1234
    encI(0x0D, 4, 4, 0x5678),   // ori   $4,$4,0x5678
    encI(0x0F, 0, 8, 0x0880),   // lui   $8,0x0880
    encI(0x0D, 8, 8, 0x2000),   // ori   $8,$8,0x2000
    encI(0x2B, 8, 4, 0),        // sw    $4,0($8)
    encI(0x23, 8, 5, 0),        // lw    $5,0($8)
    encI(0x09, 5, 6, 1),        // addiu $6,$5,1
};
constexpr uint32_t kProgramCount =
    static_cast<uint32_t>(sizeof(kTestProgram) / sizeof(kTestProgram[0]));

void DumpRegisters(const core::CPUState& cpu) {
    std::printf("\nРегистры:\n");
    for (int i = 0; i < 32; i += 4) {
        std::printf("  ");
        for (int j = 0; j < 4; ++j) {
            std::printf(" $%-2d=0x%08X", i + j, cpu.gpr[i + j]);
        }
        std::printf("\n");
    }
    std::printf("\n  hi=0x%08X  lo=0x%08X\n", cpu.hi, cpu.lo);
    std::printf("  pc=0x%08X  nextPC=0x%08X\n", cpu.pc, cpu.nextPC);
    std::printf("  выполнено инструкций: %llu\n",
                static_cast<unsigned long long>(cpu.instructionsExecuted));
}

} // namespace

int main(int argc, char** argv) {
    core::SetLogLevel(core::LogLevel::Info);

    std::printf("psp360Phoenix — хостовый раннер ядра эмуляции\n");
    std::printf("=============================================\n");

    core::Memory mem;
    mem.Init();
    if (!mem.IsInitialized()) {
        std::printf("ОШИБКА: не удалось выделить память PSP\n");
        return 1;
    }

    // Загружаем тестовую программу в ОЗУ
    for (uint32_t i = 0; i < kProgramCount; ++i) {
        mem.Write32(kCodeBase + i * 4, kTestProgram[i]);
    }
    std::printf("Тестовая программа загружена: %u инструкций @ 0x%08X\n",
                kProgramCount, kCodeBase);

    core::CPUState cpu;
    cpu.Reset(kCodeBase);

    interp::Interpreter interp(cpu, mem);
    interp.SetTracing(true);

    const uint64_t executed = interp.Run(kProgramCount);

    std::printf("\nВыполнено %llu инструкций.\n",
                static_cast<unsigned long long>(executed));
    DumpRegisters(cpu);

    // Проверка ожидаемых результатов
    int failures = 0;
    auto check = [&](const char* name, uint32_t got, uint32_t want) {
        const bool ok = (got == want);
        if (!ok) ++failures;
        std::printf("  [%s] %-28s получено 0x%08X, ожидалось 0x%08X\n",
                    ok ? "OK  " : "FAIL", name, got, want);
    };

    std::printf("\nПроверка результатов:\n");
    check("addiu $2,42",        cpu.gpr[2], 42u);
    check("addiu $3,$2,8",      cpu.gpr[3], 50u);
    check("lui/ori $4",        cpu.gpr[4], 0x12345678u);
    check("lw $5 <- RAM",       cpu.gpr[5], 0x12345678u);
    check("addiu $6,$5,1",      cpu.gpr[6], 0x12345679u);

    mem.Shutdown();

    if (failures == 0) {
        std::printf("\nВсе проверки пройдены.\n");
        return 0;
    }
    std::printf("\nПровалено проверок: %d\n", failures);
    return 1;
}