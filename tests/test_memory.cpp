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

// =========================================================================
// Невыравненный доступ: LWL / LWR / SWL / SWR
//
// Эталон — QEMU, target/mips/tcg/translate.c, функции gen_lxl (LWL)
// и gen_lxr (LWR). При little-endian:
//   gen_lxl: t1 = (addr & 3) ^ 3;  shift = t1 * 8;
//            rt = (memword << shift) | (rt & ~(~0u << shift))
//   gen_lxr: t1 = addr & 3;         shift = t1 * 8;
//            rt = (memword >> shift) | (rt & mask_high)
//
// Обозначения: W = A & ~3 (выровненное слово), n = A & 3,
// b[k] — k-й байт слова по адресу W+k (little-endian: b[0] = младший).
//
//   LWL:  rt[3 - n + p] = b[p]      для p = 0..n      (остальное сохр.)
//   LWR:  rt[p]         = b[n + p]  для p = 0..3-n    (остальное сохр.)
//   SWL:  b[p]          = rt[3-n+p] для p = 0..n      (остальное сохр.)
//   SWR:  b[n + p]      = rt[p]     для p = 0..3-n    (остальное сохр.)
//
// Проверяется таблица значений, побайтовое соответствие регистру и
// сохранение нетронутых байтов. Round-trip как критерий НЕ используется.
// =========================================================================

namespace {
// Слово в памяти 0x11223344 -> b[0]=0x44, b[1]=0x33, b[2]=0x22, b[3]=0x11.
constexpr uint32_t kWordValue = 0x11223344u;
// Начальное значение регистра-источника:
// r[0]=0xDD, r[1]=0xCC, r[2]=0xBB, r[3]=0xAA.
constexpr uint32_t kSeed = 0xAABBCCDDu;

inline uint32_t bsel(uint32_t v, int k) { return (v >> (8 * k)) & 0xFFu; }
} // namespace

class UnalignedTest : public ::testing::Test {
protected:
    Memory mem;
    static constexpr uint32_t kWord = kRamBase + 0x400;

    void SetUp() override { mem.Init(); }
    void TearDown() override { mem.Shutdown(); }

    void resetWord() { mem.Write32(kWord, kWordValue); }

    static uint32_t encMem(uint32_t op, uint32_t rt, uint32_t rs, int32_t off) {
        return (op << 26) | (rs << 21) | (rt << 16) |
               (static_cast<uint32_t>(off) & 0xFFFF);
    }

    // Выполняет одну инструкцию: rt=4 (источник), rs=8 (база = kWord).
    ppsspp::interp::StepResult execAt(uint32_t op, uint32_t off,
                                      ppsspp::core::CPUState& cpu,
                                      ppsspp::interp::Interpreter& interp) {
        cpu.gpr[8] = kWord;
        cpu.gpr[4] = kSeed;
        return interp.StepInstruction(0, encMem(op, 4, 8, off));
    }
};

// ---------------- LWL: rt[3-n+p] = b[p], p = 0..n ----------------
// n=0: r3=b0             -> 44 BB CC DD
// n=1: r3=b1, r2=b0      -> 33 44 CC DD
// n=2: r3=b2,r2=b1,r1=b0 -> 22 33 44 DD
// n=3: всё слово         -> 11 22 33 44
TEST_F(UnalignedTest, LwlTableFromQemuReference) {
    const uint32_t expect[4] = { 0x44BBCCDDu, 0x3344CCDDu,
                                 0x223344DDu, 0x11223344u };
    for (uint32_t n = 0; n < 4; ++n) {
        resetWord();
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        execAt(0x22u, static_cast<int32_t>(n), cpu, interp);
        EXPECT_EQ(cpu.gpr[4], expect[n]) << "lwl n=" << n;
    }
}

TEST_F(UnalignedTest, LwlPreservesUntouchedBytes) {
    for (uint32_t n = 0; n < 4; ++n) {
        resetWord();
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        execAt(0x22u, static_cast<int32_t>(n), cpu, interp);
        const int first = 3 - static_cast<int>(n);
        for (int j = 0; j < first; ++j) {
            EXPECT_EQ(bsel(cpu.gpr[4], j), bsel(kSeed, j))
                << "lwl n=" << n << " изменил байт " << j;
        }
    }
}

