// tests/test_cpu.cpp — тесты состояния процессора, прерываний и исключений
//
// Прерывания и состояние CP0 настраиваются НАСТОЯЩИМИ инструкциями
// mtc0/mfc0, а не прямым изменением внутренних полей: отдельного
// регистра Interrupt Mask на Allegrex нет, маска IM живёт в Status
// (биты 15..8), и гостевой код обязан иметь к ней доступ.
#include <gtest/gtest.h>

#include "core/cpu_state.h"
#include "core/decoder.h"
#include "core/memory.h"
#include "interp/interpreter.h"

using ppsspp::core::CPUState;
using ppsspp::core::PC_HALTED;
using ppsspp::core::Decoded;
using ppsspp::core::ExcCode;
using ppsspp::interp::Interpreter;
using ppsspp::interp::StepResult;

namespace {

// Кодировщики инструкций COP0.
//   mfc0 rt, rd : COP0 | 00000 | rt | rd | 00000 | 00000
//   mtc0 rt, rd : COP0 | 00000 | rt | rd | 00000 | 00100
static inline uint32_t encMfc0(uint32_t rt, uint32_t rd) {
    return (0x10u << 26) | (rt << 16) | (rd << 11) | 0x00u;
}
static inline uint32_t encMtc0(uint32_t rt, uint32_t rd) {
    return (0x10u << 26) | (rt << 16) | (rd << 11) | 0x04u;
}
static inline uint32_t encI(uint32_t op, uint32_t rs, uint32_t rt, int32_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) |
           (static_cast<uint32_t>(imm) & 0xFFFF);
}
static inline uint32_t encAdd(uint32_t rd, uint32_t rs, uint32_t rt) {
    return (rs << 21) | (rt << 16) | (rd << 11) | 0x20u;   // SPECIAL, ADD
}
static inline uint32_t encAddu(uint32_t rd, uint32_t rs, uint32_t rt) {
    return (rs << 21) | (rt << 16) | (rd << 11) | 0x21u;   // SPECIAL, ADDU
}
static inline uint32_t encSub(uint32_t rd, uint32_t rs, uint32_t rt) {
    return (rs << 21) | (rt << 16) | (rd << 11) | 0x22u;   // SPECIAL, SUB
}
static inline uint32_t encSubu(uint32_t rd, uint32_t rs, uint32_t rt) {
    return (rs << 21) | (rt << 16) | (rd << 11) | 0x23u;   // SPECIAL, SUBU
}
static inline uint32_t encAddi(uint32_t rt, uint32_t rs, int32_t imm) {
    return (0x08u << 26) | (rs << 21) | (rt << 16) |
           (static_cast<uint32_t>(imm) & 0xFFFFu);
}
static inline uint32_t encAddiuRaw(uint32_t rt, uint32_t rs, int32_t imm) {
    return (0x09u << 26) | (rs << 21) | (rt << 16) |
           (static_cast<uint32_t>(imm) & 0xFFFFu);
}

// Разбор ExcCode из Cause.
static inline uint32_t ExcCodeOf(const CPUState& cpu) {
    return (cpu.cp0_Cause & ppsspp::core::CP0_CAUSE_EXC) >> 2;
}

TEST(CPUStateTest, DefaultConstruction) {
    CPUState cpu;
    EXPECT_EQ(cpu.pc, 0u);
    EXPECT_EQ(cpu.nextPC, 4u);
    for (int i = 0; i < 32; ++i) EXPECT_EQ(cpu.gpr[i], 0u);
    EXPECT_FALSE(cpu.interruptPending);
    EXPECT_EQ(cpu.instructionsExecuted, 0u);
}

TEST(CPUStateTest, ResetClearsEverything) {
    CPUState cpu;
    cpu.gpr[5] = 0xDEAD;
    cpu.hi = 1; cpu.lo = 2;
    cpu.vpr[10][0] = 3.5f;
    cpu.cp0_Status = 0xFFFFFFFFu;
    cpu.cp0_EBase  = 0xBFC00000u;
    cpu.instructionsExecuted = 100;
    cpu.llBit = true;

    cpu.Reset(0x08800000u);

    EXPECT_EQ(cpu.pc, 0x08800000u);
    EXPECT_EQ(cpu.nextPC, 0x08800004u);
    EXPECT_EQ(cpu.gpr[5], 0u);
    EXPECT_EQ(cpu.hi, 0u);
    EXPECT_EQ(cpu.lo, 0u);
    EXPECT_FLOAT_EQ(cpu.vpr[10][0], 0.0f);
    EXPECT_EQ(cpu.cp0_Status, 0u);
    EXPECT_EQ(cpu.cp0_EBase, 0u);
    EXPECT_FALSE(cpu.llBit);
    EXPECT_EQ(cpu.instructionsExecuted, 0u);
    EXPECT_FALSE(cpu.exceptionPending);
    EXPECT_FALSE(cpu.inDelaySlot);
}

