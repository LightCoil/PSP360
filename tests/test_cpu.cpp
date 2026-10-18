// tests/test_cpu.cpp — тесты состояния процессора и прерываний
#include <gtest/gtest.h>

#include "core/cpu_state.h"

using ppsspp::core::CPUState;
using ppsspp::core::PC_HALTED;

namespace {

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
    EXPECT_FALSE(cpu.llBit);
    EXPECT_EQ(cpu.instructionsExecuted, 0u);
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
    cpu.StepOne(0x08801000u, 0x2402002A);
    EXPECT_EQ(cpu.scratchPC, 0x08801000u);
    EXPECT_EQ(cpu.scratchInst, 0x2402002Au);
}

TEST(CPUStateTest, InterruptRequiresEnableAndMask) {
    CPUState cpu;

    cpu.RaiseInterrupt(3);
    EXPECT_TRUE(cpu.interruptPending);

    // Прерывания замаскированы и IE=0
    EXPECT_FALSE(cpu.ShouldTakeInterrupt());

    cpu.cp0_IM = 0xFF;
    EXPECT_FALSE(cpu.ShouldTakeInterrupt());

    // Включаем прерывания
    cpu.cp0_Status |= 0x1;
    EXPECT_TRUE(cpu.ShouldTakeInterrupt());
}

TEST(CPUStateTest, UnmaskedInterruptIsNotTaken) {
    CPUState cpu;
    cpu.cp0_Status |= 0x1;
    cpu.RaiseInterrupt(2);
    cpu.cp0_IM = 0x02;     // маскируем линию 2, но не 1
    EXPECT_FALSE(cpu.ShouldTakeInterrupt());
}

TEST(CPUStateTest, ExceptionLevelBlocksInterrupts) {
    CPUState cpu;
    cpu.cp0_Status |= 0x1;
    cpu.cp0_IM = 0xFF;
    cpu.RaiseInterrupt(1);
    EXPECT_TRUE(cpu.ShouldTakeInterrupt());

    cpu.cp0_Status |= 0x2;   // EXL = 1
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
    cpu.cp0_Status |= 0x1;
    cpu.cp0_IM = 0xFF;
    cpu.RaiseInterrupt(0);

    const uint32_t vector = cpu.TakeInterruptVector();
    EXPECT_NE(vector, 0u);
    EXPECT_EQ(cpu.cp0_EPC, 0x08805000u);
    EXPECT_NE(cpu.cp0_Status & 0x2u, 0u);   // EXL установлен
    EXPECT_EQ(cpu.cp0_Status & 0x1u, 0u);   // IE сброшен

    // Повторный вызов ничего не делает, пока EXL не сброшен
    EXPECT_EQ(cpu.TakeInterruptVector(), 0u);
}

} // namespace