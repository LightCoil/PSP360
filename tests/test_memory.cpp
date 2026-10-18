// tests/test_memory.cpp — тесты подсистемы памяти
#include <gtest/gtest.h>

#include "core/memory.h"

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

TEST_F(MemoryTest, WriteRead32BigEndian) {
    const uint32_t addr = kRamBase + 0x1000;
    mem.Write32(addr, 0xDEADBEEF);
    EXPECT_EQ(mem.Read32(addr), 0xDEADBEEFu);

    // Отдельная проверка порядка байтов
    const uint8_t* p = mem.GetPointer(addr);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p[0], 0xDE);
    EXPECT_EQ(p[1], 0xAD);
    EXPECT_EQ(p[2], 0xBE);
    EXPECT_EQ(p[3], 0xEF);
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

} // namespace