// РЕГРЕССИЯ: cp0_IM не инициализировался в Reset(), поле читалось
// как мусор. Отдельного регистра IM на Allegrex нет — маска живёт
// в Status, и она обязана обнуляться.
TEST(CPUStateTest, ResetClearsInterruptMaskInStatus) {
    CPUState cpu;
    cpu.SetInterruptMask(0xFFu);
    EXPECT_EQ(cpu.InterruptMask(), 0xFFu);

    cpu.Reset(0x08800000u);
    EXPECT_EQ(cpu.InterruptMask(), 0u)
        << "маска прерываний обязана обнуляться вместе с Status";
    EXPECT_EQ(cpu.cp0_Status, 0u);
}

TEST(CPUStateTest, HaltedPcSurvivesReset) {
    CPUState cpu;
    cpu.Reset(PC_HALTED);
    EXPECT_EQ(cpu.pc, PC_HALTED);
    EXPECT_EQ(cpu.nextPC, PC_HALTED);
}

TEST(CPUStateTest, RegisterAccessMasksIndex) {
    CPUState cpu;
    cpu.SetGPR(33, 0x1234);      // 33 & 31 = 1
    EXPECT_EQ(cpu.gpr[1], 0x1234u);
    EXPECT_EQ(cpu.GetGPR(1), 0x1234u);
}

TEST(CPUStateTest, StepOneSetsScratchFields) {
    CPUState cpu;
    cpu.StepOne(0x08801000u, 0x2402002Au);
    EXPECT_EQ(cpu.scratchPC, 0x08801000u);
    EXPECT_EQ(cpu.scratchInst, 0x2402002Au);
}

// =========================================================================
// РЕГРЕССИЯ: декодирование MFC0 / MTC0
//
// Раньше обработчик переключался на d.rt — это НОМЕР РЕГИСТРА GPR,
// а не вид операции. В результате mfc0 $t0,$12 (слово 0x40086010)
// не распознавалось вовсе. Вид операции лежит в битах 4..0.
// =========================================================================

TEST(Cop0DecodeTest, Mfc0OperationSelector) {
    // mfc0 $t0, $12 (Status):
    //   COP0 | rs5=00000 | rt=01000 | rd=01100 | 00000 | 00000
    const uint32_t word = encMfc0(8, 12);
    EXPECT_EQ(word, 0x40086000u);      // контрольное машинное слово

    const Decoded d = ppsspp::core::Decode(0, word);
    EXPECT_EQ(d.opcode, 0x10u) << "опкод COP0";
    EXPECT_EQ(d.rt, 8u) << "rt — регистр GPR";
    EXPECT_EQ(d.rd, 12u) << "rd — регистр CP0";
    EXPECT_EQ(d.copRs, ppsspp::core::CopRs::MFC0);
    EXPECT_EQ(d.copOp, ppsspp::core::CopField::MF);
    EXPECT_EQ(d.copKind, ppsspp::core::COP0_MFC0);
    // Старый обработчик переключался на d.rt (== 8) и не находил
    // ни MFC0 (0), ни MTC0 (4) — инструкция уходила в нереализованные.
    EXPECT_NE(d.rt, ppsspp::core::CopField::MF);
}

TEST(Cop0DecodeTest, Mtc0OperationSelector) {
    const Decoded d = ppsspp::core::Decode(0, encMtc0(8, 12));
    // Схема MIPS32: rs5 = 0, младшие биты = 4.
    EXPECT_EQ(d.copRs, 0u);
    EXPECT_EQ(d.copOp, ppsspp::core::CopField::MT);
    EXPECT_EQ(d.copKind, ppsspp::core::COP0_MTC0);
    EXPECT_EQ(d.rt, 8u);
    EXPECT_EQ(d.rd, 12u);
}

TEST(Cop0DecodeTest, TlbOperationIsNotCop0RegisterAccess) {
    // rs5 = 1 -> TLB-инструкция (tlbr/tlbwi/...), не регистры CPU.
    const uint32_t word = (0x10u << 26) | (1u << 21) | (0u << 16) | 0x10u;
    const Decoded d = ppsspp::core::Decode(0, word);
    EXPECT_EQ(d.copRs, 1u);
    EXPECT_EQ(d.copKind, ppsspp::core::COP0_UNKNOWN)
        << "TLB-инструкция не должна распознаваться как доступ к CP0";
}

// MFC0 / MTC0 через настоящий интерпретатор.
class Cop0Test : public ::testing::Test {
protected:
    ppsspp::core::Memory mem;
    CPUState cpu;
    Interpreter interp;

    Cop0Test() : interp(cpu, mem) {
        mem.Init();
        cpu.Reset(0x08800000u);
    }
    void run(uint32_t word, int n = 1) {
        mem.Write32(cpu.pc, word);
        for (int i = 0; i < n; ++i) {
            const uint32_t at = cpu.pc;
            mem.Write32(at, word);
            interp.Step();
            (void)at;
        }
    }
};

TEST_F(Cop0Test, Mfc0ReadsStatusThroughGuestInstruction) {
    cpu.cp0_Status = 0x0000FF01u;
    const uint64_t before = interp.unimplementedCount();

    interp.StepInstruction(0x08800000u, encMfc0(8, ppsspp::core::CP0_STATUS));

    EXPECT_EQ(cpu.gpr[8], 0x0000FF01u) << "mfc0 $t0,Status не сработала";
    EXPECT_EQ(interp.unimplementedCount(), before)
        << "инструкция не должна считаться нереализованной";
}

