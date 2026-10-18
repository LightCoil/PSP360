#include "xex_writer.hpp"
#include <cstring>
#include <map>
#include <vector>
#include <cstdint>
#include <string>
#include <algorithm>

namespace xexlib {

// XEX Optional Header Keys
static constexpr uint32_t XEX_HEADER_ENTRY_POINT      = 0x000002FF;
static constexpr uint32_t XEX_HEADER_IMAGE_BASE       = 0x00000300;
static constexpr uint32_t XEX_HEADER_SECTION_TABLE    = 0x00000400;
static constexpr uint32_t XEX_HEADER_IMPORT_LIBRARIES = 0x00000800;
static constexpr uint32_t XEX_HEADER_EXPORT_LIBRARIES = 0x00000C00;

// Section flags
static constexpr uint32_t XEX_SECTION_CODE = 0x20;
static constexpr uint32_t XEX_SECTION_DATA = 0x40;
static constexpr uint32_t XEX_SECTION_BSS  = 0x80;

// Write 32-bit big-endian
static void write_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((v >> 24) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back(v & 0xFF);
}

// Overwrite 32-bit BE at position
static void patch_u32(std::vector<uint8_t>& out, size_t pos, uint32_t v) {
    if (pos + 4 > out.size()) return;
    out[pos]     = (v >> 24) & 0xFF;
    out[pos + 1] = (v >> 16) & 0xFF;
    out[pos + 2] = (v >> 8) & 0xFF;
    out[pos + 3] = v & 0xFF;
}

// Align to `align` boundary (power of two)
static void write_padding(std::vector<uint8_t>& out, size_t align) {
    if (align == 0) return;
    size_t pad = (align - (out.size() % align)) % align;
    if (pad) out.insert(out.end(), pad, 0);
}

// Convert ASCII to UTF-16LE
static void write_utf16le(std::vector<uint8_t>& out, const std::string& s) {
    for (unsigned char c : s) {
        out.push_back(c);
        out.push_back(0);
    }
    out.push_back(0); out.push_back(0);
}

// Align offset up to align
static uint32_t align_up(uint32_t val, uint32_t align) {
    return (val + align - 1) & ~(align - 1);
}

// Read 32-bit BE from buffer
static uint32_t read_u32(const uint8_t* buf) {
    return (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) |
           (uint32_t(buf[2]) << 8)  | uint32_t(buf[3]);
}

// Extract .text, .data, .bss and entry_point from ELF32 BE with shstrtab
static bool extract_elf_sections(const std::vector<uint8_t>& elf,
                                 std::vector<uint8_t>& text,
                                 std::vector<uint8_t>& data,
                                 std::vector<uint8_t>& bss,
                                 uint32_t& entry_point,
                                 std::string& error) {
    if (elf.size() < 0x34 || std::memcmp(elf.data(), "\x7FELF", 4) != 0) {
        error = "Invalid or too small ELF";
        return false;
    }
    const uint8_t* hdr = elf.data();
    if (hdr[4] != 1 || hdr[5] != 2) {
        error = "Unsupported ELF (need 32-bit BE)";
        return false;
    }
    entry_point = read_u32(hdr + 0x18);
    uint32_t shoff   = read_u32(hdr + 0x20);
    uint16_t shentsz = (hdr[0x2E] << 8) | hdr[0x2F];
    uint16_t shnum   = (hdr[0x30] << 8) | hdr[0x31];
    uint16_t shstrndx= (hdr[0x32] << 8) | hdr[0x33];
    if (uint64_t(shoff) + uint64_t(shentsz)*shnum > elf.size()) {
        error = "Section headers out of bounds";
        return false;
    }
    // Locate shstrtab
    const uint8_t* shstrtab = nullptr;
    uint32_t shstrsz = 0;
    if (shstrndx < shnum) {
        const uint8_t* sh = elf.data() + shoff + shstrndx*shentsz;
        uint32_t off = read_u32(sh + 0x10);
        shstrsz = read_u32(sh + 0x14);
        if (off + shstrsz <= elf.size()) shstrtab = elf.data() + off;
    }
    // Iterate sections
    for (uint16_t i = 0; i < shnum; ++i) {
        const uint8_t* sh = elf.data() + shoff + i*shentsz;
        uint32_t nameoff = read_u32(sh + 0x00);
        uint32_t type    = read_u32(sh + 0x04);
        uint32_t flags   = read_u32(sh + 0x08);
        uint32_t off     = read_u32(sh + 0x10);
        uint32_t sz      = read_u32(sh + 0x14);
        if (off + sz > elf.size()) continue;
        std::string secname;
        if (shstrtab && nameoff < shstrsz) secname = std::string((const char*)(shstrtab + nameoff));
        if (type == 1 && secname == ".text")
            text.insert(text.end(), elf.begin()+off, elf.begin()+off+sz);
        else if (type == 1 && secname == ".data")
            data.insert(data.end(), elf.begin()+off, elf.begin()+off+sz);
        else if (type == 8 && secname == ".bss")
            bss.resize(bss.size() + sz, 0);
    }
    if (text.empty()) { error = "Missing .text"; return false; }
    return true;
}

bool write_xex_from_elf(const std::vector<uint8_t>& elf,
                        const std::map<std::string,std::vector<std::string>>& importLibs,
                        const std::map<std::string,std::vector<std::string>>& exportLibs,
                        std::vector<uint8_t>& xex_out,
                        std::string& error) {
    std::vector<uint8_t> text, data, bss;
    uint32_t entry = 0;
    if (!extract_elf_sections(elf, text, data, bss, entry, error)) return false;
    if (entry == 0) { error = "Invalid entry point"; return false; }

    using OptPair = std::pair<uint32_t,std::vector<uint8_t>>;
    std::vector<OptPair> opts;
    // ENTRY_POINT
    { std::vector<uint8_t> buf; write_u32(buf, entry); opts.emplace_back(XEX_HEADER_ENTRY_POINT, std::move(buf)); }
    // IMAGE_BASE
    { std::vector<uint8_t> buf; write_u32(buf, 0x82000000); opts.emplace_back(XEX_HEADER_IMAGE_BASE, std::move(buf)); }
    // SECTION_TABLE
    {
        std::vector<uint8_t> buf;
        uint32_t cnt = 1 + (!data.empty()) + (!bss.empty()); write_u32(buf, cnt);
        uint32_t off = align_up(static_cast<uint32_t>(0 /* header end placeholder*/), 0x1000);
        // .text
        write_u32(buf, off); write_u32(buf, text.size()); write_u32(buf, XEX_SECTION_CODE);
        off = align_up(off + static_cast<uint32_t>(text.size()), 0x1000);
        // .data
        if (!data.empty()) { write_u32(buf, off); write_u32(buf, data.size()); write_u32(buf, XEX_SECTION_DATA);
            off = align_up(off + static_cast<uint32_t>(data.size()), 0x1000);
        }
        // .bss
        if (!bss.empty()) { write_u32(buf, off); write_u32(buf, bss.size()); write_u32(buf, XEX_SECTION_BSS);
            off = align_up(off + static_cast<uint32_t>(bss.size()), 0x1000);
        }
        opts.emplace_back(XEX_HEADER_SECTION_TABLE, std::move(buf));
    }
    // IMPORT_LIBRARIES
    {
        std::vector<uint8_t> buf; write_u32(buf, importLibs.size());
        for (auto& [lib, funcs] : importLibs) {
            std::string upper = lib;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            write_utf16le(buf, upper);
            write_u32(buf, funcs.size());
            for (auto& fn : funcs) write_utf16le(buf, fn);
        }
        opts.emplace_back(XEX_HEADER_IMPORT_LIBRARIES, std::move(buf));
    }
    // EXPORT_LIBRARIES
    {
        std::vector<uint8_t> buf; write_u32(buf, exportLibs.size());
        for (auto& [lib, funcs] : exportLibs) {
            std::string upper = lib;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            write_utf16le(buf, upper);
            write_u32(buf, funcs.size());
            for (auto& fn : funcs) write_utf16le(buf, fn);
        }
        opts.emplace_back(XEX_HEADER_EXPORT_LIBRARIES, std::move(buf));
    }
    // Build header
    xex_out.clear(); write_u32(xex_out, 0x58455832); write_u32(xex_out, 2);
    size_t hdrpos = xex_out.size(); write_u32(xex_out, 0); write_u32(xex_out, 0);
    write_u32(xex_out, opts.size());
    for (auto& [key,val] : opts) { write_u32(xex_out, key); write_u32(xex_out, val.size()); xex_out.insert(xex_out.end(), val.begin(), val.end()); }
    write_u32(xex_out, 0);
    patch_u32(xex_out, hdrpos, static_cast<uint32_t>(xex_out.size()));
    // Append sections aligned
    write_padding(xex_out, 0x1000); xex_out.insert(xex_out.end(), text.begin(), text.end());
    write_padding(xex_out, 0x1000); xex_out.insert(xex_out.end(), data.begin(), data.end());
    write_padding(xex_out, 0x1000); xex_out.insert(xex_out.end(), bss.begin(), bss.end());
    write_padding(xex_out, 0x1000);
    return true;
}

} // namespace xexlib
