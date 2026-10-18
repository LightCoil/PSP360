// tests/test_interp.cpp — тесты интерпретатора инструкций
//
// Инструкции склеиваются макросами enc_* — так же, как в снапшоте
// 10.06.2025, где тесты проверяли add / LW / SW.
#include <gtest/gtest.h>

#include "core/memory.h"
#include "interp/interpreter.h"

using ppsspp::core::Memory;
using ppsspp::core::CPUState;
using ppsspp::interp::Interpreter;
using ppsspp::interp::StepResult;
namespace core = ppsspp::core;
namespace interp = ppsspp::interp;

// ---- кодировщики инструкций ----
static inline uint32_t encR(uint32_t rs, uint32_t rt, uint32_t rd,
                            uint32_t shamt, uint32_t funct) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (shamt << 6) | funct;
}
static inline uint32_t encI(uint32_t op, uint32_t rs, uint32_t rt, int32_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | (static_cast<uint32_t>(imm) & 0xFFFF);
}
static inline uint32_t encJ(uint32_t op, uint32_t target) {
    return (op << 26) | ((target >> 2) & 0x03FFFFFF);
}

static constexpr uint32_t kTestPC = 0x08801000u;

namespace {

class InterpTest : public ::testing::Test {
protected:
    Memory   mem;
    CPUState cpu;
    Interpreter interp;

    InterpTest() : interp(cpu, mem) { mem.Init(); }

