// tests/test_loader.cpp — тесты загрузчика PBP/ELF
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "Loader/loader.h"
#include "Loader/memory_sink.h"
#include "core/cpu_state.h"
#include "core/memory.h"
#include "interp/interpreter.h"

using ppsspp::core::Memory;
using namespace ppsspp::loader;

namespace {

void putLE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 24));
}
void putLE16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void putBytes(std::vector<uint8_t>& v, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    v.insert(v.end(), b, b + n);
}

// ---- сборка минимального ELF ----
std::vector<uint8_t> MakeElf(uint32_t entry, uint32_t textAddr,
                            const std::vector<uint8_t>& code) {
    constexpr uint32_t kEhsize = 52;
    constexpr uint32_t kPhnum  = 2;   // text + bss
    const uint32_t phoff     = kEhsize;
    const uint32_t textOff   = phoff + kPhnum * 32;

    std::vector<uint8_t> e;
    e.push_back(0x7F); e.push_back('E'); e.push_back('L'); e.push_back('F');
    e.push_back(1);    // 32-bit
    e.push_back(1);    // little-endian
    e.push_back(1);    // EV_CURRENT
    for (int i = 0; i < 9; ++i) e.push_back(0);

    putLE16(e, 2);            // e_type = ET_EXEC
    putLE16(e, 8);            // e_machine = EM_MIPS
    putLE32(e, 1);            // e_version
    putLE32(e, entry);        // e_entry
    putLE32(e, phoff);        // e_phoff
    putLE32(e, 0);            // e_shoff
    putLE32(e, 0);            // e_flags
    putLE16(e, kEhsize);
    putLE16(e, 32);           // e_phentsize
    putLE16(e, static_cast<uint16_t>(kPhnum));
    putLE16(e, 0);            // e_shentsize
    putLE16(e, 0);            // e_shnum
    putLE16(e, 0);            // e_shstrndx

    // PT_LOAD: text
    putLE32(e, 1);            // p_type = PT_LOAD
    putLE32(e, textOff);      // p_offset
    putLE32(e, textAddr);     // p_vaddr
    putLE32(e, textAddr);     // p_paddr
    putLE32(e, static_cast<uint32_t>(code.size()));
    putLE32(e, static_cast<uint32_t>(code.size()));
    putLE32(e, 0x5);          // r-x
    putLE32(e, 0x1000);       // p_align

    // PT_LOAD: bss
    putLE32(e, 1);
    putLE32(e, 0);
    putLE32(e, textAddr + 0x10000u);
    putLE32(e, textAddr + 0x10000u);
    putLE32(e, 0);            // filesz = 0 -> .bss
    putLE32(e, 0x1000);
    putLE32(e, 0x6);          // rw-
    putLE32(e, 0x1000);

    putBytes(e, code.data(), code.size());
    return e;
}

// ---- сборка минимального PBP вокруг ELF ----
// Формат v0x100: заголовок 0x2C байт (размер data.psar отсутствует).
// Формат v0x200: заголовок 0x30 байт + секция data.psar.
std::vector<uint8_t> MakePBP(const std::vector<uint8_t>& elf, uint32_t version) {
    const bool hasPsar = (version == 0x00020000u);
    const uint32_t headerSize = hasPsar ? 0x30u : 0x2Cu;

    // Раскладка секций в массиве offsets[8] заголовка PBP
    const uint32_t offsets[8] = {
        0,                  // 0: param.sfo
        0, 0, 0, 0,         // 1..4: иконки
        0,                  // 5: snd0.dat
        headerSize,         // 6: data.psp
        hasPsar ? (headerSize + static_cast<uint32_t>(elf.size())) : 0u,  // 7: data.psar
    };

    std::vector<uint8_t> p;
    putBytes(p, "\0PBP", 4);
    putLE32(p, version);
    for (int i = 0; i < 8; ++i) putLE32(p, offsets[i]);
    putLE32(p, static_cast<uint32_t>(elf.size()));  // data.psp size @ 0x28
    if (hasPsar) putLE32(p, 0);                    // data.psar size @ 0x2C

    // Проверка раскладки заголовка (ASSERT_* нельзя в функции,
    // возвращающей значение, поэтому используем явное сравнение).
    if (p.size() != headerSize) return std::vector<uint8_t>();

    putBytes(p, elf.data(), elf.size());

    if (hasPsar) {
        // Заглушка data.psar нужна, чтобы загрузчик увидел конец data.psp.
        for (int i = 0; i < 16; ++i) p.push_back(0xFF);
    }

    return p;
}

class LoaderTest : public ::testing::Test {
protected:
    std::vector<uint8_t> code;
    std::vector<uint8_t> elf;
    Memory mem;

