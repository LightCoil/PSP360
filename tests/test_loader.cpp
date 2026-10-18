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
// Перезаписывает 4 байта little-endian по заданному смещению.
void patchLE32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    ASSERT_LE(off + 4, v.size());
    v[off + 0] = static_cast<uint8_t>(x);
    v[off + 1] = static_cast<uint8_t>(x >> 8);
    v[off + 2] = static_cast<uint8_t>(x >> 16);
    v[off + 3] = static_cast<uint8_t>(x >> 24);
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

// PBP v0x200 с произвольным содержимым поля data.psar в offsets[7].
// Нужен, чтобы воспроизвести обработку «отсутствующей» секции.
std::vector<uint8_t> MakePBPv200WithPsar(const std::vector<uint8_t>& elf,
                                         uint32_t psarOffsetField) {
    std::vector<uint8_t> p;
    putBytes(p, "\0PBP", 4);
    putLE32(p, 0x00020000u);
    const uint32_t offsets[8] = {0, 0, 0, 0, 0, 0, 0x30u, psarOffsetField};
    for (int i = 0; i < 8; ++i) putLE32(p, offsets[i]);
    putLE32(p, static_cast<uint32_t>(elf.size()));   // data.psp size @ 0x28
    putLE32(p, 0);                                  // data.psar size @ 0x2C
    p.insert(p.end(), elf.begin(), elf.end());
    return p;
}

class LoaderTest : public ::testing::Test {
protected:
    std::vector<uint8_t> code;
    std::vector<uint8_t> elf;
    Memory mem;