    // Выполняет одну инструкцию через scratch-интерфейс (как oneInst).
    uint32_t one(uint32_t word, uint32_t pc = kTestPC) {
        return interp.StepOne(pc, word);
    }
};

TEST_F(InterpTest, Add) {
    cpu.gpr[1] = 10;
    cpu.gpr[2] = 5;

    const uint32_t next = one(encR(1, 2, 3, 0, 0x20)); // add $3,$1,$2

    EXPECT_EQ(cpu.gpr[3], 15u);
    EXPECT_EQ(next, kTestPC + 4u);
}

TEST_F(InterpTest, Addu) {
    cpu.gpr[1] = 10;
    cpu.gpr[2] = 5;
    one(encR(1, 2, 3, 0, 0x21)); // addu $3,$1,$2
    EXPECT_EQ(cpu.gpr[3], 15u);
}

TEST_F(InterpTest, Sub) {
    cpu.gpr[1] = 10;
    cpu.gpr[2] = 3;
    one(encR(1, 2, 3, 0, 0x22)); // sub $3,$1,$2
    EXPECT_EQ(cpu.gpr[3], 7u);
}

TEST_F(InterpTest, RegisterZeroIsHardwired) {
    // Имитируем «мусор» в поле хранения $0 — на реальном PSP он
    // всё равно всегда читается как ноль.
    cpu.gpr[0] = 0xFFFFFFFFu;
    EXPECT_EQ(cpu.GetGPR(0), 0u);

    cpu.gpr[1] = 4;
    one(encR(1, 1, 0, 0, 0x21)); // addu $0,$1,$1 — запись в $0 игнорируется
    EXPECT_EQ(cpu.GetGPR(0), 0u);

    // Регистры, отличные от нулевого, пишутся нормально
    one(encR(1, 1, 5, 0, 0x21)); // addu $5,$1,$1
    EXPECT_EQ(cpu.gpr[5], 8u);
}

TEST_F(InterpTest, Addi) {
    cpu.gpr[1] = 5;
    const uint32_t next = one(encI(0x08, 1, 2, 10)); // addi $2,$1,10
    EXPECT_EQ(cpu.gpr[2], 15u);
    EXPECT_EQ(next, kTestPC + 4u);
}

TEST_F(InterpTest, AddiNegativeImmediate) {
    cpu.gpr[1] = 5;
    one(encI(0x09, 1, 2, -3)); // addiu $2,$1,-3
    EXPECT_EQ(cpu.gpr[2], 2u);
}

TEST_F(InterpTest, LuiAndOri) {
    one(encI(0x0F, 0, 2, 0x1234));  // lui $2,0x1234
    EXPECT_EQ(cpu.gpr[2], 0x12340000u);
    one(encI(0x0D, 2, 2, 0x5678));  // ori $2,$2,0x5678
    EXPECT_EQ(cpu.gpr[2], 0x12345678u);
}

TEST_F(InterpTest, LogicOps) {
    cpu.gpr[1] = 0xF0F0F0F0u;
    cpu.gpr[2] = 0xFF00FF00u;

    one(encR(1, 2, 3, 0, 0x24)); // and $3,$1,$2
    EXPECT_EQ(cpu.gpr[3], 0xF000F000u);

    one(encR(1, 2, 4, 0, 0x25)); // or $4,$1,$2
    EXPECT_EQ(cpu.gpr[4], 0xFFF0FFF0u);

    one(encR(1, 2, 5, 0, 0x26)); // xor $5,$1,$2
    EXPECT_EQ(cpu.gpr[5], 0x0FF00FF0u);

    one(encR(1, 2, 6, 0, 0x27)); // nor $6,$1,$2
    EXPECT_EQ(cpu.gpr[6], 0x000F000Fu);
}

TEST_F(InterpTest, ShiftOps) {
    cpu.gpr[1] = 0x0000000Fu;
    one(encR(0, 1, 2, 4, 0x00)); // sll $2,$1,4
    EXPECT_EQ(cpu.gpr[2], 0xF0u);

    one(encR(0, 1, 3, 0, 0x02)); // srl $3,$1,0  (без сдвига)
    EXPECT_EQ(cpu.gpr[3], 0x0Fu);

    cpu.gpr[4] = 0x80000000u;
    one(encR(0, 4, 5, 31, 0x02)); // srl $5,$4,31 -> 1
    EXPECT_EQ(cpu.gpr[5], 1u);

    one(encR(0, 4, 6, 4, 0x03)); // sra $6,$4,4 -> 0xF8000000
    EXPECT_EQ(cpu.gpr[6], 0xF8000000u);
}

TEST_F(InterpTest, MultAndDiv) {
    cpu.gpr[1] = 7;
    cpu.gpr[2] = 6;

    one(encR(1, 2, 0, 0, 0x18)); // mult $1,$2
    EXPECT_EQ(cpu.lo, 42u);
    EXPECT_EQ(cpu.hi, 0u);

    one(encR(1, 2, 0, 0, 0x1A)); // div $1,$2
    EXPECT_EQ(cpu.lo, 1u);
    EXPECT_EQ(cpu.hi, 1u);
}

TEST_F(InterpTest, MultiplyOverflowGoesToHi) {
    cpu.gpr[1] = 0x00010000u;
    cpu.gpr[2] = 0x00010000u;
    one(encR(1, 2, 0, 0, 0x19)); // multu
    EXPECT_EQ(cpu.lo, 0u);
    EXPECT_EQ(cpu.hi, 1u);
}

TEST_F(InterpTest, LoadStoreWord) {
    cpu.gpr[1] = 0x08802000u;
    cpu.gpr[2] = 0xDEADBEEFu;

    const uint32_t sw = encI(0x2B, 1, 2, 0); // sw $2,0($1)
    EXPECT_EQ(one(sw), kTestPC + 4u);
    EXPECT_EQ(mem.Read32(0x08802000u), 0xDEADBEEFu);

    const uint32_t lw = encI(0x23, 1, 3, 0); // lw $3,0($1)
    one(lw);
    EXPECT_EQ(cpu.gpr[3], 0xDEADBEEFu);
}

TEST_F(InterpTest, LoadStoreByteAndHalf) {
    cpu.gpr[1] = 0x08802100u;

    cpu.gpr[2] = 0x80u;
    one(encI(0x28, 1, 2, 0));          // sb $2,0($1)
    one(encI(0x21, 1, 3, 0));          // lh $3,0($1)
    // PSP little-endian: по адресу 80 00 => halfword 0x0080 = +128.
    EXPECT_EQ(cpu.gpr[3], 128u);

    one(encI(0x25, 1, 4, 0));          // lhu $4,0($1) -> беззнаковое
    EXPECT_EQ(cpu.gpr[4], 128u);

    cpu.gpr[5] = 0x1234u;
    one(encI(0x29, 1, 5, 2));          // sh $5,2($1)
    one(encI(0x25, 1, 6, 2));          // lhu $6,2($1)
    EXPECT_EQ(cpu.gpr[6], 0x1234u);
}

// Знаковое расширение halfword в little-endian памяти.
TEST_F(InterpTest, LoadHalfwordSignExtension) {
    cpu.gpr[1] = 0x08802140u;
    // sw $2 -> 0xFFFFFFFF, затем lh читает младшие два байта: FF FF -> -1
    cpu.gpr[2] = 0xFFFFFFFFu;
    one(encI(0x2B, 1, 2, 0));
    one(encI(0x21, 1, 3, 0));
    EXPECT_EQ(cpu.gpr[3], 0xFFFFFFFFu);

    // lw того же слова
    one(encI(0x23, 1, 4, 0));
    EXPECT_EQ(cpu.gpr[4], 0xFFFFFFFFu);
}

// Загрузка байта с расширением знака: 0x80 -> -128, 0x7F -> 127.
TEST_F(InterpTest, LoadByteSignExtension) {
    cpu.gpr[1] = 0x08802180u;
    mem.Write8(0x08802180u, 0x80u);
    mem.Write8(0x08802181u, 0x7Fu);

    one(encI(0x20, 1, 2, 0));          // lb $2,0($1) -> -128
    EXPECT_EQ(cpu.gpr[2], 0xFFFFFF80u);

    one(encI(0x20, 1, 3, 1));          // lb $3,1($1) -> 127
    EXPECT_EQ(cpu.gpr[3], 127u);

    one(encI(0x24, 1, 4, 0));          // lbu $4,0($1) -> 128
    EXPECT_EQ(cpu.gpr[4], 128u);
}

TEST_F(InterpTest, NegativeOffsetLoad) {
    cpu.gpr[1] = 0x08802200u;
    cpu.gpr[2] = 0xAAAA5555u;
    one(encI(0x2B, 1, 2, -0x100));       // sw $2,-0x100($1)
    one(encI(0x23, 1, 3, -0x100));       // lw $3,-0x100($1)
    EXPECT_EQ(cpu.gpr[3], 0xAAAA5555u);
}

TEST_F(InterpTest, BranchTaken) {
    cpu.gpr[1] = 5;
    cpu.gpr[2] = 5;
    const uint32_t next = one(encI(0x04, 1, 2, 2)); // beq $1,$2,+2
    // Цель = pc + 4 + (2 << 2) = pc + 12
    EXPECT_EQ(next, kTestPC + 12u);
}

TEST_F(InterpTest, BranchNotTaken) {
    cpu.gpr[1] = 5;
    cpu.gpr[2] = 6;
    const uint32_t next = one(encI(0x04, 1, 2, 2)); // beq (не берётся)
    EXPECT_EQ(next, kTestPC + 4u);
}

TEST_F(InterpTest, Bne) {
    cpu.gpr[1] = 5;
    cpu.gpr[2] = 6;
    const uint32_t next = one(encI(0x05, 1, 2, 1)); // bne $1,$2,+1
    EXPECT_EQ(next, kTestPC + 8u);
}

TEST_F(InterpTest, JumpAndLink) {
    const uint32_t target = 0x08805000u;
    const uint32_t next = one(encJ(0x03, target)); // jal target
    EXPECT_EQ(next, target);
    // Адрес возврата указывает за инструкцию в слоте задержки
    EXPECT_EQ(cpu.gpr[31], kTestPC + 8u);
}

TEST_F(InterpTest, JumpRegister) {
    cpu.gpr[31] = 0x08806000u;
    const uint32_t next = one(encR(31, 0, 0, 0, 0x08)); // jr $31
    EXPECT_EQ(next, 0x08806000u);
}

TEST_F(InterpTest, SltAndSltu) {
    cpu.gpr[1] = static_cast<uint32_t>(-1);   // 0xFFFFFFFF
    cpu.gpr[2] = 1;

    one(encR(1, 2, 3, 0, 0x2A)); // slt $3,$1,$2  -> -1 < 1 -> 1
    EXPECT_EQ(cpu.gpr[3], 1u);

    one(encR(1, 2, 4, 0, 0x2B)); // sltu $4,$1,$2 -> 0xFFFFFFFF < 1 -> 0
    EXPECT_EQ(cpu.gpr[4], 0u);
}

// ---- полноценный пошаговый прогон со слотами задержки ----

TEST_F(InterpTest, DelaySlotExecutesBeforeBranchTarget) {
    const uint32_t start  = 0x08803000u;
    const uint32_t target = 0x08803100u;

    // Ветвление со смещением, приводящим ровно к target:
    // target = PC + 4 + (imm << 2)  =>  imm = (target - PC - 4) / 4
    const int32_t imm = static_cast<int32_t>((target - start - 4) / 4);

    mem.Write32(start,      encI(0x04, 0, 0, imm));   // beq $0,$0 -> target
    mem.Write32(start + 4,  encI(0x09, 0, 3, 7));    // слот задержки
    mem.Write32(target,     encI(0x09, 0, 4, 9));    // цель

    cpu.Reset(start);
    const uint64_t executed = interp.Run(3);

    EXPECT_EQ(cpu.gpr[3], 7u);   // инструкция в слоте задержки выполнена
    EXPECT_EQ(cpu.gpr[4], 9u);   // цель достигнута
    EXPECT_EQ(cpu.pc, target + 4u);
    EXPECT_EQ(executed, 3u);
}

TEST_F(InterpTest, DelaySlotIsSkippedForJump) {
    const uint32_t start  = 0x08803300u;
    const uint32_t target = 0x08803400u;

    mem.Write32(start,     0x0C000000u | (target >> 2));  // j target
    mem.Write32(start + 4, encI(0x09, 0, 3, 7));          // слот задержки ВСЕГДА
    mem.Write32(start + 8, encI(0x09, 0, 4, 9));          // не выполняется
    mem.Write32(target,    encI(0x09, 0, 5, 11));

    cpu.Reset(start);
    interp.Run(3);

    EXPECT_EQ(cpu.gpr[3], 7u);    // слот задержки выполнен
    EXPECT_EQ(cpu.gpr[4], 0u);    // инструкция после слота пропущена
    EXPECT_EQ(cpu.gpr[5], 11u);   // цель достигнута
    EXPECT_EQ(cpu.pc, target + 4u);
}

TEST_F(InterpTest, RunCountersAndHalt) {
    const uint32_t start = 0x08803200u;
    for (int i = 0; i < 10; ++i) {
        mem.Write32(start + i * 4, encI(0x09, 0, 0, 0)); // addiu $0,$0,0
    }

    cpu.Reset(start);
    const uint64_t executed = interp.Run(5);

    EXPECT_EQ(executed, 5u);
    EXPECT_EQ(cpu.instructionsExecuted, 5u);
    EXPECT_EQ(cpu.pc, start + 20u);
}

// =========================================================================
// РЕГРЕССИЯ: адрес возврата JAL / JALR
//
// Адрес возврата определяется адресом самой инструкции: PC + 8
// (инструкция после слота задержки). Раньше он вычислялся из изменяемого
// cpu.nextPC, который к моменту диспетчеризации в Step() уже равен PC+8,
// из-за чего получалось PC+12. Пути Step() и StepOne() давали разные
// значения. Здесь проверяется согласованность всех путей исполнения.
// =========================================================================

static uint32_t encJalr(uint32_t rs, uint32_t rd) { return encR(rs, 0, rd, 0, 0x09); }
static uint32_t encJr(uint32_t rs) { return encR(rs, 0, 0, 0, 0x08); }

// --- одиночный шаг через Step() ---

TEST_F(InterpTest, JalLinkViaStep) {
    const uint32_t target = 0x08805000u;
    mem.Write32(kTestPC, encJ(0x03, target));          // jal target
    mem.Write32(kTestPC + 4, encI(0x09, 0, 0, 0));     // слот задержки: nop
    mem.Write32(target, encI(0x09, 0, 0, 0));

    cpu.Reset(kTestPC);
    interp.Step();

    EXPECT_EQ(cpu.gpr[31], kTestPC + 8u)
        << "JAL через Step(): адрес возврата обязан быть PC+8";
    EXPECT_EQ(cpu.nextPC, target);
}

TEST_F(InterpTest, JalrLinkViaStep) {
    const uint32_t target = 0x08805100u;
    mem.Write32(kTestPC, encJalr(5, 31));              // jalr $31, $5
    mem.Write32(kTestPC + 4, encI(0x09, 0, 0, 0));
    mem.Write32(target, encI(0x09, 0, 0, 0));

    cpu.Reset(kTestPC);
    cpu.gpr[5] = target;
    interp.Step();

    EXPECT_EQ(cpu.gpr[31], kTestPC + 8u)
        << "JALR через Step(): адрес возврата обязан быть PC+8";
    EXPECT_EQ(cpu.nextPC, target);
}

// --- одиночный шаг через StepOne() ---

TEST_F(InterpTest, JalLinkViaStepOne) {
    const uint32_t target = 0x08805200u;
    const uint32_t next = interp.StepOne(kTestPC, encJ(0x03, target));
    EXPECT_EQ(cpu.gpr[31], kTestPC + 8u);
    EXPECT_EQ(next, target);
}

TEST_F(InterpTest, JalrLinkViaStepOne) {
    const uint32_t target = 0x08805300u;
    cpu.gpr[5] = target;
    const uint32_t next = interp.StepOne(kTestPC, encJalr(5, 31));
    EXPECT_EQ(cpu.gpr[31], kTestPC + 8u);
    EXPECT_EQ(next, target);
}

// --- прогон через Run() ---

TEST_F(InterpTest, JalLinkViaRun) {
    const uint32_t start  = 0x08805400u;
    const uint32_t target = 0x08805500u;

    mem.Write32(start, encJ(0x03, target));           // jal
    mem.Write32(start + 4, encI(0x09, 0, 8, 0x11));   // слот задержки
    mem.Write32(target, encI(0x09, 0, 9, 0x22));

    cpu.Reset(start);
    interp.Run(3);

    EXPECT_EQ(cpu.gpr[31], start + 8u);
    EXPECT_EQ(cpu.gpr[8], 0x11u);    // слот задержки выполнен
    EXPECT_EQ(cpu.gpr[9], 0x22u);    // цель достигнута
}

TEST_F(InterpTest, JalrLinkViaRun) {
    const uint32_t start  = 0x08805600u;
    const uint32_t target = 0x08805700u;

    mem.Write32(start, encJalr(5, 31));
    mem.Write32(start + 4, encI(0x09, 0, 8, 0x33));
    mem.Write32(target, encI(0x09, 0, 9, 0x44));

    cpu.Reset(start);
    cpu.gpr[5] = target;
    interp.Run(3);

    EXPECT_EQ(cpu.gpr[31], start + 8u);
    EXPECT_EQ(cpu.gpr[8], 0x33u);
    EXPECT_EQ(cpu.gpr[9], 0x44u);
}

// --- вызов и возврат: jal ... jr $ra ---

TEST_F(InterpTest, CallAndReturnThroughJrRa) {
    //   main:   jal    sub
    //           addiu  $4, $0, 7      <- должно выполниться после возврата
    //   sub:    addiu  $3, $0, 99
    //           jr     $31
    //           nop
    const uint32_t main_ = 0x08805800u;
    const uint32_t sub_  = 0x08805900u;

    mem.Write32(main_, encJ(0x03, sub_));
    mem.Write32(main_ + 4, encI(0x09, 0, 0, 0));           // слот задержки jal
    mem.Write32(main_ + 8, encI(0x09, 0, 4, 7));           // выполнится после ret

    mem.Write32(sub_, encI(0x09, 0, 3, 99));
    mem.Write32(sub_ + 4, encJr(31));
    mem.Write32(sub_ + 8, encI(0x09, 0, 0, 0));            // слот задержки jr

    cpu.Reset(main_);
    // 3 инструкции main (jal, слот) + 2 подпрограммы (addiu, jr) + 1 слот jr
    // + 1 инструкция после возврата
    interp.Run(6);

    EXPECT_EQ(cpu.gpr[3], 99u);   // подпрограмма выполнилась
    EXPECT_EQ(cpu.gpr[4], 7u);    // выполнение продолжилось после возврата
}

// --- регистр назначения JALR ---

TEST_F(InterpTest, JalrUsesRdAsLinkRegister) {
    const uint32_t target = 0x08805A00u;
    mem.Write32(kTestPC, encJalr(5, 6));   // jalr $6, $5
    mem.Write32(kTestPC + 4, encI(0x09, 0, 0, 0));

    cpu.Reset(kTestPC);
    cpu.gpr[5] = target;
    interp.Step();

    EXPECT_EQ(cpu.gpr[6], kTestPC + 8u);
    EXPECT_EQ(cpu.gpr[31], 0u) << "JALR не должен трогать $ra, если rd = 6";
}

TEST_F(InterpTest, JalrWithRdZeroDoesNotWriteLink) {
    const uint32_t target = 0x08805B00u;
    mem.Write32(kTestPC, encJalr(5, 0));    // jalr $0, $5
    mem.Write32(kTestPC + 4, encI(0x09, 0, 0, 0));

    cpu.Reset(kTestPC);
    cpu.gpr[5] = target;
    cpu.gpr[31] = 0xDEADBEEFu;
    interp.Step();

    EXPECT_EQ(cpu.gpr[31], 0xDEADBEEFu) << "jalr $0,$5 не должен писать $ra";
}

// --- согласованность всех путей ---

TEST_F(InterpTest, AllExecutionPathsAgreeOnJalLink) {
    const uint32_t target = 0x08805C00u;
    const uint32_t word   = encJ(0x03, target);

    uint32_t viaStepOne = 0;
    {
        core::CPUState c; c.Reset(kTestPC);
        interp::Interpreter i(c, mem);
        i.StepOne(kTestPC, word);
        viaStepOne = c.gpr[31];
    }
    uint32_t viaStep = 0;
    {
        core::CPUState c; c.Reset(kTestPC);
        interp::Interpreter i(c, mem);
        mem.Write32(kTestPC, word);
        i.Step();
        viaStep = c.gpr[31];
    }
    uint32_t viaRun = 0;
    {
        core::CPUState c; c.Reset(kTestPC);
        interp::Interpreter i(c, mem);
        mem.Write32(kTestPC, word);
        mem.Write32(kTestPC + 4, encI(0x09, 0, 0, 0));
        mem.Write32(target, encI(0x09, 0, 0, 0));
        i.Run(1);
        viaRun = c.gpr[31];
    }

    EXPECT_EQ(viaStepOne, kTestPC + 8u);
    EXPECT_EQ(viaStep, viaStepOne) << "Step() и StepOne() расходятся";
    EXPECT_EQ(viaRun, viaStepOne)   << "Run() и StepOne() расходятся";
}

// --- слот задержки ---

TEST_F(InterpTest, DelaySlotExecutesForJalBeforeTarget) {
    const uint32_t start  = 0x08805D00u;
    const uint32_t target = 0x08805E00u;

    mem.Write32(start, encJ(0x03, target));
    mem.Write32(start + 4, encI(0x09, 0, 5, 0x77));   // слот задержки
    mem.Write32(start + 8, encI(0x09, 0, 6, 0x88));   // не выполняется
    mem.Write32(target, encI(0x09, 0, 7, 0x99));

    cpu.Reset(start);
    interp.Run(3);

    EXPECT_EQ(cpu.gpr[5], 0x77u) << "слот задержки JAL не выполнен";
    EXPECT_EQ(cpu.gpr[6], 0u)   << "инструкция после слота выполнена";
    EXPECT_EQ(cpu.gpr[7], 0x99u);
    EXPECT_EQ(cpu.pc, target + 4u);
}

TEST_F(InterpTest, DelaySlotExecutesForJalrBeforeTarget) {
    const uint32_t start  = 0x08805F00u;
    const uint32_t target = 0x08806000u;

    mem.Write32(start, encJalr(5, 31));
    mem.Write32(start + 4, encI(0x09, 0, 6, 0x77));
    mem.Write32(start + 8, encI(0x09, 0, 7, 0x88));   // не выполняется
    mem.Write32(target, encI(0x09, 0, 8, 0x99));

    cpu.Reset(start);
    cpu.gpr[5] = target;
    interp.Run(3);

    EXPECT_EQ(cpu.gpr[6], 0x77u);
    EXPECT_EQ(cpu.gpr[7], 0u);
    EXPECT_EQ(cpu.gpr[8], 0x99u);
    EXPECT_EQ(cpu.pc, target + 4u);
}

} // namespace