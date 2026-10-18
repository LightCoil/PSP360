// test_xex.cpp - независимая проверка структуры выходного XEX2.
//
// Тесты НЕ доверяют коду xex_writer: файл разбирается заново по
// раскладке из документации Free60 (System Software / Formats / XEX).
#include "xex_writer.hpp"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

int g_fail = 0;
int g_pass = 0;

void check(bool cond, const char* what) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL: %s\n", what);
    }
}

uint32_t rd32(const std::vector<uint8_t>& v, size_t off) {
    return (uint32_t(v[off]) << 24) | (uint32_t(v[off + 1]) << 16) |
           (uint32_t(v[off + 2]) << 8) | uint32_t(v[off + 3]);
}

// --- минимальный корректный ELF32 big-endian с .text и .data -------------
// Собирается прямо здесь, чтобы тест не зависел от реального ELF-файла
// и от наличия тулчейна.
std::vector<uint8_t> make_elf(uint32_t entry) {
    const uint32_t text_off = 0x1000, text_sz = 0x10;
    const uint32_t data_off = 0x2000, data_sz = 0x08;
    const uint32_t shstr_off = 0x3000;
    const uint32_t shoff = 0x100;
    const uint16_t shnum = 5;      // 0 null, 1 .text, 2 .data, 3 .shstrtab, 4 .bss
    const uint16_t shstrndx = 3;
    const uint16_t shent = 40;

    static const char kShstr[] = "\0.text\0.data\0.shstrtab\0.bss";
    const uint32_t shstr_sz = static_cast<uint32_t>(sizeof(kShstr));

    std::vector<uint8_t> e(0x4000, 0);
    std::memcpy(&e[0], "\x7F" "ELF", 4);
    e[4] = 1;    // ELFCLASS32
    e[5] = 2;    // ELFDATA2MSB (big-endian)
    e[6] = 1;    // EV_CURRENT
    e[16] = 2;   // e_type = ET_EXEC
    e[18] = 20;  // e_machine = EM_PPC
    auto w = [&](size_t off, uint32_t v) {
        e[off]   = uint8_t((v >> 24) & 0xFF);
        e[off+1] = uint8_t((v >> 16) & 0xFF);
        e[off+2] = uint8_t((v >> 8) & 0xFF);
        e[off+3] = uint8_t(v & 0xFF);
    };
    w(0x18, entry);   // e_entry
    w(0x20, shoff);   // e_shoff
    e[0x2E] = uint8_t(shent >> 8); e[0x2F] = uint8_t(shent & 0xFF);
    e[0x30] = uint8_t(shnum >> 8); e[0x31] = uint8_t(shnum & 0xFF);
    e[0x32] = uint8_t(shstrndx >> 8); e[0x33] = uint8_t(shstrndx & 0xFF);

    auto sec = [&](uint16_t i, uint32_t name, uint32_t type,
                   uint32_t off, uint32_t sz) {
        uint8_t* p = &e[shoff + size_t(i) * shent];
        auto put = [&](size_t o, uint32_t v) {
            p[o]   = uint8_t((v >> 24) & 0xFF);
            p[o+1] = uint8_t((v >> 16) & 0xFF);
            p[o+2] = uint8_t((v >> 8) & 0xFF);
            p[o+3] = uint8_t(v & 0xFF);
        };
        put(0x00, name);
        put(0x04, type);
        put(0x08, 0);
        put(0x0C, 0);
        put(0x10, off);
        put(0x14, sz);
    };
    sec(0, 0, 0, 0, 0);
    sec(1, 1,  1, text_off, text_sz);
    sec(2, 7,  1, data_off, data_sz);
    sec(3, 13, 3, shstr_off, shstr_sz);
    sec(4, 22, 8, 0, 0x200);          // .bss - без данных в файле

    std::memcpy(&e[shstr_off], kShstr, shstr_sz);
    for (uint32_t i = 0; i < text_sz; ++i) e[text_off + i] = uint8_t(0x4E + i);
    for (uint32_t i = 0; i < data_sz; ++i) e[data_off + i] = uint8_t(0xA0 + i);
    return e;
}

void write_file(const char* path, const std::vector<uint8_t>& d) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fwrite(d.data(), 1, d.size(), f);
    std::fclose(f);
}

// Разбор списка Optional Headers строго по раскладке Free60.
//   (ID & 0xFF) == 0x00 -> Header Data это размер блока в DWORD'ах
//   (ID & 0xFF) == 0x01 -> Header Data само является данными, блока нет
//   (ID & 0xFF) == 0xFF -> Header Data это размер блока в байтах,
//                          блок начинается со своего размера (DWORD)
struct Opt { uint32_t id; uint32_t data_field; size_t body_off; size_t body_size; };

bool parse_opts(const std::vector<uint8_t>& x, std::vector<Opt>& out, std::string& why) {
    if (x.size() < 24) { why = "файл короче 24 байт"; return false; }
    const uint32_t count = rd32(x, 0x14);
    size_t p = 24;
    for (uint32_t i = 0; i < count; ++i) {
        if (p + 8 > x.size()) { why = "заголовок обрезан"; return false; }
        Opt o;
        o.id = rd32(x, p);
        o.data_field = rd32(x, p + 4);
        p += 8;
        const uint32_t low = o.id & 0xFFu;
        if (low == 0x01) {
            o.body_off = 0; o.body_size = 0;          // данных за полем нет
        } else if (low == 0xFF) {
            o.body_size = o.data_field;
            o.body_off = p;
            if (o.body_size < 4) { why = "самоописываемый блок меньше DWORD"; return false; }
            // первый DWORD блока обязан совпадать с длиной остатка блока
            const uint32_t inner = rd32(x, p);
            if (inner + 4u != o.body_size) {
                why = "внутренний размер блока не сходится с длиной блока";
                return false;
            }
            p += o.body_size;
        } else {
            o.body_size = size_t(o.data_field) * 4;
            o.body_off = p;
            p += o.body_size;
        }
        if (p > x.size()) { why = "блок выходит за пределы файла"; return false; }
        out.push_back(o);
    }
    return true;
}

} // namespace