TEST_F(Cop0Test, Mtc0ThenMfc0RoundTrip) {
    // mtc0 $t0, Status ; mfc0 $t1, Status
    interp.StepInstruction(0x08800000u, encMtc0(8, ppsspp::core::CP0_STATUS));
    cpu.gpr[8] = 0x00030100u;          // IM = 0xFF, IE = 1
    interp.StepInstruction(0x08800004u, encMtc0(8, ppsspp::core::CP0_STATUS));

    EXPECT_EQ(cpu.cp0_Status, 0x00030100u);

    interp.StepInstruction(0x08800008u, encMfc0(9, ppsspp::core::CP0_STATUS));
    EXPECT_EQ(cpu.gpr[9], 0x00030100u);
}

TEST_F(Cop0Test, Mfc0IntoZeroRegisterIsIgnored) {
    cpu.cp0_EPC = 0x1234u;
    cpu.gpr[0] = 0xFFFFFFFFu;
    interp.StepInstruction(0x08800000u, encMfc0(0, ppsspp::core::CP0_EPC));
    EXPECT_EQ(cpu.gpr[0], 0xFFFFFFFFu) << "mfc0 $0,rd не должен писать $0";
}

TEST_F(Cop0Test, TlbOperationIsNotHandled) {
    const uint32_t word = (0x10u << 26) | (1u << 21) | (0u << 16) | 0x10u;
    const uint64_t before = interp.unimplementedCount();
    interp.StepInstruction(0x08800000u, word);
    EXPECT_EQ(interp.unimplementedCount(), before + 1);
}

// =========================================================================
// Прерывания: маска настраивается через mtc0 Status
// =========================================================================

TEST_F(Cop0Test, InterruptMaskIsGuestAccessible) {
    // Через mtc0 Status гостевой код включает линии 0..7 и IE.
    cpu.gpr[8] = 0x0000FF01u;
    interp.StepInstruction(0x08800000u, encMtc0(8, ppsspp::core::CP0_STATUS));

    EXPECT_EQ(cpu.InterruptMask(), 0xFFu) << "IM обязан извлекаться из Status";
    EXPECT_NE(cpu.cp0_Status & ppsspp::core::CP0_STATUS_IE, 0u);
}

TEST_F(Cop0Test, InterruptIsDeliveredThroughGuestSetup) {
    // 1) разрешить прерывания через mtc0 Status
    cpu.gpr[8] = 0x0000FF01u;                       // IM = FF, IE = 1
    interp.StepInstruction(0x08800000u, encMtc0(8, ppsspp::core::CP0_STATUS));
    // 2) возбудить линию 3
    cpu.RaiseInterrupt(3);
    EXPECT_TRUE(cpu.ShouldTakeInterrupt());

    const uint32_t vector = cpu.TakeInterruptVector();
    // ExcCode 0 (Int) + irq*0x20
    EXPECT_EQ(vector, 0x100u + 3u * 0x20u);
    EXPECT_EQ(cpu.cp0_EPC, cpu.pc) << "EPC указывает на прерываемую инструкцию";
    EXPECT_NE(cpu.cp0_Status & ppsspp::core::CP0_STATUS_EXL, 0u);
    EXPECT_EQ(cpu.cp0_Status & ppsspp::core::CP0_STATUS_IE, 0u);
}

TEST(CPUStateTest, InterruptRequiresEnableAndMask) {
    CPUState cpu;

    cpu.RaiseInterrupt(3);
    EXPECT_TRUE(cpu.interruptPending);

    // Прерывания замаскированы и IE=0
    EXPECT_FALSE(cpu.ShouldTakeInterrupt());

    cpu.SetInterruptMask(0xFF);
    EXPECT_FALSE(cpu.ShouldTakeInterrupt());

    // Включаем прерывания
    cpu.cp0_Status |= ppsspp::core::CP0_STATUS_IE;
    EXPECT_TRUE(cpu.ShouldTakeInterrupt());
}

TEST(CPUStateTest, UnmaskedInterruptIsNotTaken) {
    CPUState cpu;
    cpu.cp0_Status |= ppsspp::core::CP0_STATUS_IE;
    cpu.RaiseInterrupt(2);
    cpu.SetInterruptMask(0x02);     // маскируем линию 2, но не 1
    EXPECT_FALSE(cpu.ShouldTakeInterrupt());
}

TEST(CPUStateTest, ExceptionLevelBlocksInterrupts) {
    CPUState cpu;
    cpu.cp0_Status |= ppsspp::core::CP0_STATUS_IE;
    cpu.SetInterruptMask(0xFF);
    cpu.RaiseInterrupt(1);
    EXPECT_TRUE(cpu.ShouldTakeInterrupt());

    cpu.cp0_Status |= ppsspp::core::CP0_STATUS_EXL;
    EXPECT_FALSE(cpu.ShouldTakeInterrupt());
}

