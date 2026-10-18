// xex_writer.cpp - формирование контейнера XEX2 для Xbox 360.
//
// Спецификация: XEX Header - ровно 24 байта, big-endian
// (Free60, System Software / Formats / XEX):
//
//   0x00  4  ascii        "XEX2"
//   0x04  4  module flags (битовое поле)
//   0x08  4  uint32       PE data offset
//   0x0C  4  uint32       reserved
//   0x10  4  uint32       security info offset
//   0x14  4  uint32       optional header count
//
// Далее идут Optional Headers, каждый из двух uint32:
//   Header ID, Header Data.
// Размер данных определяется младшим байтом ID:
//   (ID & 0xFF) == 0x01  -> Header Data СОДЕРЖИТ данные (инлайн);
//   (ID & 0xFF) == 0xFF  -> данные содержат свой размер в первом DWORD;
//   иначе                -> Header Data это размер в DWORD'ах.
//
// ВАЖНО: перечисленные выше ключи взяты из документации Free60.
// Идентификаторы, использовавшиеся в прежней версии этого файла
// (0x2FF, 0x300, 0x400, 0x800, 0xC00), не соответствуют спецификации:
//   0x2FF   -> Resource Info, а не Entry Point
//   0x300   -> не определён
//   0x400   -> не определён (таблицы секций в XEX2 нет)
//   0x800   -> не определён
//   0xC00   -> не определён
// Подробности и тесты: tests/test_xex.cpp.

#include "xex_writer.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace xexlib {

// --- ключи Optional Headers (Free60, System Software / Formats / XEX) ------
constexpr uint32_t XEX_OPT_ENTRY_POINT       = 0x00010100u;  // &0xFF = 0x00 -> размер в DWORD
constexpr uint32_t XEX_OPT_IMAGE_BASE        = 0x00010201u;  // &0xFF = 0x01 -> инлайн
constexpr uint32_t XEX_OPT_ORIGINAL_BASE      = 0x00010001u;
constexpr uint32_t XEX_OPT_IMPORT_LIBRARIES  = 0x000103FFu;  // &0xFF = 0xFF -> свой размер
constexpr uint32_t XEX_OPT_STATIC_LIBRARIES  = 0x000200FFu;
constexpr uint32_t XEX_OPT_BASE_FILE_FORMAT   = 0x000003FFu;

// Размер фиксированного заголовка XEX2.
constexpr size_t XEX_HEADER_SIZE = 24;

namespace {

void put_u8(std::vector<uint8_t>& v, uint8_t b) { v.push_back(b); }

void put_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}

uint32_t get_u32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

void put_utf16le(std::vector<uint8_t>& v, const std::string& s) {
    for (unsigned char c : s) {
        v.push_back(c);
        v.push_back(0);
    }
    v.push_back(0);   // завершающий NUL
    v.push_back(0);
}

size_t align_up(size_t v, size_t a) {
    if (a == 0) return v;
    return (v + a - 1) & ~(a - 1);
}

// Модульные флаги XEX2 (Free60).
// bit 7 = User Mode - для обычной пользовательской программы.
constexpr uint32_t XEX_MODULE_FLAG_USER_MODE = 0x00000080u;

} // namespace

// ---------------------------------------------------------------------------
// Разбор ELF32 big-endian (PowerPC) - минимально достаточный:
// нужны entry point и содержимое загружаемых секций.
// ---------------------------------------------------------------------------
bool elf_extract(const std::vector<uint8_t>& elf,
                 std::vector<uint8_t>& payload,
                 uint32_t& entry_point,
                 std::string& error) {
    if (elf.size() < 52 || std::memcmp(elf.data(), "\x7F" "ELF", 4) != 0) {
        error = "не ELF или слишком мал";
        return false;
    }
    if (elf[4] != 1) { error = "ожидается ELF32 (EI_CLASS=1)"; return false; }
    if (elf[5] != 2) { error = "ожидается big-endian (EI_DATA=2)"; return false; }

    entry_point = get_u32(&elf[0x18]);

    const uint32_t shoff = get_u32(&elf[0x20]);
    const uint16_t shentsize = static_cast<uint16_t>((elf[0x2E] << 8) | elf[0x2F]);
    const uint16_t shnum = static_cast<uint16_t>((elf[0x30] << 8) | elf[0x31]);
    const uint16_t shstrndx = static_cast<uint16_t>((elf[0x32] << 8) | elf[0x33]);

    if (shentsize < 40) { error = "некорректный размер записи секции"; return false; }
    if (static_cast<uint64_t>(shoff) +
            static_cast<uint64_t>(shentsize) * shnum > elf.size()) {
        error = "заголовки секций за пределами файла";
        return false;
    }

    const uint8_t* shstr = nullptr;
    uint32_t shstr_size = 0;
    if (shstrndx < shnum) {
        const uint8_t* sh = &elf[shoff + static_cast<size_t>(shstrndx) * shentsize];
        const uint32_t off = get_u32(sh + 0x10);
        const uint32_t sz  = get_u32(sh + 0x14);
        if (static_cast<uint64_t>(off) + sz <= elf.size()) {
            shstr = &elf[off];
            shstr_size = sz;
        }
    }

    payload.clear();
    for (uint16_t i = 0; i < shnum; ++i) {
        const uint8_t* sh = &elf[shoff + static_cast<size_t>(i) * shentsize];
        const uint32_t name_off = get_u32(sh + 0x00);
        const uint32_t type     = get_u32(sh + 0x04);
        const uint32_t off      = get_u32(sh + 0x10);
        const uint32_t sz       = get_u32(sh + 0x14);
        if (type != 1 /* SHT_PROGBITS */) continue;   // .bss не несёт данных
        if (static_cast<uint64_t>(off) + sz > elf.size()) continue;
        std::string name;
        if (shstr && name_off < shstr_size) {
            name.assign(reinterpret_cast<const char*>(shstr + name_off));
        }
        if (name == ".text" || name == ".data" || name == ".rodata") {
            payload.insert(payload.end(), elf.begin() + off, elf.begin() + off + sz);
        }
    }
    if (payload.empty()) { error = "не найдено ни одной загружаемой секции"; return false; }
    return true;
}

