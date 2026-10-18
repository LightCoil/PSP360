// tests/test_vfpu.cpp — тесты обработчика VFPU
//
// Кодировка инструкций VFPU Allegrex установлена по первоисточникам:
//   * PPSSPP, Core/MIPS/MIPSVFPUUtils.h:
//         #define _VD (op & 0x7F)        // приёмник,  биты 6..0
//         #define _VS ((op>>8) & 0x7F)   // источник 1, биты 14..8
//         #define _VT ((op>>16) & 0x7F)  // источник 2, биты 22..16
//   * PPSSPP, Core/MIPS/MIPSDisVFPU.cpp, VSuff()/GetVecSize():
//         размер вектора = ((op>>15)&1)<<1 | ((op>>7)&1)
//         0 = .s (1 lane), 1 = .p (2), 2 = .t (3), 3 = .q (4)
//   * PPSSPP, Core/MIPS/MIPSTables.cpp: таблицы tableVFPU0/1/3/4,
//     выбираемые по опкоду MIPS (биты 31..26) и под-индексу.
//   * PSP CPU Overview (Hitmen), раздел 4.9 — контрольные слова.
//
// Тесты передают настоящие машинные слова через core::Decode().
// Ручные присваивания полей Decoded запрещены: именно они скрывали
// ошибку декодирования (d.rt и d.vs читались из одного и того же поля).
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

// ---- кодировщик VFPU согласно установленной раскладке ----
enum : uint32_t {
    kVfpuVmov = 0x00,   // tableVFPU4[0],  opcode 0x34
    kVfpuVabs = 0x01,   // tableVFPU4[1]
    kVfpuVneg = 0x02,   // tableVFPU4[2]
    kVfpuVadd = 0x00,   // tableVFPU0[0],  opcode 0x18
    kVfpuVsub = 0x01,   // tableVFPU0[1]
    kVfpuVdiv = 0x07,   // tableVFPU0[7]
    kVfpuVmul = 0x00,   // tableVFPU1[0],  opcode 0x19
    kVfpuVmin = 0x02,   // tableVFPU3[2],  opcode 0x1B
    kVfpuVmax = 0x03,   // tableVFPU3[3]
};

// Размер вектора: .s = 0, .p = 1, .t = 2, .q = 3
inline uint32_t dtypeBits(uint32_t d) {
    return (d == 0) ? 0u : (d == 1 ? 1u : (d == 2 ? 2u : 3u));
}

// Трёхоперандная форма: vd[6:0], vs[14:8], vt[22:16], размер, опкод/под-индекс.
inline uint32_t encVfpu3(uint32_t opcode, uint32_t sub, uint32_t vd,
                         uint32_t vs, uint32_t vt, uint32_t dtype = 0) {
    const uint32_t d = dtypeBits(dtype);
    return (opcode << 26) | (vt << 16) | (vs << 8) | (vd & 0x7Fu) |
           ((d & 1u) << 7) | ((d >> 1) << 15) | ((sub & 0x7u) << 23);
}

// Двухоперандная форма (tableVFPU4): тот же vd/vs, поле vt используется
// как номер функции.
inline uint32_t encVfpu2(uint32_t func, uint32_t vd, uint32_t vs,
                         uint32_t dtype = 0) {
    const uint32_t d = dtypeBits(dtype);
    return (0x34u << 26) | ((func & 0x1Fu) << 16) | (vs << 8) | (vd & 0x7Fu) |
           ((d & 1u) << 7) | ((d >> 1) << 15);
}

class VfpuTest : public ::testing::Test {
protected:
    Memory    mem;
    CPUState  cpu;
    Interpreter interp;

    VfpuTest() : interp(cpu, mem) { mem.Init(); }

    // Выполняет инструкцию через настоящий путь Step() с записью в память,
    // чтобы декодер работал с реальным машинным словом.
    void execAt(uint32_t pc, uint32_t word) {
        mem.Write32(pc, word);
        interp.StepInstruction(pc, word);
    }
    void exec(uint32_t word) { execAt(0x08800000u, word); }
};

// =========================================================================
// РЕГРЕССИЯ: раскладка полей
//
// Раньде д.vt и d.vs вычислялись из одного поля (word>>16), из-за чего
// обработчик использовал один регистр как оба операнда: vadd считал
// x + x. Здесь проверяется, что Decode() различает все три поля.
// =========================================================================