TEST(CPUStateTest, ClearInterrupt) {
    CPUState cpu;
    cpu.RaiseInterrupt(5);
    EXPECT_TRUE(cpu.interruptPending);
    cpu.ClearInterrupt(5);
    EXPECT_FALSE(cpu.interruptPending);
}

TEST(CPUStateTest, TakeInterruptVectorSetsEpcAndExl) {
    CPUState cpu;
    cpu.Reset(0x08805000u);
    cpu.cp0_Status |= ppsspp::core::CP0_STATUS_IE;
    cpu.SetInterruptMask(0xFF);
    cpu.RaiseInterrupt(0);

    const uint32_t vector = cpu.TakeInterruptVector();
    EXPECT_NE(vector, 0u);
    EXPECT_EQ(cpu.cp0_EPC, 0x08805000u);
    EXPECT_NE(cpu.cp0_Status & ppsspp::core::CP0_STATUS_EXL, 0u);   // EXL установлен
    EXPECT_EQ(cpu.cp0_Status & ppsspp::core::CP0_STATUS_IE, 0u);   // IE сброшен

    // Повторный вызов ничего не делает, пока EXL не сброшен
    EXPECT_EQ(cpu.TakeInterruptVector(), 0u);
}

TEST(CPUStateTest, EBaseShiftsExceptionVectors) {
    CPUState cpu;
    cpu.cp0_EBase = 0xBFC00000u;
    EXPECT_EQ(cpu.InterruptVector(0), 0xBFC00100u);
    EXPECT_EQ(cpu.InterruptVector(3), 0xBFC00160u);
    EXPECT_EQ(cpu.ExceptionVector(ExcCode::Sys), 0xBFC00200u);
    EXPECT_EQ(cpu.ExceptionVector(ExcCode::Ov), 0xBFC00280u);
}

// MIPS32 Vol. III: EBase используется только при Status.BEV = 0.
// При BEV = 1 действуют фиксированные загрузочные векторы, и все
// исключения сходятся на одном адресе.
TEST(CPUStateTest, BevSelectsBootVectors) {
    CPUState cpu;
    cpu.cp0_EBase = 0xBFC00000u;

    cpu.cp0_Status |= ppsspp::core::CP0_STATUS_BEV;
    EXPECT_EQ(cpu.ExceptionVector(ExcCode::Ov), 0x00000180u)
        << "при BEV=1 все общие исключения идут на 0x180";
    EXPECT_EQ(cpu.ExceptionVector(ExcCode::Sys), 0x00000180u);
    EXPECT_EQ(cpu.ExceptionVector(ExcCode::Bp), 0x00000180u);
    EXPECT_EQ(cpu.InterruptVector(0), 0x00000380u);
    EXPECT_EQ(cpu.InterruptVector(4), 0x00000380u + 4u * 0x20u)
        << "при BEV=1 вектор прерывания отсчитывается от 0x380";

    // Сброс BEV возвращает адресацию через EBase.
    cpu.cp0_Status &= ~ppsspp::core::CP0_STATUS_BEV;
    EXPECT_EQ(cpu.ExceptionVector(ExcCode::Ov), 0xBFC00280u);
    EXPECT_EQ(cpu.InterruptVector(0), 0xBFC00100u);
}

// EBase выставляется гостевым кодом через mtc0, поэтому BEV и EBase
// должны влиять на вектор независимо.
TEST_F(Cop0Test, BevIsGuestControllable) {
    cpu.gpr[8] = 0x00400000u;                 // Status = BEV
    interp.StepInstruction(0x08800000u, encMtc0(8, ppsspp::core::CP0_STATUS));
    EXPECT_NE(cpu.cp0_Status & ppsspp::core::CP0_STATUS_BEV, 0u);
    EXPECT_EQ(cpu.ExceptionVector(ExcCode::Ov), 0x00000180u);
}

// ---- вектор прерывания не должен совпадать с вектором исключения ----
TEST(CPUStateTest, InterruptAndExceptionVectorsDiffer) {
    CPUState cpu;
    // ExcCode 0 = Int. Вектор прерывания irq=0 обязан совпадать с
    // общим вектором, а irq>0 — отличаться.
    EXPECT_EQ(cpu.InterruptVector(0), cpu.ExceptionVector(ExcCode::Int));
    EXPECT_NE(cpu.InterruptVector(1), cpu.ExceptionVector(ExcCode::Int));
    EXPECT_NE(cpu.InterruptVector(1), cpu.ExceptionVector(ExcCode::Ov));
}

// =========================================================================
// РЕГРЕССИЯ: переполнение ADD больше не останавливает эмулятор
// =========================================================================

class OverflowTest : public ::testing::Test {
protected:
    ppsspp::core::Memory mem;
    CPUState cpu;
    Interpreter interp;
    static constexpr uint32_t kStart = 0x08808000u;

    OverflowTest() : interp(cpu, mem) {
        mem.Init();
        cpu.Reset(kStart);
    }
    // Кладёт инструкцию по текущему pc и выполняет её через полный путь
    // Step(): он же обрабатывает перенаправление PC по вектору.
    StepResult step(uint32_t word) {
        mem.Write32(cpu.pc, word);
        return interp.Step();
    }
};

