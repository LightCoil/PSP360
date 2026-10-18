#include "loader.h"

#include <cstdio>
#include <cstring>

namespace ppsspp {
namespace loader {

namespace {

// ---- PBP ----
// Смещения полей заголовка PBP
constexpr size_t kPbpMagicOffset      = 0x00;
constexpr size_t kPbpVersionOffset    = 0x04;
constexpr size_t kPbpOffsetsArray     = 0x08;  // 8 x uint32
constexpr size_t kPbpDataPspSizeField = 0x28;
constexpr size_t kPbpHeaderMinSize    = 0x2C;
constexpr uint32_t kPbpVersion100     = 0x00010000u;
constexpr uint32_t kPbpVersion200     = 0x00020000u;

constexpr int kPbpIndexDataPsp = 6;
constexpr int kPbpIndexDataPsar = 7;

// Индексы секций в массиве смещений PBP
constexpr int kPbpIndexParamSfo = 0;

uint32_t ReadLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint32_t>(p[0]) |
                                 (static_cast<uint32_t>(p[1]) << 8));
}

uint32_t ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// ---- ELF ----
constexpr size_t kElfHeaderSize = 52;
constexpr size_t kPhdrSize      = 32;

constexpr uint8_t kElfClass32    = 1;
constexpr uint8_t kElfData2LSB   = 1;   // PSP: little-endian
constexpr uint16_t kEmMips       = 8;
constexpr uint16_t kEtExec       = 2;
constexpr uint16_t kEtDyn        = 3;
constexpr uint32_t kPtLoad       = 1;

// Чтение little-endian 16/32
uint32_t ReadELF32(const uint8_t* p) { return ReadLE32(p); }
uint16_t ReadELF16(const uint8_t* p) { return ReadLE16(p); }

// Разбор строки PARAM.SFO (формат: длина, тип, затем значение).
std::string ParseSfoTitleId(const uint8_t* sfo, uint32_t size) {
    if (!sfo || size < 20) return std::string();

    const uint32_t keyTableStart = ReadBE32(sfo + 8);
    const uint32_t dataTableStart = ReadLE32(sfo + 0x14);

    if (keyTableStart + 8 > size || dataTableStart > size) return std::string();

    // Заголовок записи: длина, тип(1), максимальная длина(1), длина значения(1)
    const uint32_t keyLen  = ReadLE16(sfo + keyTableStart + 0x02);
    const uint32_t valMax  = sfo[keyTableStart + 0x04];
    const uint32_t valLen  = sfo[keyTableStart + 0x05];
    const uint32_t valOff  = dataTableStart + ReadLE16(sfo + keyTableStart + 0x06);

    const char* key = reinterpret_cast<const char*>(sfo + keyTableStart + 8);
    const uint32_t keyMax = sfo[keyTableStart + 0x03];
    if (keyLen > keyMax) return std::string();

    // Ищем запись TITLE_ID
    if (std::strncmp(key, "TITLE_ID", 8) == 0) {
        if (valLen == 0 || valMax == 0) return std::string();
        if (valOff + valLen > size) return std::string();
        const char* v = reinterpret_cast<const char*>(sfo + valOff);
        return std::string(v, strnlen(v, valLen));
    }

    return std::string();
}

} // namespace

const char* StatusName(LoadStatus s) {
    switch (s) {
    case LoadStatus::Ok:                   return "Ok";
    case LoadStatus::FileNotFound:         return "FileNotFound";
    case LoadStatus::BadPbpMagic:          return "BadPbpMagic";
    case LoadStatus::BadPbpVersion:        return "BadPbpVersion";
    case LoadStatus::PbpTruncated:         return "PbpTruncated";
    case LoadStatus::EmptyDataPsp:         return "EmptyDataPsp";
    case LoadStatus::BadElfMagic:          return "BadElfMagic";
    case LoadStatus::BadElfClass:          return "BadElfClass";
    case LoadStatus::BadElfEndian:         return "BadElfEndian";
    case LoadStatus::BadElfMachine:        return "BadElfMachine";
    case LoadStatus::NoLoadableSegments:   return "NoLoadableSegments";
    case LoadStatus::OutOfBounds:          return "OutOfBounds";
    default:                               return "Unknown";
    }
}

LoadStatus ExtractFromPBP(const uint8_t* pbp, size_t size,
                          std::vector<uint8_t>& outELF,
                          uint32_t& outTitleID) {
    if (!pbp || size < kPbpHeaderMinSize) return LoadStatus::PbpTruncated;

    if (std::memcmp(pbp + kPbpMagicOffset, "\0PBP", 4) != 0)
        return LoadStatus::BadPbpMagic;

    const uint32_t version = ReadLE32(pbp + kPbpVersionOffset);
    if (version != kPbpVersion100 && version != kPbpVersion200)
        return LoadStatus::BadPbpVersion;

    const uint32_t dataPspOffset = ReadLE32(pbp + kPbpOffsetsArray + kPbpIndexDataPsp * 4);

    if (version == kPbpVersion100) {
        // Размер data.psp хранится в заголовке.
        const uint32_t dataPspSize = ReadLE32(pbp + kPbpDataPspSizeField);
        if (dataPspSize == 0) return LoadStatus::EmptyDataPsp;
        if (dataPspOffset == 0 || dataPspOffset >= size)
            return LoadStatus::OutOfBounds;
        if (dataPspOffset + dataPspSize > size)
            return LoadStatus::PbpTruncated;

        outELF.assign(pbp + dataPspOffset, pbp + dataPspOffset + dataPspSize);
    } else {
        // В версии 0x200 размер определяется по началу следующей секции.
        uint32_t nextOffset = 0;
        for (int i = kPbpIndexDataPsp + 1; i < 8; ++i) {
            const uint32_t off = ReadLE32(pbp + kPbpOffsetsArray + i * 4);
            if (off != 0 && (nextOffset == 0 || off < nextOffset)) nextOffset = off;
        }
        if (nextOffset == 0) return LoadStatus::PbpTruncated;
        if (dataPspOffset == 0 || dataPspOffset >= size)
            return LoadStatus::OutOfBounds;
        if (dataPspOffset >= nextOffset) return LoadStatus::OutOfBounds;

        outELF.assign(pbp + dataPspOffset, pbp + nextOffset);
    }

    if (outELF.empty()) return LoadStatus::EmptyDataPsp;

    // TitleID из PARAM.SFO (не обязателен).
    outTitleID = 0;
    const uint32_t sfoOffset = ReadLE32(pbp + kPbpOffsetsArray + kPbpIndexParamSfo * 4);
    if (sfoOffset != 0 && sfoOffset + 20 <= size) {
        const uint32_t available = static_cast<uint32_t>(size - sfoOffset);
        const std::string title = ParseSfoTitleId(pbp + sfoOffset, available);
        if (!title.empty()) {
            char buf[16] = {0};
            std::memcpy(buf, title.c_str(), title.size() < 15 ? title.size() : 15);
            outTitleID = ReadLE32(reinterpret_cast<const uint8_t*>(buf));
        }
    }

    return LoadStatus::Ok;
}

