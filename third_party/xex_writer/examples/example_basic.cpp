// example_basic.cpp - минимальный пример использования xex_writer.
//
// Собирает ELF32 big-endian в памяти (без внешнего тулчейна),
// прогоняет через xex_writer и печатает разобранные поля заголовка XEX2.
#include "xex_writer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> make_minimal_elf(uint32_t entry) {
    const uint32_t text_off = 0x1000, text_sz = 0x10;
    const uint32_t shstr_off = 0x3000;
    const uint32_t shoff = 0x100;
    const uint16_t shnum = 4, shstrndx = 2, shent = 40;
    static const char kShstr[] = "\0.text\0.shstrtab\0.data";
    const uint32_t shstr_sz = static_cast<uint32_t>(sizeof(kShstr));

    std::vector<uint8_t> e(0x4000, 0);
    std::memcpy(&e[0], "\x7F" "ELF", 4);
    e[4] = 1; e[5] = 2; e[6] = 1;
    e[16] = 2; e[18] = 20;
    auto w = [&](size_t off, uint32_t v) {
        e[off] = uint8_t((v >> 24) & 0xFF); e[off+1] = uint8_t((v >> 16) & 0xFF);
        e[off+2] = uint8_t((v >> 8) & 0xFF); e[off+3] = uint8_t(v & 0xFF);
    };
    w(0x18, entry);
    w(0x20, shoff);
    e[0x2E] = uint8_t(shent >> 8); e[0x2F] = uint8_t(shent & 0xFF);
    e[0x30] = uint8_t(shnum >> 8); e[0x31] = uint8_t(shnum & 0xFF);
    e[0x32] = uint8_t(shstrndx >> 8); e[0x33] = uint8_t(shstrndx & 0xFF);
    auto sec = [&](uint16_t i, uint32_t name, uint32_t type,
                   uint32_t off, uint32_t sz) {
        uint8_t* p = &e[shoff + size_t(i) * shent];
        auto put = [&](size_t o, uint32_t v) {
            p[o] = uint8_t((v >> 24) & 0xFF); p[o+1] = uint8_t((v >> 16) & 0xFF);
            p[o+2] = uint8_t((v >> 8) & 0xFF); p[o+3] = uint8_t(v & 0xFF);
        };
        put(0x00, name); put(0x04, type); put(0x10, off); put(0x14, sz);
    };
    sec(0, 0, 0, 0, 0);
    sec(1, 1, 1, text_off, text_sz);
    sec(2, 7, 3, shstr_off, shstr_sz);
    sec(3, 14, 1, 0x2000, 8);
    std::memcpy(&e[shstr_off], kShstr, shstr_sz);
    for (uint32_t i = 0; i < text_sz; ++i) e[text_off + i] = uint8_t(0x60 + i);
    for (uint32_t i = 0; i < 8; ++i) e[0x2000 + i] = uint8_t(0xB0 + i);
    return e;
}

} // namespace

int main() {
    const std::vector<uint8_t> elf = make_minimal_elf(0x00008200u);

    std::map<std::string, std::vector<std::string>> imports;
    imports["xboxkrnl"] = {"XutThreadDelay"};

    std::vector<uint8_t> xex;
    std::string err;
    if (!xexlib::write_xex_from_elf(elf, imports, {}, xex, err)) {
        std::printf("ошибка: %s\n", err.c_str());
        return 1;
    }

    auto rd = [&](size_t o) {
        return (uint32_t(xex[o]) << 24) | (uint32_t(xex[o+1]) << 16) |
               (uint32_t(xex[o+2]) << 8) | uint32_t(xex[o+3]);
    };
    std::printf("XEX2 сформирован, %zu байт\n", xex.size());
    std::printf("  magic            = %c%c%c%c\n", xex[0], xex[1], xex[2], xex[3]);
    std::printf("  module flags     = 0x%08X\n", rd(0x04));
    std::printf("  PE data offset   = 0x%08X\n", rd(0x08));
    std::printf("  security info    = 0x%08X\n", rd(0x10));
    std::printf("  opt header count = %u\n", rd(0x14));
    return 0;
}