bool write_xex_from_elf(const std::vector<uint8_t>& elf,
                        const std::map<std::string, std::vector<std::string>>& importLibs,
                        const std::map<std::string, std::vector<std::string>>& exportLibs,
                        std::vector<uint8_t>& xex_out,
                        std::string& error) {
    std::vector<uint8_t> payload;
    uint32_t entry = 0;
    if (!elf_extract(elf, payload, entry, error)) return false;
    if (entry == 0) { error = "нулевая точка входа"; return false; }

    // ---- данные Optional Headers -----------------------------------------
    // Кодирование размера зависит от младшего байта ID (см. спецификацию):
    //   (ID & 0xFF) == 0x00 -> поле Header Data есть размер блока в DWORD'ах,
    //                         сами данные идут следом;
    //   (ID & 0xFF) == 0x01 -> поле Header Data САМО является данными,
    //                         дополнительных байт нет;
    //   (ID & 0xFF) == 0xFF -> поле Header Data есть размер блока в байтах,
    //                         блок начинается со своего размера (DWORD).
    struct OptEntry { uint32_t id; uint32_t data_field; std::vector<uint8_t> body; };
    std::vector<OptEntry> opts;

    // Entry Point (0x10100): класс 0x00, блок = одно DWORD.
    { OptEntry e{XEX_OPT_ENTRY_POINT, 1, {}};
      put_u32(e.body, entry);
      opts.push_back(std::move(e)); }

    // Image Base (0x10201): класс 0x01 -> значение прямо в поле Header Data.
    { OptEntry e{XEX_OPT_IMAGE_BASE, 0x82000000u, {}};
      opts.push_back(std::move(e)); }

    // Блок класса 0xFF: сам содержит свой размер первым DWORD.
    // Вызывающий передаёт содержимое БЕЗ этого размера.
    auto add_self_sized = [&](uint32_t id, const std::vector<uint8_t>& inner) {
        OptEntry e;
        e.id = id;
        put_u32(e.body, static_cast<uint32_t>(inner.size()));   // размер внутри
        e.body.insert(e.body.end(), inner.begin(), inner.end());
        while ((e.body.size() % 4) != 0) e.body.push_back(0);   // выравнивание
        e.data_field = static_cast<uint32_t>(e.body.size());     // размер снаружи
        opts.push_back(std::move(e));
    };

    // Base File Format (0x3FF): класс 0xFF.
    { std::vector<uint8_t> inner;
      put_utf16le(inner, "xex");
      add_self_sized(XEX_OPT_BASE_FILE_FORMAT, inner); }

    auto libs_blob = [](const std::map<std::string, std::vector<std::string>>& libs) {
        std::vector<uint8_t> d;
        for (const auto& kv : libs) {
            std::string upper = kv.first;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            put_utf16le(d, upper);
            put_u32(d, static_cast<uint32_t>(kv.second.size()));
            for (const auto& fn : kv.second) put_utf16le(d, fn);
        }
        return d;
    };

    if (!importLibs.empty()) add_self_sized(XEX_OPT_IMPORT_LIBRARIES, libs_blob(importLibs));
    if (!exportLibs.empty()) add_self_sized(XEX_OPT_STATIC_LIBRARIES, libs_blob(exportLibs));

    // ---- тело файла: заголовок + optional headers + полезная нагрузка ---
    // PE data offset: нагрузка начинается с выровненной границы после
    // заголовка и optional headers. Это поле обязано указывать на неё.
    const size_t pe_offset = align_up(XEX_HEADER_SIZE, 0x200);

    std::vector<uint8_t> out;
    out.reserve(pe_offset + payload.size());
    put_u32(out, 0x58455832u);                 // 0x00 "XEX2"
    put_u32(out, XEX_MODULE_FLAG_USER_MODE);    // 0x04 module flags
    put_u32(out, static_cast<uint32_t>(pe_offset)); // 0x08 PE data offset
    put_u32(out, 0);                            // 0x0C reserved
    put_u32(out, 0);                            // 0x10 security info offset
    put_u32(out, static_cast<uint32_t>(opts.size())); // 0x14 count

    for (const OptEntry& o : opts) {
        put_u32(out, o.id);
        put_u32(out, o.data_field);
        out.insert(out.end(), o.body.begin(), o.body.end());
    }

    if (out.size() > pe_offset) {
        error = "optional headers не помещаются до границы PE data";
        return false;
    }
    out.resize(pe_offset, 0);
    out.insert(out.end(), payload.begin(), payload.end());

    xex_out.swap(out);
    return true;
}

} // namespace xexlib