TEST_F(OverflowTest, AddOverflowRaisesExceptionInsteadOfHalting) {
    cpu.gpr[8] = 0x7FFFFFFFu;
    cpu.gpr[9] = 0x00000001u;

    const StepResult r = step(encAdd(10, 8, 9));

    EXPECT_EQ(r, StepResult::Exception);
    EXPECT_NE(cpu.pc, PC_HALTED) << "эмулятор не должен останавливаться";
    EXPECT_FALSE(cpu.exceptionPending) << "флаг должен быть снят после обработки";

    EXPECT_EQ(ExcCodeOf(cpu), static_cast<uint32_t>(ExcCode::Ov))
        << "код исключения обязан быть Ov (12)";
    EXPECT_EQ(cpu.cp0_EPC, kStart);
    EXPECT_EQ(cpu.cp0_Status & ppsspp::core::CP0_STATUS_EXL,
              ppsspp::core::CP0_STATUS_EXL);
    EXPECT_EQ(cpu.cp0_Status & ppsspp::core::CP0_STATUS_IE, 0u);
    EXPECT_EQ(cpu.gpr[10], 0u) << "при переполнении результат в rd не пишется";
    EXPECT_EQ(cpu.pc, 0x280u) << "PC перенаправлен на вектор Ov";
}

TEST_F(OverflowTest, AdduDoesNotTrap) {
    cpu.gpr[8] = 0x7FFFFFFFu;
    cpu.gpr[9] = 0x00000001u;

    const StepResult r = step(encAddu(10, 8, 9));

    EXPECT_NE(r, StepResult::Exception);
    EXPECT_EQ(cpu.gpr[10], 0x80000000u) << "ADDU не должно возбуждать исключение";
    EXPECT_NE(cpu.pc, PC_HALTED);
    EXPECT_EQ(cpu.gpr[0], 0u);
    // после ADDU исполнение продолжается
    EXPECT_EQ(cpu.pc, kStart + 4u);
}

TEST_F(OverflowTest, AddWithoutOverflowDoesNotTrap) {
    cpu.gpr[8] = 1u;
    cpu.gpr[9] = 2u;

    const StepResult r = step(encAdd(10, 8, 9));

    EXPECT_NE(r, StepResult::Exception);
    EXPECT_EQ(cpu.gpr[10], 3u);
}

TEST_F(OverflowTest, SubStillWritesResult) {
    cpu.gpr[8] = 10u;
    cpu.gpr[9] = 3u;
    const StepResult r = step(encSub(10, 8, 9));   // sub $10, $8, $9
    EXPECT_NE(r, StepResult::Exception);
    EXPECT_EQ(cpu.gpr[10], 7u) << "sub обязан писать rd";
}

TEST_F(OverflowTest, SubOverflowRaisesException) {
    cpu.gpr[8] = 0x80000000u;      // INT32_MIN
    cpu.gpr[9] = 1u;
    const StepResult r = step(encSub(10, 8, 9));
    EXPECT_EQ(r, StepResult::Exception);
    EXPECT_EQ(ExcCodeOf(cpu), static_cast<uint32_t>(ExcCode::Ov));
    EXPECT_EQ(cpu.gpr[10], 0u);
}

TEST_F(OverflowTest, SubuDoesNotTrap) {
    cpu.gpr[8] = 0x80000000u;
    cpu.gpr[9] = 1u;
    const StepResult r = step(encSubu(10, 8, 9));
    EXPECT_NE(r, StepResult::Exception);
    EXPECT_EQ(cpu.gpr[10], 0x7FFFFFFFu);
}

TEST_F(OverflowTest, AddiOverflowRaisesException) {
    cpu.gpr[8] = 0x7FFFFFFFu;

    const StepResult r = step(encAddi(9, 8, 1));

    EXPECT_EQ(r, StepResult::Exception);
    EXPECT_EQ(ExcCodeOf(cpu), static_cast<uint32_t>(ExcCode::Ov));
    EXPECT_EQ(cpu.gpr[9], 0u);
}

TEST_F(OverflowTest, AddiuDoesNotTrap) {
    cpu.gpr[8] = 0x7FFFFFFFu;

    const StepResult r = step(encAddiuRaw(9, 8, 1));

    EXPECT_NE(r, StepResult::Exception);
    EXPECT_EQ(cpu.gpr[9], 0x80000000u);
}

TEST_F(OverflowTest, NegativeAddiOverflowRaisesException) {
    cpu.gpr[8] = 0x80000000u;      // INT32_MIN

    const StepResult r = step(encAddi(9, 8, -1));

    EXPECT_EQ(r, StepResult::Exception);
    EXPECT_EQ(ExcCodeOf(cpu), static_cast<uint32_t>(ExcCode::Ov));
}

TEST_F(OverflowTest, NegativeAddiWithoutOverflowDoesNotTrap) {
    cpu.gpr[8] = 0x7FFFFFFFu;
    const StepResult r = step(encAddi(9, 8, -1));
    EXPECT_NE(r, StepResult::Exception);
    EXPECT_EQ(cpu.gpr[9], 0x7FFFFFFEu);
}

