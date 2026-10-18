// tests/test_vfpu.cpp — тесты обработчика VFPU (из снапшота 10.06.2025)
#include <gtest/gtest.h>

#include <cmath>

#include "core/cpu_state.h"
#include "core/decoder.h"
#include "interp/interpreter.h"
#include "vfpu/vfpu_handler.h"

using ppsspp::core::CPUState;
using ppsspp::core::Memory;
using ppsspp::core::Decoded;
using ppsspp::interp::Interpreter;
using ppsspp::vfpu::VFPUHandler;

namespace {

// Кодирование инструкции VFPU: opcode 0x12,
// биты 10..6 = приёмник (vt), 20..16 = источник (vs), 5..0 = fmt.
static inline uint32_t encVfpu(uint32_t vt, uint32_t vs, uint32_t fmt) {
    return (0x12u << 26) | (vs << 16) | (vt << 6) | fmt;
}

class VfpuTest : public ::testing::Test {
protected:
    Memory    mem;
    CPUState  cpu;
    Interpreter interp;

    VfpuTest() : interp(cpu, mem) { mem.Init(); }

    void exec(uint32_t word) {
        interp.StepOne(0x08800000u, word);
    }
};

TEST_F(VfpuTest, MovS) {
    cpu.vpr[5][0] = 3.1415f;
    exec(encVfpu(6, 5, 0x00)); // vD=vS
    EXPECT_FLOAT_EQ(cpu.vpr[6][0], 3.1415f);
}

TEST_F(VfpuTest, AbsS) {
    cpu.vpr[2][0] = -7.5f;
    exec(encVfpu(3, 2, 0x02)); // vD=|vS|
    EXPECT_FLOAT_EQ(cpu.vpr[3][0], 7.5f);
}

TEST_F(VfpuTest, NegS) {
    cpu.vpr[2][0] = 7.5f;
    exec(encVfpu(3, 2, 0x01)); // vD=-vS
    EXPECT_FLOAT_EQ(cpu.vpr[3][0], -7.5f);
}

TEST_F(VfpuTest, TwoOperandArithmetic) {
    cpu.vpr[1][0] = 10.0f;
    cpu.vpr[2][0] = 4.0f;

    // Двухаргументные операции: vD = vS (биты 20..16) op vT (биты 10..6),
    // второй операнд передаётся в d.rt.
    struct Case { uint8_t fmt; float expect; const char* name; };
    const Case cases[] = {
        {0x03, 14.0f, "add"},
        {0x04,  6.0f, "sub"},
        {0x06, 40.0f, "mul"},
        {0x07,  2.5f, "div"},
        {0x08,  4.0f, "min"},
        {0x09, 10.0f, "max"},
    };

    for (const Case& c : cases) {
        cpu.vpr[1][0] = 10.0f;
        cpu.vpr[2][0] = 4.0f;

        Decoded d = ppsspp::core::Decode(0, encVfpu(3, 1, c.fmt));
        d.rt = 2;   // второй операнд

        ASSERT_TRUE(VFPUHandler::Execute(cpu, d)) << c.name;
        EXPECT_FLOAT_EQ(cpu.vpr[3][0], c.expect) << c.name;
    }
}

TEST_F(VfpuTest, DivisionByZeroProducesInfinity) {
    cpu.vpr[1][0] = 1.0f;
    cpu.vpr[2][0] = 0.0f;

    Decoded d = ppsspp::core::Decode(0, encVfpu(3, 1, 0x07)); // div
    d.rt = 2;
    ASSERT_TRUE(VFPUHandler::Execute(cpu, d));
    EXPECT_TRUE(std::isinf(cpu.vpr[3][0]));
}

TEST_F(VfpuTest, RegisterSourceIsCorrectlyDecoded) {
    cpu.vpr[5][0] = 3.1415f;

    // Проверяем, что декодер берёт источник из бит 20..16,
    // а приёмник — из бит 10..6 (как в реальном кодировании VFPU).
    Decoded d = ppsspp::core::Decode(0, encVfpu(6, 5, 0x00));
    EXPECT_EQ(d.vs, 5);
    EXPECT_EQ(d.vt, 6);
    EXPECT_EQ(d.fmt, 0x00);

    ASSERT_TRUE(VFPUHandler::Execute(cpu, d));
    EXPECT_FLOAT_EQ(cpu.vpr[6][0], 3.1415f);
}

TEST_F(VfpuTest, MinMax) {
    cpu.vpr[1][0] = 3.0f;
    cpu.vpr[2][0] = 9.0f;

    Decoded d = ppsspp::core::Decode(0, encVfpu(3, 1, 0x08)); // min
    d.rt = 2;
    EXPECT_TRUE(VFPUHandler::Execute(cpu, d));
    EXPECT_FLOAT_EQ(cpu.vpr[3][0], 3.0f);

    d = ppsspp::core::Decode(0, encVfpu(3, 1, 0x09)); // max
    d.rt = 2;
    EXPECT_TRUE(VFPUHandler::Execute(cpu, d));
    EXPECT_FLOAT_EQ(cpu.vpr[3][0], 9.0f);
}

TEST_F(VfpuTest, UnknownFormatIsRejected) {
    const uint64_t before = VFPUHandler::unsupportedCount();
    Decoded d = ppsspp::core::Decode(0, encVfpu(3, 1, 0x3F));
    EXPECT_FALSE(VFPUHandler::Execute(cpu, d));
    EXPECT_EQ(VFPUHandler::unsupportedCount(), before + 1);
}

TEST_F(VfpuTest, VectorRegisterFileIsFullySized) {
    // Регистры 0..127, каждый по 4 float — как на реальном Allegrex
    cpu.vpr[127][3] = 42.0f;
    EXPECT_FLOAT_EQ(cpu.vpr[127][3], 42.0f);

    cpu.Reset(0);
    EXPECT_FLOAT_EQ(cpu.vpr[127][3], 0.0f);
}

} // namespace