TEST_F(VfpuTest, DecoderSeparatesVdVsAndVt) {
    const Decoded d = ppsspp::core::Decode(0, encVfpu3(0x18, kVfpuVadd, 3, 1, 2));

    EXPECT_EQ(d.vfpuVd, 3u) << "приёмник — биты 6..0";
    EXPECT_EQ(d.vfpuVs, 1u) << "источник 1 — биты 14..8";
    EXPECT_EQ(d.vfpuVt, 2u) << "источник 2 — биты 22..16";
    EXPECT_NE(d.vfpuVs, d.vfpuVt) << "источники не должны совпадать";
    EXPECT_TRUE(d.isVfpu);
    EXPECT_EQ(d.opcode, 0x18u);
    EXPECT_EQ(d.vfpuFunc, kVfpuVadd);
    EXPECT_EQ(d.vfpuSize, 0u) << "размер .s";
}

TEST_F(VfpuTest, DecodeMatchesDocumentedOpcodeWords) {
    // Контрольные слова из PSP CPU Overview (все поля регистров нулевые).
    EXPECT_EQ(encVfpu3(0x18, kVfpuVadd, 0, 0, 0), 0x60000000u);
    EXPECT_EQ(encVfpu3(0x18, kVfpuVsub, 0, 0, 0), 0x60800000u);
    EXPECT_EQ(encVfpu3(0x18, kVfpuVdiv, 0, 0, 0), 0x63800000u);
    EXPECT_EQ(encVfpu3(0x19, kVfpuVmul, 0, 0, 0), 0x64000000u);
    EXPECT_EQ(encVfpu3(0x1B, kVfpuVmin, 0, 0, 0), 0x6D000000u);
    EXPECT_EQ(encVfpu3(0x1B, kVfpuVmax, 0, 0, 0), 0x6D800000u);
    EXPECT_EQ(encVfpu2(kVfpuVmov, 0, 0), 0xD0000000u);
    EXPECT_EQ(encVfpu2(kVfpuVabs, 0, 0), 0xD0010000u);
    EXPECT_EQ(encVfpu2(kVfpuVneg, 0, 0), 0xD0020000u);
}

TEST_F(VfpuTest, DecodeExtractsSevenBitRegisters) {
    // Регистры 0..127: младшие 5 бит в основном поле + 2 бита в соседних.
    // Старая маска & 0x1F обрезала индекс и делала регистры 32..127
    // недостижимыми.
    const Decoded d = ppsspp::core::Decode(0, encVfpu3(0x18, kVfpuVadd, 100, 3, 127));
    EXPECT_EQ(d.vfpuVd, 100u);
    EXPECT_EQ(d.vfpuVs, 3u);
    EXPECT_EQ(d.vfpuVt, 127u);
}

TEST_F(VfpuTest, DecodeExtractsVectorSize) {
    // .s / .p / .t / .q различаются битами 7 и 15.
    const uint32_t words[4] = {
        encVfpu3(0x18, kVfpuVadd, 1, 2, 3, 0),   // .s
        encVfpu3(0x18, kVfpuVadd, 1, 2, 3, 1),   // .p
        encVfpu3(0x18, kVfpuVadd, 1, 2, 3, 2),   // .t
        encVfpu3(0x18, kVfpuVadd, 1, 2, 3, 3),   // .q
    };
    const char* names[4] = { ".s", ".p", ".t", ".q" };
    for (int i = 0; i < 4; ++i) {
        const Decoded d = ppsspp::core::Decode(0, words[i]);
        EXPECT_EQ(d.vfpuSize, static_cast<uint32_t>(i)) << "размер вектора " << names[i];
    }
}

// =========================================================================
// Операции: проверяются по РЕАЛЬНЫМ машинным словам
// =========================================================================

TEST_F(VfpuTest, Vmov) {
    cpu.vpr[5][0] = 3.1415f;
    exec(encVfpu2(kVfpuVmov, 6, 5));
    EXPECT_FLOAT_EQ(cpu.vpr[6][0], 3.1415f);
}

TEST_F(VfpuTest, Vabs) {
    cpu.vpr[2][0] = -7.5f;
    exec(encVfpu2(kVfpuVabs, 3, 2));
    EXPECT_FLOAT_EQ(cpu.vpr[3][0], 7.5f);
}

TEST_F(VfpuTest, Vneg) {
    cpu.vpr[2][0] = 7.5f;
    exec(encVfpu2(kVfpuVneg, 3, 2));
    EXPECT_FLOAT_EQ(cpu.vpr[3][0], -7.5f);
}