// ---------------- LWR: rt[p] = b[n+p], p = 0..3-n ----------------
// n=0: всё слово          -> 11 22 33 44
// n=1: r0=b1,r1=b2,r2=b3 -> AA 11 22 33
// n=2: r0=b2,r1=b3       -> AA BB 11 22
// n=3: r0=b3             -> AA BB CC 11
TEST_F(UnalignedTest, LwrTableFromQemuReference) {
    const uint32_t expect[4] = { 0x11223344u, 0xAA112233u,
                                 0xAABB1122u, 0xAABBCC11u };
    for (uint32_t n = 0; n < 4; ++n) {
        resetWord();
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        execAt(0x26u, static_cast<int32_t>(n), cpu, interp);
        EXPECT_EQ(cpu.gpr[4], expect[n]) << "lwr n=" << n;
    }
}

TEST_F(UnalignedTest, LwrPreservesUntouchedBytes) {
    for (uint32_t n = 0; n < 4; ++n) {
        resetWord();
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        execAt(0x26u, static_cast<int32_t>(n), cpu, interp);
        for (uint32_t j = 4 - n; j < 4; ++j) {
            EXPECT_EQ(bsel(cpu.gpr[4], static_cast<int>(j)),
                      bsel(kSeed, static_cast<int>(j)))
                << "lwr n=" << n << " изменил байт " << j;
        }
    }
}

// ---------------- SWL: b[p] = rt[3-n+p], p = 0..n ----------------
// память до операции 44 33 22 11, rt = AA BB CC DD
// n=0: b0=r3            -> AA 33 22 11
// n=1: b0=r2,b1=r3      -> BB AA 22 11
// n=2: b0=r1,b1=r2,b2=r3-> CC BB AA 11
// n=3: всё слово rt     -> DD CC BB AA
TEST_F(UnalignedTest, SwlTableFromQemuReference) {
    const uint32_t expect[4] = { 0x112233AAu, 0x1122AABBu,
                                 0x11AABBCCu, 0xAABBCCDDu };
    for (uint32_t n = 0; n < 4; ++n) {
        resetWord();
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        execAt(0x2Au, static_cast<int32_t>(n), cpu, interp);
        uint32_t got = 0;
        for (int k = 0; k < 4; ++k)
            got |= static_cast<uint32_t>(mem.Read8(kWord + k)) << (8 * k);
        EXPECT_EQ(got, expect[n]) << "swl n=" << n;
    }
}

TEST_F(UnalignedTest, SwlEachByteMatchesRegister) {
    for (uint32_t n = 0; n < 4; ++n) {
        resetWord();
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        execAt(0x2Au, static_cast<int32_t>(n), cpu, interp);
        for (uint32_t p = 0; p <= n; ++p) {
            const uint32_t want = bsel(kSeed, static_cast<int>(3 - n + p));
            EXPECT_EQ(mem.Read8(kWord + p), static_cast<uint8_t>(want))
                << "swl n=" << n << ": байт " << p
                << " ожидался из rt[" << (3 - n + p) << "]";
        }
    }
}

TEST_F(UnalignedTest, SwlLeavesOtherBytesUntouched) {
    for (uint32_t n = 0; n < 4; ++n) {
        resetWord();
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        execAt(0x2Au, static_cast<int32_t>(n), cpu, interp);
        for (uint32_t k = n + 1; k < 4; ++k) {
            EXPECT_EQ(mem.Read8(kWord + k),
                      static_cast<uint8_t>(bsel(kWordValue, static_cast<int>(k))))
                << "swl n=" << n << " изменил байт " << k
                << " за пределами диапазона";
        }
    }
}