TEST_F(OverflowTest, OverflowInDelaySlotPointsEpcAtBranch) {
    //   beq  $0,$0, target
    //   add  $t0,$t1,$t2     <- переполнение в слоте задержки
    //   nop
    //   target: ...
    const uint32_t target = kStart + 0x40;
    const uint32_t imm = static_cast<uint32_t>((target - kStart - 4) / 4);

    mem.Write32(kStart, encI(0x04, 0, 0, static_cast<int32_t>(imm)));
    mem.Write32(kStart + 4, encAdd(10, 8, 9));
    mem.Write32(kStart + 8, encAddiuRaw(11, 0, 0));
    mem.Write32(target, encAddiuRaw(12, 0, 0));
    cpu.gpr[8] = 0x7FFFFFFFu;
    cpu.gpr[9] = 1u;

    interp.Step();   // beq
    interp.Step();   // add в слоте задержки -> исключение

    EXPECT_NE(cpu.cp0_Cause & ppsspp::core::CP0_CAUSE_BD, 0u)
        << "BD обязан быть установлен для исключения в слоте задержки";
    EXPECT_EQ(cpu.cp0_EPC, kStart)
        << "EPC обязан указывать на инструкцию ветвления, а не на слот";
}

TEST_F(OverflowTest, ExceptionRedirectsPcToHandler) {
    // База векторов выбирается в отображённую область RAM, чтобы обработчик
    // реально был доступен для выборки.
    cpu.cp0_EBase = 0x08808000u;
    const uint32_t vector = cpu.ExceptionVector(ExcCode::Ov);
    EXPECT_EQ(vector, 0x08808000u + 0x100u + 12u * 0x20u);

    mem.Write32(kStart, encAdd(10, 8, 9));
    mem.Write32(vector, encAddiuRaw(20, 0, 0x55));
    cpu.gpr[8] = 0x7FFFFFFFu;
    cpu.gpr[9] = 1u;

    interp.Step();   // add -> исключение
    EXPECT_EQ(cpu.pc, vector);

    interp.Step();   // обработчик
    EXPECT_EQ(cpu.gpr[20], 0x55u) << "управление должно перейти в обработчик";
}

// =========================================================================
// Дополнительные регистры CP0 доступны через mtc0/mfc0
// =========================================================================

TEST_F(Cop0Test, EBaseIsGuestAccessible) {
    cpu.gpr[8] = 0xBFC00000u;
    interp.StepInstruction(0x08800000u, encMtc0(8, ppsspp::core::CP0_EBASE));
    EXPECT_EQ(cpu.cp0_EBase, 0xBFC00000u);
    EXPECT_EQ(cpu.InterruptVector(0), 0xBFC00100u);

    interp.StepInstruction(0x08800004u, encMfc0(9, ppsspp::core::CP0_EBASE));
    EXPECT_EQ(cpu.gpr[9], 0xBFC00000u);
}

TEST_F(Cop0Test, CauseAndEpcAreGuestAccessible) {
    cpu.gpr[8] = 0x0000010Cu;                 // ExcCode = 3, BD = 1
    interp.StepInstruction(0x08800000u, encMtc0(8, ppsspp::core::CP0_CAUSE));
    interp.StepInstruction(0x08800004u, encMfc0(9, ppsspp::core::CP0_CAUSE));
    EXPECT_EQ(cpu.gpr[9], 0x0000010Cu);

    cpu.gpr[8] = 0x08809900u;
    interp.StepInstruction(0x08800008u, encMtc0(8, ppsspp::core::CP0_EPC));
    interp.StepInstruction(0x0880000Cu, encMfc0(9, ppsspp::core::CP0_EPC));
    EXPECT_EQ(cpu.gpr[9], 0x08809900u);
}

TEST_F(Cop0Test, Mtc0ToNonExistingRegisterIsRejected) {
    cpu.gpr[8] = 0xFFFFFFFFu;
    const uint64_t before = interp.unimplementedCount();
    interp.StepInstruction(0x08800000u, encMtc0(8, 7));
    EXPECT_EQ(interp.unimplementedCount(), before + 1);
}

// =========================================================================
// НЕЗАВИСИМЫЙ АУДИТ COP0
//
// Источники:
//   PPSPP Core/MIPS/MIPSTables.cpp:
//     encodingBits[Cop0] = EncodingBitsInfo(21, 5)  -> биты 25..21
//     tableCop0[32] = // 010000 xxxxx ..... ................
//       [0] = mfc0, [4] = mtc0, [10] = rdpgpr, [11] = mfmc0,
//       [14] = wrpgpr, [16..31] = ENCODING(Cop0CO) (бит 25 = 1)
//   PPSPP Core/MIPS/Interpreter.cpp, Int_Cop0:
//     switch ((op >> 21) & 0x1f) { case 0: //mfc0  case 4: //mtc0 ... }
// То есть селектор MF/MT находится в битах 25..21, а НЕ в битах 4..0.
//
// MIPS32 Vol. III при этом задаёт другую раскладку: rs = 0, вид операции
// лежит в битах 4..0 (MF=0, MT=4, CF=2, CT=6), биты 10..5 = 0.
// Поддерживаются обе схемы; всё, что не соответствует ни одной из них,
// должно отвергаться, а не распознаваться.
// =========================================================================