int main() {
    std::printf("=== xex_writer: проверка по спецификации XEX2 (Free60) ===\n");

    const uint32_t kEntry = 0x00008200u;   // типичный адрес входа для XEX
    const std::vector<uint8_t> elf = make_elf(kEntry);

    // ---- [1] отказ на некорректном входе --------------------------------
    {
        std::vector<uint8_t> bad, out; std::string err;
        std::map<std::string, std::vector<std::string>> m;
        check(!xexlib::write_xex_from_elf(bad, m, m, out, err),
              "пустой вход должен отклоняться");
        check(!err.empty(), "при отказе заполняется строка ошибки");

        std::vector<uint8_t> le = elf; le[5] = 1;   // little-endian
        check(!xexlib::write_xex_from_elf(le, m, m, out, err),
              "little-endian ELF должен отклоняться");
    }

    // ---- [2] формирование XEX --------------------------------------------
    std::vector<uint8_t> xex; std::string err;
    std::map<std::string, std::vector<std::string>> noLibs;
    const bool made = xexlib::write_xex_from_elf(elf, noLibs, noLibs, xex, err);
    check(made, "корректный ELF32 BE должен приниматься");
    if (!made) { std::printf("  ошибка: %s\n", err.c_str()); return 1; }

    // ---- [3] фиксированный заголовок: ровно 24 байта ---------------------
    check(xex.size() > 24, "файл длиннее фиксированного заголовка");
    check(xex[0] == 'X' && xex[1] == 'E' && xex[2] == 'X' && xex[3] == '2',
          "сигнатура XEX2");
    check(rd32(xex, 0x08) != 0, "PE data offset ненулевой");
    check((rd32(xex, 0x08) & 0x1FF) == 0, "PE data offset выровнен");
    const uint32_t count = rd32(xex, 0x14);
    check(count > 0, "optional header count ненулевой");

    // ---- [4] разбор optional headers -------------------------------------
    std::vector<Opt> opts;
    std::string why;
    const bool structure_ok = parse_opts(xex, opts, why);
    check(structure_ok, "optional headers разбираются по спецификации");
    if (!structure_ok) std::printf("  причина: %s\n", why.c_str());
    check(opts.size() == count, "число разобранных заголовков равно count");

    // ---- [5] наличие обязательных ключей ---------------------------------
    bool has_entry = false, has_base = false;
    uint32_t entry_value = 0, base_value = 0;
    for (const Opt& o : opts) {
        if (o.id == 0x00010100u) {           // Entry Point, класс 0x00
            has_entry = true;
            entry_value = rd32(xex, o.body_off);
        }
        if (o.id == 0x00010201u) {           // Image Base, класс 0x01
            has_base = true;
            base_value = o.data_field;       // значение лежит в самом поле
            check(o.body_size == 0, "у Image Base нет отдельного тела");
        }
    }
    check(has_entry, "есть optional header Entry Point (0x10100)");
    check(has_base,  "есть optional header Image Base (0x10201)");
    check(entry_value == kEntry, "значение Entry Point совпадает с e_entry");
    check(base_value == 0x82000000u, "Image Base = 0x82000000");

    // ---- [6] запрещённые ключи отсутствуют -------------------------------
    for (uint32_t bad : {0x000002FFu, 0x00000300u, 0x00000400u,
                         0x00000800u, 0x00000C00u}) {
        bool present = false;
        for (const Opt& o : opts) if (o.id == bad) present = true;
        check(!present, "устаревший ключ отсутствует в выходном файле");
    }

    // ---- [7] нагрузка на месте по PE data offset --------------------------
    const uint32_t pe = rd32(xex, 0x08);
    check(pe < xex.size(), "PE data offset внутри файла");
    if (pe < xex.size()) {
        // первые 16 байт .text - возрастающая последовательность 0x4E..0x5D
        bool ok = true;
        for (uint32_t i = 0; i < 16 && pe + i < xex.size(); ++i) {
            if (xex[pe + i] != uint8_t(0x4E + i)) ok = false;
        }
        check(ok, "по PE data offset лежит содержимое .text");
    }

    // ---- [8] библиотеки импорта кодируются по спецификации ----------------
    {
        std::map<std::string, std::vector<std::string>> imp;
        imp["xboxkrnl"] = {"XutThreadDelay"};
        std::vector<uint8_t> x2; std::string e2;
        check(xexlib::write_xex_from_elf(elf, imp, noLibs, x2, e2),
              "XEX с библиотеками импорта создаётся");
        std::vector<Opt> o2;
        std::string w2;
        check(parse_opts(x2, o2, w2), "XEX с импортами разбирается");
        bool found = false;
        for (const Opt& o : o2) {
            if (o.id == 0x000103FFu) {
                found = true;
                check(o.body_size > 4, "размер блока импортов ненулевой");
                check(o.body_off + o.body_size <= x2.size(),
                      "блок импортов помещается в файл");
            }
        }
        check(found, "есть optional header Import Libraries (0x103FF)");
    }

    // ---- [9] артефакты для внешней проверки ------------------------------
    write_file("test_out.xex", xex);
    write_file("test_in.elf", elf);

    std::printf("=== успешно %d, неудачно %d ===\n", g_pass, g_fail);
    std::printf("записано: test_out.xex (%zu байт), test_in.elf (%zu байт)\n",
                xex.size(), elf.size());
    return g_fail == 0 ? 0 : 1;
}
