// tests/test_memory.cpp — тесты подсистемы памяти
#include <gtest/gtest.h>

#include "core/cpu_state.h"
#include "core/memory.h"
#include "interp/interpreter.h"

using ppsspp::core::Memory;
using ppsspp::core::MemFault;

namespace {

constexpr uint32_t kRamBase     = 0x08800000u;
constexpr uint32_t kVramBase    = 0x04000000u;
constexpr uint32_t kScratchBase = 0x10000000u;

class MemoryTest : public ::testing::Test {
protected:
    Memory mem;

    void SetUp() override { mem.Init(); }
    void TearDown() override { mem.Shutdown(); }
};

TEST_F(MemoryTest, IsInitialized) {
    EXPECT_TRUE(mem.IsInitialized());
}

TEST_F(MemoryTest, WriteRead32LittleEndian) {
    const uint32_t addr = kRamBase + 0x1000;
    mem.Write32(addr, 0xDEADBEEF);
    EXPECT_EQ(mem.Read32(addr), 0xDEADBEEFu);

    // Allegrex работает в little-endian: младший байт — первым.
    const uint8_t* p = mem.GetPointer(addr);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p[0], 0xEF);
    EXPECT_EQ(p[1], 0xBE);
    EXPECT_EQ(p[2], 0xAD);
    EXPECT_EQ(p[3], 0xDE);
}

TEST_F(MemoryTest, MipsInstructionLayoutInMemory) {
    // 0x2402002A = addiu $v0, $zero, 42.
    // MIPSELF на диске: младший байт первым -> 2A 00 02 24.
    const uint32_t addr = kRamBase + 0x2000;
    mem.Write32(addr, 0x2402002Au);

    const uint8_t* p = mem.GetPointer(addr);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p[0], 0x2A);
    EXPECT_EQ(p[1], 0x00);
    EXPECT_EQ(p[2], 0x02);
    EXPECT_EQ(p[3], 0x24);
    EXPECT_EQ(mem.Read32(addr), 0x2402002Au);
}

TEST_F(MemoryTest, WriteRead16And8) {
    const uint32_t addr = kRamBase + 0x2000;
    mem.Write16(addr, 0xBEEF);
    EXPECT_EQ(mem.Read16(addr), 0xBEEF);

    mem.Write8(addr + 2, 0x42);
    EXPECT_EQ(mem.Read8(addr + 2), 0x42);
}

TEST_F(MemoryTest, RegionsAreIsolated) {
    mem.Write8(kVramBase + 0x10, 0x11);
    mem.Write8(kRamBase + 0x10, 0x22);
    mem.Write8(kScratchBase + 0x10, 0x33);

    EXPECT_EQ(mem.Read8(kVramBase + 0x10), 0x11);
    EXPECT_EQ(mem.Read8(kRamBase + 0x10), 0x22);
    EXPECT_EQ(mem.Read8(kScratchBase + 0x10), 0x33);
}

TEST_F(MemoryTest, UnmappedAddressIsRejected) {
    // Адрес вне отображённых областей
    const uint32_t bad = 0x00000000u;
    mem.Write32(bad, 0xFFFFFFFF);
    EXPECT_EQ(mem.LastFault(), MemFault::Unmapped);
    EXPECT_EQ(mem.LastFaultAddress(), bad);

    // Запись не должна была ничего изменить
    EXPECT_EQ(mem.Read32(kRamBase), 0u);
}

TEST_F(MemoryTest, AccessCrossingRegionBoundaryIsRejected) {
    // Адрес в самом конце RAM, попытка прочитать 4 байта выходит за границу
    const uint32_t ramEnd = kRamBase + 32u * 1024u * 1024u - 2u;
    mem.Read32(ramEnd);
    EXPECT_EQ(mem.LastFault(), MemFault::Unmapped);
}