LoadStatus LoadELF(const uint8_t* data, size_t size, Image& out) {
    if (!data || size < kElfHeaderSize) return LoadStatus::BadElfMagic;

    if (!(data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F'))
        return LoadStatus::BadElfMagic;

    if (data[4] != kElfClass32) return LoadStatus::BadElfClass;
    if (data[5] != kElfData2LSB) return LoadStatus::BadElfEndian;

    // Сохраняем образ — сегменты ссылаются на него по смещениям.
    out.elf.assign(data, data + size);

    const uint16_t machine = ReadELF16(data + 18);
    if (machine != kEmMips) return LoadStatus::BadElfMachine;

    const uint16_t eType     = ReadELF16(data + 16);
    const uint32_t entry     = ReadELF32(data + 24);
    const uint32_t phoff     = ReadELF32(data + 28);
    const uint16_t phentsize = ReadELF16(data + 42);
    const uint16_t phnum     = ReadELF16(data + 44);

    if (phentsize != kPhdrSize) return LoadStatus::NoLoadableSegments;
    if (phoff == 0 || phnum == 0) return LoadStatus::NoLoadableSegments;
    if (phoff + static_cast<size_t>(phnum) * phentsize > size)
        return LoadStatus::OutOfBounds;

    out.segments.clear();
    out.entryPoints.clear();

    for (uint16_t i = 0; i < phnum; ++i) {
        const uint8_t* ph = data + phoff + i * phentsize;
        const uint32_t pType = ReadELF32(ph + 0);

        if (pType != kPtLoad) continue;

        Segment seg;
        seg.offset = ReadELF32(ph + 4);
        seg.vaddr  = ReadELF32(ph + 8);
        seg.memsz  = ReadELF32(ph + 20);
        seg.filesz = ReadELF32(ph + 16);
        seg.flags  = ReadELF32(ph + 24);

        if (seg.offset + seg.filesz > size) return LoadStatus::OutOfBounds;

        // Сегменты .bss (filesz == 0) тоже нужны — они обнуляются.
        out.segments.push_back(seg);
    }

    if (out.segments.empty()) return LoadStatus::NoLoadableSegments;

    if (eType == kEtExec || eType == kEtDyn) {
        if (entry != 0) out.entryPoints.push_back(entry);
    }
    if (out.entryPoints.empty()) {
        // Если тип нестандартный, берём первый загружаемый адрес как вход.
        out.entryPoints.push_back(out.segments.front().vaddr);
    }

    return LoadStatus::Ok;
}

LoadStatus LoadPBPFromMemory(const uint8_t* data, size_t size, Image& out) {
    out.segments.clear();
    out.entryPoints.clear();
    out.elf.clear();

    std::vector<uint8_t> elf;
    uint32_t titleId = 0;
    LoadStatus st = ExtractFromPBP(data, size, elf, titleId);
    if (st != LoadStatus::Ok) return st;

    out.elf = std::move(elf);
    out.titleId = titleId;
    out.entryPoints.clear();

    st = LoadELF(out.elf.data(), out.elf.size(), out);
    if (st != LoadStatus::Ok) return st;

    return LoadStatus::Ok;
}

LoadStatus LoadPBP(const std::string& path, Image& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return LoadStatus::FileNotFound;

    std::fseek(f, 0, SEEK_END);
    const long fileSize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (fileSize <= 0) {
        std::fclose(f);
        return LoadStatus::PbpTruncated;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    const size_t read = std::fread(buffer.data(), 1, buffer.size(), f);
    std::fclose(f);

    if (read != buffer.size()) return LoadStatus::PbpTruncated;

    return LoadPBPFromMemory(buffer.data(), buffer.size(), out);
}

LoadStatus LoadIntoMemory(const Image& img, IMemorySink& sink) {
    if (!img.IsValid()) return LoadStatus::BadElfMagic;

    for (const Segment& seg : img.segments) {
        if (seg.filesz > 0) {
            sink.WriteBytes(seg.vaddr, img.elf.data() + seg.offset, seg.filesz);
        }
        // Хвост сегмента (memsz > filesz) — это .bss, заполняется нулями
        // вызывающей стороной (она же владеет очисткой памяти).
    }
    return LoadStatus::Ok;
}

} // namespace loader
} // namespace ppsspp