// ---------------- SWR: b[n+p] = rt[p], p = 0..3-n ----------------
// n=0: всё слово rt     -> DD CC BB AA
// n=1: b1=r0,b2=r1,b3=r2-> 44 DD CC BB
// n=2: b2=r0,b3=r1      -> 44 33 DD CC
// n=3: b3=r0            -> 44 33 22 DD
TEST_F(UnalignedTest, SwrTableFromQemuReference) {
    const uint32_t expect[4] = { 0xAABBCCDDu, 0xBBCCDD44u,
                                 0xCCDD3344u, 0xDD223344u };
    for (uint32_t n = 0; n < 4; ++n) {
        resetWord();
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        execAt(0x2Eu, static_cast<int32_t>(n), cpu, interp);
        uint32_t got = 0;
        for (int k = 0; k < 4; ++k)
            got |= static_cast<uint32_t>(mem.Read8(kWord + k)) << (8 * k);
        EXPECT_EQ(got, expect[n]) << "swr n=" << n;
    }
}

TEST_F(UnalignedTest, SwrEachByteMatchesRegister) {
    for (uint32_t n = 0; n < 4; ++n) {
        resetWord();
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        execAt(0x2Eu, static_cast<int32_t>(n), cpu, interp);
        for (uint32_t p = 0; p <= 3 - n; ++p) {
            const uint32_t want = bsel(kSeed, static_cast<int>(p));
            EXPECT_EQ(mem.Read8(kWord + n + p), static_cast<uint8_t>(want))
                << "swr n=" << n << ": байт " << (n + p)
                << " ожидался из rt[" << p << "]";
        }
    }
}

TEST_F(UnalignedTest, SwrLeavesOtherBytesUntouched) {
    for (uint32_t n = 0; n < 4; ++n) {
        resetWord();
        ppsspp::core::CPUState cpu;
        ppsspp::interp::Interpreter interp(cpu, mem);
        execAt(0x2Eu, static_cast<int32_t>(n), cpu, interp);
        for (uint32_t k = 0; k < n; ++k) {
            EXPECT_EQ(mem.Read8(kWord + k),
                      static_cast<uint8_t>(bsel(kWordValue, static_cast<int>(k))))
                << "swr n=" << n << " изменил байт " << k << " перед диапазоном";
        }
    }
}

// На выровненном адресе SWR записывает всё слово целиком.
// SWL — нет: по gprMask из QEMU (0xFFFFFFFF >> (24 - 8n)) при n = 0 он
// пишет только старший байт rt. Выровненная 32-битная запись на
// little-endian выполняется одной инструкцией SWR.
TEST_F(UnalignedTest, AlignedSwrWritesWholeWord) {
    resetWord();
    ppsspp::core::CPUState cpu;
    ppsspp::interp::Interpreter interp(cpu, mem);
    execAt(0x2Eu, 0, cpu, interp);
    EXPECT_EQ(mem.Read32(kWord), kSeed)
        << "выровненный SWR должен записать слово целиком";
}

TEST_F(UnalignedTest, AlignedSwlWritesOnlyTopByte) {
    resetWord();
    ppsspp::core::CPUState cpu;
    ppsspp::interp::Interpreter interp(cpu, mem);
    execAt(0x2Au, 0, cpu, interp);
    uint32_t got = 0;
    for (int k = 0; k < 4; ++k)
        got |= static_cast<uint32_t>(mem.Read8(kWord + k)) << (8 * k);
    EXPECT_EQ(got, 0x112233AAu)
        << "выровненный SWL пишет только rt[3] в первый байт слова";
}

// SWL/SWR не должны выходить за пределы выровненного слова.
TEST_F(UnalignedTest, StoresStayInsideTheAlignedWord) {
    mem.Write8(kWord - 1, 0x5A);
    mem.Write8(kWord + 4, 0x5A);
    const uint32_t ops[2] = { 0x2Au, 0x2Eu };
    for (uint32_t op : ops) {
        for (uint32_t n = 0; n < 4; ++n) {
            resetWord();
            ppsspp::core::CPUState cpu;
            ppsspp::interp::Interpreter interp(cpu, mem);
            execAt(op, static_cast<int32_t>(n), cpu, interp);
            EXPECT_EQ(mem.Read8(kWord - 1), 0x5A)
                << "запись до начала слова (opcode 0x" << std::hex << op
                << ", n=" << std::dec << n << ")";
            EXPECT_EQ(mem.Read8(kWord + 4), 0x5A)
                << "запись за концом слова (opcode 0x" << std::hex << op
                << ", n=" << std::dec << n << ")";
        }
    }
}

} // namespace