TEST_F(MemoryTest, BlockTransferClampsToRegion) {
    uint8_t src[64];
    for (int i = 0; i < 64; ++i) src[i] = static_cast<uint8_t>(i);

    // Запись, выходящая за конец VRAM, обрезается
    const uint32_t vramEnd = kVramBase + 2u * 1024u * 1024u - 16u;
    mem.WriteBlock(vramEnd, src, sizeof(src));

    EXPECT_EQ(mem.Read8(vramEnd), 0x00);
    EXPECT_EQ(mem.Read8(vramEnd + 15), 0x0F);   // 15 байт поместилось
    EXPECT_EQ(mem.Read8(vramEnd + 16), 0x00);   // остальное не записано
}

TEST_F(MemoryTest, ReinitClearsData) {
    mem.Write32(kRamBase, 0x12345678);
    EXPECT_EQ(mem.Read32(kRamBase), 0x12345678u);

    mem.Init();
    EXPECT_EQ(mem.Read32(kRamBase), 0u);
}

TEST_F(MemoryTest, OperationCounters) {
    const uint64_t r0 = mem.readOps();
    const uint64_t w0 = mem.writeOps();

    mem.Write32(kRamBase + 0x40, 1);
    mem.Read32(kRamBase + 0x40);

    EXPECT_EQ(mem.writeOps(), w0 + 1);
    EXPECT_EQ(mem.readOps(), r0 + 1);
}

// РЕГРЕССИЯ: LastFault() не сбрасывался при успешном доступе, поэтому
// устаревшая ошибка от неотображённого адреса вызывала ложные BusError.
TEST_F(MemoryTest, LastFaultReflectsMostRecentAccess) {
    EXPECT_EQ(mem.LastFault(), MemFault::None);

    mem.Write32(0x00000000u, 1u);
    EXPECT_EQ(mem.LastFault(), MemFault::Unmapped);

    mem.Write32(kRamBase, 1u);
    EXPECT_EQ(mem.LastFault(), MemFault::None)
        << "успешный доступ обязан сбрасывать LastFault";

    mem.Read32(0x00000000u);
    EXPECT_EQ(mem.LastFault(), MemFault::Unmapped);

    mem.Read32(kRamBase);
    EXPECT_EQ(mem.LastFault(), MemFault::None);
}

// =========================================================================
// Невыравненный доступ: LWL / LWR / SWL / SWR
//
// Семантика little-endian (Allegrex, Config.LEL):
//   A = base+offset, W = A & ~3, n = A & 3, b[k] = байт по адресу W+k
//   LWL: rt[3-k] = b[k] для k = 0..n
//   LWR: rt[k-n] = b[k] для k = n..3
//   SWL: b[k]    = rt[3-k] для k = 0..n      (обратна LWL)
//   SWR: b[k]    = rt[k-n] для k = n..3      (обратна LWR)
//
// Проверяются: точные значения при всех смещениях, обратность пар,
// сохранение нетронутых байтов регистра и отсутствие записи вне диапазона.
// =========================================================================

class UnalignedTest : public ::testing::Test {
protected:
    Memory mem;
    static constexpr uint32_t kWord = kRamBase + 0x400;

    void SetUp() override {
        mem.Init();
        // Слово 0x11223344. В little-endian памяти это байты
        // b[0]=0x44, b[1]=0x33, b[2]=0x22, b[3]=0x11.
        mem.Write32(kWord, 0x11223344u);
    }
    void TearDown() override { mem.Shutdown(); }

    static uint32_t encMem(uint32_t op, uint32_t rt, uint32_t rs, int32_t off) {
        return (op << 26) | (rs << 21) | (rt << 16) |
               (static_cast<uint32_t>(off) & 0xFFFF);
    }
    static uint32_t byteAt(uint32_t v, uint32_t k) {
        return (v >> (8 * k)) & 0xFFu;
    }
};

// Эталонные значения посчитаны по правилам выше при rt = 0.
// Слово 0x11223344 в little-endian памяти: b[0]=0x44, b[1]=0x33,
// b[2]=0x22, b[3]=0x11.
TEST_F(UnalignedTest, LwlAtEveryOffset) {
    const uint32_t expect[4] = { 0x44000000u, 0x44330000u,
                                 0x44332200u, 0x44332211u };
    for (uint32_t off = 0; off < 4; ++off) {
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        cpu.gpr[8] = kWord;
        cpu.gpr[4] = 0;
        interp.StepInstruction(0, encMem(0x22, 4, 8, static_cast<int32_t>(off)));
        EXPECT_EQ(cpu.gpr[4], expect[off]) << "lwl offset " << off;
    }
}