namespace {

// rs5 = биты 25..21, low5 = биты 4..0, code = биты 2..0
inline uint32_t mkCop0(uint32_t rs5, uint32_t rt, uint32_t rd,
                       uint32_t low5, uint32_t code = 0) {
    return (0x10u << 26) | (rs5 << 21) | (rt << 16) | (rd << 11) |
           (low5 & 0x1Fu) | (code & 7u);
}

constexpr uint32_t Cop0Cf = 0x02;   // поле op = CF
constexpr uint32_t Cop0Mt = 0x04;   // поле op = MT
constexpr uint32_t Cop0Ct = 0x06;   // поле op = CT

std::string kindName(uint8_t k) {
    switch (k) {
    case ppsspp::core::COP0_MFC0: return "MFC0";
    case ppsspp::core::COP0_MTC0: return "MTC0";
    case ppsspp::core::COP0_CFC0: return "CFC0";
    case ppsspp::core::COP0_CTC0: return "CTC0";
    default: return "UNKNOWN";
    }
}

} // namespace

TEST(Cop0AuditTest, DecodedCarriesBothSelectorFields) {
    // Слово, у которого rs5 и младшие биты дают разные ответы:
    // rs5 = 4 (как mtc0 у PPSSPP), младшие биты = 0.
    const Decoded d = ppsspp::core::Decode(0, mkCop0(4, 8, 12, 0));

    EXPECT_EQ(d.copRs, 4u) << "селектор по PPSSPP — биты 25..21";
    EXPECT_EQ(d.copOp, 0u) << "поле MIPS32 — биты 4..0";
    EXPECT_EQ(kindName(d.copKind), std::string("MTC0"))
        << "при rs5=4 инструкция обязана распознаваться как mtc0, "
           "а не как mfc0";
}

// Ключевой случай: rs5 = 4 и младшие биты = 0 — это НЕ mfc0.
TEST(Cop0AuditTest, RsFiveFourIsMtc0NotMfc0) {
    const Decoded d = ppsspp::core::Decode(0, mkCop0(4, 8, 12, 0));
    EXPECT_NE(kindName(d.copKind), std::string("MFC0"));
}

TEST(Cop0AuditTest, Mfc0EncodingIsZeroFiveBits) {
    const Decoded d = ppsspp::core::Decode(0, mkCop0(0, 8, 12, 0));
    EXPECT_EQ(kindName(d.copKind), std::string("MFC0"));
}

// Трактовка MIPS32: rs5 = 0, младшие биты = 4 -> тоже mtc0.
TEST(Cop0AuditTest, Mtc0ViaMips32LowBitsIsMtc0) {
    const Decoded d = ppsspp::core::Decode(0, mkCop0(0, 8, 12, 4));
    EXPECT_EQ(kindName(d.copKind), std::string("MTC0"));
}

// --- зарезервированные биты 10..3 обязаны быть нулевыми ---
TEST(Cop0AuditTest, ReservedBitsMustBeZeroForMfc0) {
    for (uint32_t bit = 3; bit <= 10; ++bit) {
        const uint32_t word = mkCop0(0, 8, 12, 0) | (1u << bit);
        const Decoded d = ppsspp::core::Decode(0, word);
        EXPECT_EQ(kindName(d.copKind), std::string("UNKNOWN"))
            << "бит " << bit << " установлен — не должно распознаваться как mfc0";
    }
}

TEST(Cop0AuditTest, ReservedBitsMustBeZeroForMtc0) {
    for (uint32_t bit = 3; bit <= 10; ++bit) {
        const uint32_t word = mkCop0(4, 8, 12, 0) | (1u << bit);
        const Decoded d = ppsspp::core::Decode(0, word);
        EXPECT_EQ(kindName(d.copKind), std::string("UNKNOWN"))
            << "бит " << bit << " установлен — не должно распознаваться как mtc0";
    }
}

// Биты 4..0 — это поле op целиком (MF=0, MT=4, CF=2, CT=6), поэтому
// подфункция bits 2..0 для mtc0 равна 4, а не 0. Проверять, что mfc0
// не превращается в другую инструкцию при ненулевых битах 4..0.
TEST(Cop0AuditTest, NonZeroLowBitsDoNotProduceMfc0) {
    for (uint32_t low = 1; low < 32; ++low) {
        const Decoded d = ppsspp::core::Decode(0, mkCop0(0, 8, 12, low));
        EXPECT_NE(kindName(d.copKind), std::string("MFC0"))
            << "low5=" << low << " не должен распознаваться как mfc0";
    }
}