// РЕГРЕССИЯ: раньше vadd/vsub/... складывали регистр с самим собой.
TEST_F(VfpuTest, TwoOperandOpsUseDistinctSourceRegisters) {
    struct Case { uint32_t opcode, sub; float expect; const char* name; };
    const Case cases[] = {
        { 0x18, kVfpuVadd, 14.0f, "vadd" },
        { 0x18, kVfpuVsub,  6.0f, "vsub" },
        { 0x19, kVfpuVmul, 40.0f, "vmul" },
        { 0x18, kVfpuVdiv,  2.5f, "vdiv" },
        { 0x1B, kVfpuVmin,  4.0f, "vmin" },
        { 0x1B, kVfpuVmax, 10.0f, "vmax" },
    };

    for (const Case& c : cases) {
        cpu.vpr[1][0] = 10.0f;      // vs (биты 14..8)
        cpu.vpr[2][0] =  4.0f;      // vt (биты 22..16)
        cpu.vpr[3][0] =  0.0f;

        exec(encVfpu3(c.opcode, c.sub, 3, 1, 2));

        EXPECT_FLOAT_EQ(cpu.vpr[3][0], c.expect)
            << c.name << ": vs=10, vt=4 -> ожидалось " << c.expect
            << ", получено " << cpu.vpr[3][0];
    }
}

TEST_F(VfpuTest, TwoOperandOpsAreNotCommutative) {
    // Если обработчик путал источники, vsub/vdiv дали бы тот же результат,
    // что и при переставленных операндах.
    cpu.vpr[1][0] = 10.0f;
    cpu.vpr[2][0] =  4.0f;
    exec(encVfpu3(0x18, kVfpuVsub, 3, 1, 2));
    EXPECT_FLOAT_EQ(cpu.vpr[3][0], 6.0f);      // 10 - 4

    cpu.vpr[3][0] = 0.0f;
    exec(encVfpu3(0x18, kVfpuVsub, 3, 2, 1));
    EXPECT_FLOAT_EQ(cpu.vpr[3][0], -6.0f);     // 4 - 10
}

TEST_F(VfpuTest, DivisionByZeroProducesInfinity) {
    cpu.vpr[1][0] = 1.0f;
    cpu.vpr[2][0] = 0.0f;
    exec(encVfpu3(0x18, kVfpuVdiv, 3, 1, 2));
    EXPECT_TRUE(std::isinf(cpu.vpr[3][0]));
}

TEST_F(VfpuTest, SevenBitRegisterOperands) {
    // Регистры выше 63 — только корректная 7-битная раскладка позволяет
    // к ним обратиться (старая маска & 31 обрезала индекс).
    cpu.vpr[70][0] = 6.0f;
    cpu.vpr[100][0] = 2.0f;
    cpu.vpr[110][0] = 0.0f;

    exec(encVfpu3(0x18, kVfpuVadd, 110, 70, 100));
    EXPECT_FLOAT_EQ(cpu.vpr[110][0], 8.0f);

    cpu.vpr[110][0] = 0.0f;
    exec(encVfpu2(kVfpuVneg, 110, 100));
    EXPECT_FLOAT_EQ(cpu.vpr[110][0], -2.0f);
}

TEST_F(VfpuTest, UnknownOperationIsRejected) {
    // 0x18 с несуществующим под-индексом 5 -> не реализовано.
    const uint64_t before = VFPUHandler::unsupportedCount();
    const uint32_t word = encVfpu3(0x18, 5, 3, 1, 2);
    const Decoded d = ppsspp::core::Decode(0, word);
    EXPECT_FALSE(VFPUHandler::Execute(cpu, d));
    EXPECT_EQ(VFPUHandler::unsupportedCount(), before + 1);
}

TEST_F(VfpuTest, NonVfpuOpcodesAreNotAccepted) {
    // COP2 (0x12) в текущей модели не реализован.
    const Decoded d = ppsspp::core::Decode(0, 0x48000000u);
    EXPECT_FALSE(VFPUHandler::Execute(cpu, d));
}

TEST_F(VfpuTest, VectorRegisterFileIsFullySized) {
    // Регистры 0..127, каждый по 4 float — как на реальном Allegrex
    cpu.vpr[127][3] = 42.0f;
    EXPECT_FLOAT_EQ(cpu.vpr[127][3], 42.0f);

    cpu.Reset(0);
    EXPECT_FLOAT_EQ(cpu.vpr[127][3], 0.0f);
}

// =========================================================================
// Интеграция: инструкция должна доходить до обработчика через интерпретатор
// =========================================================================

TEST_F(VfpuTest, DispatcherRoutesVfpuOpcodesToHandler) {
    cpu.vpr[1][0] = 8.0f;
    cpu.vpr[2][0] = 3.0f;
    cpu.vpr[4][0] = 0.0f;

    // Выполняем через StepInstruction — тот же путь, что и у диспетчера.
    ppsspp::interp::StepResult res =
        interp.StepInstruction(0x08800000u, encVfpu3(0x18, kVfpuVadd, 4, 1, 2));

    EXPECT_NE(res, ppsspp::interp::StepResult::Unimplemented)
        << "инструкция VFPU не должна попадать в нереализованные";
    EXPECT_FLOAT_EQ(cpu.vpr[4][0], 11.0f);
}

} // namespace