    LoaderTest() {
        mem.Init();
        // addiu $2,$0,42 ; addiu $3,$0,24
        code.push_back(0x24); code.push_back(0x02); code.push_back(0x00); code.push_back(0x2A);
        code.push_back(0x24); code.push_back(0x03); code.push_back(0x00); code.push_back(0x18);
        elf = MakeElf(0x08804000u, 0x08804000u, code);
    }
    void TearDown() override { mem.Shutdown(); }
};

TEST_F(LoaderTest, ParseElf) {
    Image img;
    const LoadStatus st = LoadELF(elf.data(), elf.size(), img);

    ASSERT_EQ(st, LoadStatus::Ok) << StatusName(st);
    EXPECT_TRUE(img.IsValid());
    EXPECT_EQ(img.Entry(), 0x08804000u);
    EXPECT_EQ(img.segments.size(), 2u);
    EXPECT_EQ(img.segments[0].vaddr, 0x08804000u);
    EXPECT_EQ(img.segments[0].filesz, 8u);
    EXPECT_EQ(img.segments[1].filesz, 0u);   // .bss
}

TEST_F(LoaderTest, RejectsBadElf) {
    Image img;
    std::vector<uint8_t> bad = elf;
    bad[1] = 'X';
    EXPECT_EQ(LoadELF(bad.data(), bad.size(), img), LoadStatus::BadElfMagic);

    bad = elf; bad[4] = 2;   // 64-bit
    EXPECT_EQ(LoadELF(bad.data(), bad.size(), img), LoadStatus::BadElfClass);

    bad = elf; bad[5] = 2;   // big-endian
    EXPECT_EQ(LoadELF(bad.data(), bad.size(), img), LoadStatus::BadElfEndian);

    bad = elf; bad[18] = 0; bad[19] = 40;  // e_machine = EM_ARM
    EXPECT_EQ(LoadELF(bad.data(), bad.size(), img), LoadStatus::BadElfMachine);
}

TEST_F(LoaderTest, ParsePbpV100) {
    const std::vector<uint8_t> pbp = MakePBP(elf, 0x00010000u);

    Image img;
    const LoadStatus st = LoadPBPFromMemory(pbp.data(), pbp.size(), img);

    ASSERT_EQ(st, LoadStatus::Ok) << StatusName(st);
    EXPECT_EQ(img.Entry(), 0x08804000u);
    EXPECT_EQ(img.elf.size(), elf.size());
}

TEST_F(LoaderTest, ParsePbpV200) {
    const std::vector<uint8_t> pbp = MakePBP(elf, 0x00020000u);

    Image img;
    const LoadStatus st = LoadPBPFromMemory(pbp.data(), pbp.size(), img);

    ASSERT_EQ(st, LoadStatus::Ok) << StatusName(st);
    EXPECT_EQ(img.Entry(), 0x08804000u);
}

TEST_F(LoaderTest, RejectsBadPbpMagic) {
    std::vector<uint8_t> pbp = MakePBP(elf, 0x00010000u);
    pbp[1] = 'X';

    Image img;
    EXPECT_EQ(LoadPBPFromMemory(pbp.data(), pbp.size(), img), LoadStatus::BadPbpMagic);
}

TEST_F(LoaderTest, RejectsTruncatedPbp) {
    std::vector<uint8_t> pbp = MakePBP(elf, 0x00010000u);
    pbp.resize(10);   // короче заголовка

    Image img;
    EXPECT_EQ(LoadPBPFromMemory(pbp.data(), pbp.size(), img), LoadStatus::PbpTruncated);
}

TEST_F(LoaderTest, LoadIntoMemoryAndExecute) {
    const std::vector<uint8_t> pbp = MakePBP(elf, 0x00010000u);

    Image img;
    ASSERT_EQ(LoadPBPFromMemory(pbp.data(), pbp.size(), img), LoadStatus::Ok);

    MemorySink sink(mem);
    ASSERT_EQ(LoadIntoMemory(img, sink), LoadStatus::Ok);

    // Инструкции должны оказаться в памяти по адресу входа
    EXPECT_EQ(mem.Read32(0x08804000u), 0x2402002Au);
    EXPECT_EQ(mem.Read32(0x08804004u), 0x24030018u);

    // И выполниться интерпретатором
    ppsspp::core::CPUState cpu;
    ppsspp::interp::Interpreter interp(cpu, mem);
    cpu.Reset(0x08804000u);
    interp.Run(2);

    EXPECT_EQ(cpu.gpr[2], 42u);
    EXPECT_EQ(cpu.gpr[3], 24u);
}

} // namespace