TEST_F(UnalignedTest, LwrAtEveryOffset) {
    const uint32_t expect[4] = { 0x11223344u, 0x00112233u,
                                 0x00001122u, 0x00000011u };
    for (uint32_t off = 0; off < 4; ++off) {
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        cpu.gpr[8] = kWord;
        cpu.gpr[4] = 0;
        interp.StepInstruction(0, encMem(0x26, 4, 8, static_cast<int32_t>(off)));
        EXPECT_EQ(cpu.gpr[4], expect[off]) << "lwr offset " << off;
    }
}

// LWL пишет rt[3-k] для k = 0..n, поэтому сохраняются байты 0..(2-n).
TEST_F(UnalignedTest, LwlPreservesLowBytes) {
    const uint32_t seed = 0xDEADBEEFu;
    for (uint32_t off = 0; off < 4; ++off) {
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        cpu.gpr[8] = kWord;
        cpu.gpr[4] = seed;
        interp.StepInstruction(0, encMem(0x22, 4, 8, static_cast<int32_t>(off)));
        const uint32_t got = cpu.gpr[4];
        for (uint32_t k = 0; k + off < 3; ++k) {
            EXPECT_EQ(byteAt(got, k), byteAt(seed, k))
                << "lwl offset " << off << " must preserve byte " << k;
        }
    }
}

// LWR пишет rt[k-n] для k = n..3, поэтому сохраняются байты (4-n)..3.
TEST_F(UnalignedTest, LwrPreservesHighBytes) {
    const uint32_t seed = 0xDEADBEEFu;
    for (uint32_t off = 0; off < 4; ++off) {
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        cpu.gpr[8] = kWord;
        cpu.gpr[4] = seed;
        interp.StepInstruction(0, encMem(0x26, 4, 8, static_cast<int32_t>(off)));
        const uint32_t got = cpu.gpr[4];
        for (uint32_t k = 4 - off; k < 4; ++k) {
            EXPECT_EQ(byteAt(got, k), byteAt(seed, k))
                << "lwr offset " << off << " must preserve byte " << k;
        }
    }
}

// SWR — обратная операция к LWR при любом смещении.
TEST_F(UnalignedTest, SwrLwrAreInverse) {
    const uint32_t seed = 0xA5C3F709u;
    for (uint32_t off = 0; off < 4; ++off) {
        Memory local; local.Init();
        for (int i = 0; i < 32; ++i) local.Write8(kWord + i, 0x00);

        ppsspp::core::CPUState cs, cl;
        ppsspp::interp::Interpreter is(cs, local);
        ppsspp::interp::Interpreter il(cl, local);
        cs.gpr[8] = kWord; cs.gpr[4] = seed;
        cl.gpr[8] = kWord; cl.gpr[4] = 0;

        is.StepInstruction(0, encMem(0x2E, 4, 8, static_cast<int32_t>(off)));
        il.StepInstruction(4, encMem(0x26, 4, 8, static_cast<int32_t>(off)));

        for (uint32_t k = off; k < 4; ++k) {
            EXPECT_EQ(byteAt(cl.gpr[4], k - off), byteAt(seed, k - off))
                << "swr/lwr inverse offset " << off << " byte " << k;
        }
        local.Shutdown();
    }
}

// SWL — обратная операция к LWL при любом смещении.
TEST_F(UnalignedTest, SwlLwlAreInverse) {
    const uint32_t seed = 0xA5C3F709u;
    for (uint32_t off = 0; off < 4; ++off) {
        Memory local; local.Init();
        for (int i = 0; i < 32; ++i) local.Write8(kWord + i, 0x00);

        ppsspp::core::CPUState cs, cl;
        ppsspp::interp::Interpreter is(cs, local);
        ppsspp::interp::Interpreter il(cl, local);
        cs.gpr[8] = kWord; cs.gpr[4] = seed;
        cl.gpr[8] = kWord; cl.gpr[4] = 0;

        is.StepInstruction(0, encMem(0x2A, 4, 8, static_cast<int32_t>(off)));
        il.StepInstruction(4, encMem(0x22, 4, 8, static_cast<int32_t>(off)));

        for (uint32_t k = 0; k <= off; ++k) {
            EXPECT_EQ(byteAt(cl.gpr[4], 3 - k), byteAt(seed, 3 - k))
                << "swl/lwl inverse offset " << off << " byte " << k;
        }
        local.Shutdown();
    }
}