    LoaderTest() {
        mem.Init();
        // addiu $2,$0,42 (0x2402002A) ; addiu $3,$0,24 (0x24030018).
        // ELF-образ PSP — MIPSEL, поэтому в файле младший байт первым:
        // 2A 00 02 24 и 18 00 03 24.
        code.push_back(0x2A); code.push_back(0x00); code.push_back(0x02); code.push_back(0x24);
        code.push_back(0x18); code.push_back(0x00); code.push_back(0x03); code.push_back(0x24);
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

// =========================================================================
// РЕГРЕССИЯ: границы секций PBP
//
// Старый код брал offsets[7] (data.psar) как верхнюю границу data.psp,
// пропуская значение 0xFFFFFFFF, которым часть писателей PBP помечает
// отсутствующую секцию. Итог — чтение примерно 4 ГиБ за пределами буфера:
// access violation на файле размером 140 байт.
// =========================================================================

TEST_F(LoaderTest, PbpV200WithPsarOffsetAllOnesIsTreatedAsAbsent) {
    // 0xFFFFFFFF — штатная метка «секции нет», а не граница буфера.
    // Старый код принимал её как nextOffset и читал ~4 ГиБ (access violation).
    // Теперь data.psp должен просто дойти до конца файла.
    const std::vector<uint8_t> pbp = MakePBPv200WithPsar(elf, 0xFFFFFFFFu);

    Image img;
    const LoadStatus st = LoadPBPFromMemory(pbp.data(), pbp.size(), img);

    ASSERT_EQ(st, LoadStatus::Ok) << StatusName(st);
    // Размер ограничен реальным файлом, а не 0xFFFFFFFF.
    EXPECT_EQ(img.elf.size(), pbp.size() - 0x30u);
    EXPECT_TRUE(img.IsValid());
}

// Смещение, которое не является «отсутствующей секцией», но уходит за
// конец файла, — это повреждение, а не «секции нет».
TEST_F(LoaderTest, RejectsPbpV200WithPsarOffsetPastEndOfFile) {
    const std::vector<uint8_t> pbp = MakePBPv200WithPsar(elf, 0x7FFFFFFFu);
    Image img;
    EXPECT_EQ(LoadPBPFromMemory(pbp.data(), pbp.size(), img),
              LoadStatus::OutOfBounds);
}

TEST_F(LoaderTest, RejectsPbpV200WithPsarOffsetNearUint32Max) {
    const std::vector<uint8_t> pbp = MakePBPv200WithPsar(elf, 0xFFFFFFF0u);
    Image img;
    EXPECT_EQ(LoadPBPFromMemory(pbp.data(), pbp.size(), img),
              LoadStatus::OutOfBounds);
}

// Смещение проскочило ровно на один байт за конец файла.
TEST_F(LoaderTest, RejectsPbpV200WithPsarOffsetOneBytePastEnd) {
    std::vector<uint8_t> pbp = MakePBPv200WithPsar(elf, 0u);
    const uint32_t past = static_cast<uint32_t>(pbp.size()) + 1u;
    patchLE32(pbp, 0x08 + 7 * 4, past);

    Image img;
    EXPECT_EQ(LoadPBPFromMemory(pbp.data(), pbp.size(), img),
              LoadStatus::OutOfBounds);
}

// Смещение ровно на границе файла — пустая секция, файл корректен.
TEST_F(LoaderTest, AcceptsPbpV200WithPsarOffsetAtEndOfFile) {
    std::vector<uint8_t> pbp = MakePBPv200WithPsar(elf, 0u);
    patchLE32(pbp, 0x08 + 7 * 4, static_cast<uint32_t>(pbp.size()));

    Image img;
    const LoadStatus st = LoadPBPFromMemory(pbp.data(), pbp.size(), img);
    ASSERT_EQ(st, LoadStatus::Ok) << StatusName(st);
    EXPECT_EQ(img.elf.size(), pbp.size() - 0x30u);
}

// Нормальный случай: data.psar есть и лежит сразу за data.psp.
TEST_F(LoaderTest, AcceptsPbpV200WithRealPsarSection) {
    const uint32_t psarOffset = 0x30u + static_cast<uint32_t>(elf.size());
    std::vector<uint8_t> pbp = MakePBPv200WithPsar(elf, psarOffset);
    for (int i = 0; i < 16; ++i) pbp.push_back(0x00);

    Image img;
    const LoadStatus st = LoadPBPFromMemory(pbp.data(), pbp.size(), img);
    ASSERT_EQ(st, LoadStatus::Ok) << StatusName(st);
    EXPECT_EQ(img.elf.size(), elf.size());
}

// data.psp — последняя секция, data.psar отсутствует: размер берётся
// по концу файла, а не по 0xFFFFFFFF.
TEST_F(LoaderTest, AcceptsPbpV200WithoutPsar) {
    const std::vector<uint8_t> pbp = MakePBPv200WithPsar(elf, 0u);
    Image img;
    const LoadStatus st = LoadPBPFromMemory(pbp.data(), pbp.size(), img);
    ASSERT_EQ(st, LoadStatus::Ok) << StatusName(st);
    EXPECT_EQ(img.elf.size(), elf.size());
}

// Переполнение при проверке «offset + size <= size».
TEST_F(LoaderTest, RejectsPbpV100WithOversizedDataPspSize) {
    std::vector<uint8_t> pbp = MakePBP(elf, 0x00010000u);
    patchLE32(pbp, 0x28, 0xFFFFFF00u);      // data.psp size абсурдно велик

    Image img;
    const LoadStatus st = LoadPBPFromMemory(pbp.data(), pbp.size(), img);
    EXPECT_NE(st, LoadStatus::Ok);
    EXPECT_TRUE(img.elf.empty());
}

TEST_F(LoaderTest, RejectsPbpWithDataPspOffsetPastEnd) {
    std::vector<uint8_t> pbp = MakePBP(elf, 0x00010000u);
    patchLE32(pbp, 0x08 + 6 * 4, 0x7FFFFFFFu);

    Image img;
    EXPECT_NE(LoadPBPFromMemory(pbp.data(), pbp.size(), img), LoadStatus::Ok);
}

TEST_F(LoaderTest, RejectsPbpWithDataPspOffsetAllOnes) {
    std::vector<uint8_t> pbp = MakePBP(elf, 0x00010000u);
    patchLE32(pbp, 0x08 + 6 * 4, 0xFFFFFFFFu);

    Image img;
    EXPECT_NE(LoadPBPFromMemory(pbp.data(), pbp.size(), img), LoadStatus::Ok);
}

// Поле размера data.psp нулевое при непустом файле.
TEST_F(LoaderTest, RejectsPbpV100WithZeroDataPspSize) {
    std::vector<uint8_t> pbp = MakePBP(elf, 0x00010000u);
    patchLE32(pbp, 0x28, 0u);

    Image img;
    EXPECT_EQ(LoadPBPFromMemory(pbp.data(), pbp.size(), img),
              LoadStatus::EmptyDataPsp);
}

// Смещение секции указывает внутрь области, занятой другой секцией.
TEST_F(LoaderTest, RejectsPbpV200WithOverlappingSections) {
    const uint32_t psarOffset = 0x30u + static_cast<uint32_t>(elf.size());
    std::vector<uint8_t> pbp = MakePBPv200WithPsar(elf, psarOffset);
    // data.psp сдвигаем вперёд так, что data.psar оказывается внутри него
    patchLE32(pbp, 0x08 + 6 * 4, 0x30u + 4u);

    Image img;
    EXPECT_NE(LoadPBPFromMemory(pbp.data(), pbp.size(), img), LoadStatus::Ok);
}

// param.sfo помечен как 0xFFFFFFFF — не должно ломать разбор (это
// «секции нет»), но и не должно читаться как смещение.
TEST_F(LoaderTest, ToleratesAbsentSfoOffset) {
    std::vector<uint8_t> pbp = MakePBP(elf, 0x00010000u);
    patchLE32(pbp, 0x08 + 0 * 4, 0xFFFFFFFFu);

    Image img;
    const LoadStatus st = LoadPBPFromMemory(pbp.data(), pbp.size(), img);
    ASSERT_EQ(st, LoadStatus::Ok) << StatusName(st);
    EXPECT_EQ(img.elf.size(), elf.size());
    EXPECT_EQ(img.titleId, 0u);
}

// ---- регрессия: границы сегментов ELF -------------------------------------

TEST(LoaderBoundaryTest, RejectsSegmentWithOverflowingOffsetAndSize) {
    // p_offset = 0xFFFFFF00, p_filesz = 0x100 — сумма переполняет uint32
    // и проходит старую проверку `offset + filesz > size`.
    std::vector<uint8_t> e;
    e.push_back(0x7F); e.push_back('E'); e.push_back('L'); e.push_back('F');
    e.push_back(1); e.push_back(1); e.push_back(1);
    for (int i = 0; i < 9; ++i) e.push_back(0);
    putLE16(e, 2); putLE16(e, 8); putLE32(e, 1);
    putLE32(e, 0x08804000u); putLE32(e, 52); putLE32(e, 0); putLE32(e, 0);
    putLE16(e, 52); putLE16(e, 32); putLE16(e, 1);
    putLE16(e, 0); putLE16(e, 0); putLE16(e, 0);
    putLE32(e, 1); putLE32(e, 0xFFFFFF00u); putLE32(e, 0x08804000u);
    putLE32(e, 0x08804000u);
    putLE32(e, 0x100u); putLE32(e, 0x100u); putLE32(e, 5); putLE32(e, 0x1000);

    Image img;
    const LoadStatus st = LoadELF(e.data(), e.size(), img);
    EXPECT_EQ(st, LoadStatus::OutOfBounds);
    EXPECT_TRUE(img.segments.empty());
}

TEST(LoaderBoundaryTest, RejectsSegmentWithMemSizeOverflow) {
    // p_filesz = 0, p_memsz = 0xFFFFFFFF при p_vaddr в RAM: запись .bss
    // такого размера должна быть отвергнута, а не тихо обрезана.
    std::vector<uint8_t> e;
    e.push_back(0x7F); e.push_back('E'); e.push_back('L'); e.push_back('F');
    e.push_back(1); e.push_back(1); e.push_back(1);
    for (int i = 0; i < 9; ++i) e.push_back(0);
    putLE16(e, 2); putLE16(e, 8); putLE32(e, 1);
    putLE32(e, 0x08804000u); putLE32(e, 52); putLE32(e, 0); putLE32(e, 0);
    putLE16(e, 52); putLE16(e, 32); putLE16(e, 1);
    putLE16(e, 0); putLE16(e, 0); putLE16(e, 0);
    putLE32(e, 1); putLE32(e, 0); putLE32(e, 0x08804000u);
    putLE32(e, 0x08804000u);
    putLE32(e, 0u); putLE32(e, 0xFFFFFFFFu); putLE32(e, 6); putLE32(e, 0x1000);

    Image img;
    const LoadStatus st = LoadELF(e.data(), e.size(), img);
    EXPECT_EQ(st, LoadStatus::OutOfBounds);
}

} // namespace