// Значения опкода 8..15 (бит 3 установлен) — это cache, а не cfc0/ctc0.
TEST(Cop0AuditTest, CacheRangeIsNotCfc0) {
    for (uint32_t low = 8; low < 16; ++low) {
        const Decoded d = ppsspp::core::Decode(0, mkCop0(0, 8, 0, low));
        EXPECT_EQ(kindName(d.copKind), std::string("UNKNOWN"))
            << "low5=" << low << " принадлежит cache, а не cfc0";
    }
    const Decoded cf = ppsspp::core::Decode(0, mkCop0(0, 8, 0, Cop0Cf));
    const Decoded ct = ppsspp::core::Decode(0, mkCop0(0, 8, 0, Cop0Ct));
    EXPECT_EQ(kindName(cf.copKind), std::string("CFC0"));
    EXPECT_EQ(kindName(ct.copKind), std::string("CTC0"));
}

// Оба варианта записи mtc0 распознаются: rs5=4 (PPSPP) и rs5=0,
// младшие биты = 4 (MIPS32).
TEST(Cop0AuditTest, BothMtc0EncodingsAreAccepted) {
    const Decoded a = ppsspp::core::Decode(0, mkCop0(4, 8, 12, 0));
    const Decoded b = ppsspp::core::Decode(0, mkCop0(4, 8, 12, 4));
    const Decoded c = ppsspp::core::Decode(0, mkCop0(0, 8, 12, 4));
    EXPECT_EQ(kindName(a.copKind), std::string("MTC0"));
    EXPECT_EQ(kindName(b.copKind), std::string("MTC0"));
    EXPECT_EQ(kindName(c.copKind), std::string("MTC0"));
}

// --- CO = 1 (бит 25): это не регистры CPU ---
TEST(Cop0AuditTest, CoprocessorOneIsNotCop0RegisterAccess) {
    for (uint32_t rs5 = 16; rs5 < 32; ++rs5) {
        const Decoded d = ppsspp::core::Decode(0, mkCop0(rs5, 8, 12, 0));
        EXPECT_EQ(kindName(d.copKind), std::string("UNKNOWN"))
            << "rs5=" << rs5 << " (бит 25 = 1) — eret/wait/tlb*, "
                             "а не доступ к регистрам CPU";
    }
}

// --- прочие rs5, не описанные ни одной схемой ---
TEST(Cop0AuditTest, UnassignedRsValuesAreNotRecognised) {
    for (uint32_t rs5 = 1; rs5 < 16; ++rs5) {
        if (rs5 == 4) continue;                            // mtc0 (PPSSPP)
        if (rs5 == 10 || rs5 == 11 || rs5 == 14) continue; // rdpgpr/mfmc0/wrpgpr
        const Decoded d = ppsspp::core::Decode(0, mkCop0(rs5, 8, 12, 0));
        EXPECT_EQ(kindName(d.copKind), std::string("UNKNOWN"))
            << "rs5=" << rs5 << " не должен распознаваться";
    }
}

// --- сквозная проверка через интерпретатор ---
TEST_F(Cop0Test, Mtc0ViaRsFourWritesTheRegister) {
    // rs5 = 4, младшие биты = 0. До исправления распознавалось как mfc0
    // и НЕ записывало Status.
    cpu.cp0_Status = 0;
    cpu.gpr[8] = 0x0000FF01u;

    const uint64_t before = interp.unimplementedCount();
    const StepResult res = interp.StepInstruction(0x08800000u, mkCop0(4, 8, 12, 0));

    EXPECT_NE(res, StepResult::Unimplemented)
        << "mtc0 с rs5=4 обязан обрабатываться, а не отбрасываться";
    EXPECT_EQ(interp.unimplementedCount(), before);
    EXPECT_EQ(cpu.cp0_Status, 0x0000FF01u)
        << "mtc0 должен ЗАПИСАТЬ Status; если сработал mfc0, значение "
           "останется прежним";
}

TEST_F(Cop0Test, Mfc0AndMtc0AreNotConfused) {
    cpu.cp0_EPC = 0;

    // 1) mtc0 rs5=4 -> запись EPC
    cpu.gpr[8] = 0xAABBCCDDu;
    interp.StepInstruction(0x08800000u, mkCop0(4, 8, 14, 0));
    EXPECT_EQ(cpu.cp0_EPC, 0xAABBCCDDu) << "mtc0 (rs5=4) должен записать EPC";

    // 2) mfc0 rs5=0 -> чтение EPC
    interp.StepInstruction(0x08800004u, mkCop0(0, 9, 14, 0));
    EXPECT_EQ(cpu.gpr[9], 0xAABBCCDDu) << "mfc0 (rs5=0) должен прочитать EPC";
}

TEST_F(Cop0Test, ReservedBitInstructionIsRejected) {
    cpu.gpr[8] = 0xFFFFFFFFu;
    const uint64_t before = interp.unimplementedCount();

    // mfc0 с установленным зарезервированным битом 3
    interp.StepInstruction(0x08800000u, mkCop0(0, 8, 12, 0) | 0x0008u);

    EXPECT_EQ(interp.unimplementedCount(), before + 1)
        << "инструкция с установленными зарезервированными битами "
           "не должна выполняться как mfc0";
}

} // namespace