// На выровненном адресе SWR записывает всё слово целиком, поэтому обычный
// LW обязан вернуть исходное значение. Это и есть практическое применение
// невыравненного доступа на little-endian.
TEST_F(UnalignedTest, AlignedSwrStoresWholeWord) {
    const uint32_t seed = 0xAABBCCDDu;
    ppsspp::core::CPUState cpu;
    ppsspp::interp::Interpreter interp(cpu, mem);
    cpu.gpr[8] = kWord;
    cpu.gpr[4] = seed;

    interp.StepInstruction(0, encMem(0x2E, 4, 8, 0));
    EXPECT_EQ(mem.Read32(kWord), seed);

    cpu.gpr[4] = 0;
    interp.StepInstruction(4, encMem(0x26, 4, 8, 0));
    EXPECT_EQ(cpu.gpr[4], seed);
}

// Объединение парных инструкций: SWR+LWR и SWL+LWL — это взаимно
// обратные операции, которыми сохраняют и восстанавливают регистр
// при доступе к невыравненным данным. Проверяем полное восстановление
// 32-битного значения при любом смещении.
TEST_F(UnalignedTest, PairedInstructionsRestoreWholeRegister) {
    const uint32_t seed = 0x89ABCDEFu;
    for (uint32_t off = 0; off < 4; ++off) {
        Memory local; local.Init();
        for (int i = 0; i < 32; ++i) local.Write8(kWord + i, 0x00);

        ppsspp::core::CPUState cs, cl;
        ppsspp::interp::Interpreter is(cs, local);
        ppsspp::interp::Interpreter il(cl, local);
        cs.gpr[8] = kWord;
        cs.gpr[4] = seed;
        cl.gpr[8] = kWord;
        cl.gpr[4] = 0;

        // Сохраняем swr, затем восстанавливаем lwr в регистр с нулями.
        is.StepInstruction(0, encMem(0x2E, 4, 8, static_cast<int32_t>(off)));
        il.StepInstruction(4, encMem(0x26, 4, 8, static_cast<int32_t>(off)));

        // swr кладёт младшие (4-off) байтов rt в память, lwr кладёт их
        // обратно в младшие байты нового регистра. Старшие байты, которых
        // swr не касался, остаются нулевыми.
        const uint32_t restored = cl.gpr[4];
        for (uint32_t k = 0; k < 4; ++k) {
            const uint32_t want = (k < 4 - off) ? byteAt(seed, k) : 0u;
            EXPECT_EQ(byteAt(restored, k), want)
                << "swr/lwr offset " << off << " byte " << k;
        }

        // И обратная пара для старших байтов.
        for (int i = 0; i < 32; ++i) local.Write8(kWord + i, 0x00);
        cl.gpr[4] = 0;
        cs.gpr[4] = seed;
        is.StepInstruction(8, encMem(0x2A, 4, 8, static_cast<int32_t>(off)));
        il.StepInstruction(12, encMem(0x22, 4, 8, static_cast<int32_t>(off)));

        for (uint32_t j = 0; j <= off; ++j) {
            EXPECT_EQ(byteAt(cl.gpr[4], 3 - j), byteAt(seed, 3 - j))
                << "swl/lwl offset " << off << " restored byte " << (3 - j);
        }
        for (uint32_t k = 0; k + off < 3; ++k) {
            EXPECT_EQ(byteAt(cl.gpr[4], k), 0u)
                << "swl/lwl offset " << off << " must not set byte " << k;
        }
        local.Shutdown();
    }
}

} // namespace
