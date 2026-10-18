// Loader/loader.h — загрузка образов PSP: PBP -> ELF -> сегменты в память
//
// Логика восстановлена по Loader/loader.cpp из D:\PSP360, где была
// рабочая цепочка LoadPBP -> ExtractFromPBP -> LoadELF. Здесь убраны
// зависимости от std::filesystem/строк и добавлена строгая проверка границ.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace ppsspp {
namespace loader {

// Сегмент, готовый к загрузке в память.
struct Segment {
    uint32_t vaddr;
    uint32_t memsz;
    uint32_t filesz;
    uint32_t offset;   // смещение в буфере ELF
    uint32_t flags;    // 1=exec 2=write 4=read
};

// Результат разбора образа.
struct Image {
    std::vector<uint8_t>  elf;              // сырой ELF-файл
    std::vector<Segment>  segments;
    std::vector<uint32_t> entryPoints;      // точки входа (обычно одна)
    uint32_t              titleId   = 0;
    std::string           titleIdStr;
    uint32_t              sfoOffset = 0;
    uint32_t              sfoSize   = 0;

    bool IsValid() const { return !elf.empty() && !entryPoints.empty(); }
    uint32_t Entry() const { return entryPoints.empty() ? 0 : entryPoints.front(); }
};

// Коды ошибок.
enum class LoadStatus : int {
    Ok = 0,
    FileNotFound,
    BadPbpMagic,
    BadPbpVersion,
    PbpTruncated,
    EmptyDataPsp,
    BadElfMagic,
    BadElfClass,
    BadElfEndian,
    BadElfMachine,
    NoLoadableSegments,
    OutOfBounds,
};

const char* StatusName(LoadStatus s);

// Загрузка PBP из файла (заголовок читается целиком).
LoadStatus LoadPBP(const std::string& path, Image& out);

// Загрузка образа из уже прочитанного буфера PBP.
LoadStatus LoadPBPFromMemory(const uint8_t* data, size_t size, Image& out);

// Разбор PBP: достаёт секцию data.psp и заголовок PARAM.SFO.
LoadStatus ExtractFromPBP(const uint8_t* pbp, size_t size,
                          std::vector<uint8_t>& outELF,
                          uint32_t& outTitleID);

// Разбор ELF: заполняет segments и entryPoints.
LoadStatus LoadELF(const uint8_t* data, size_t size, Image& out);

// Загрузка всех PT_LOAD-сегментов образа в подсистему памяти.
// class Mem должен предоставлять Write32/Write8 по адресу PSP.
class IMemorySink {
public:
    virtual ~IMemorySink() = default;
    virtual void WriteBytes(uint32_t vaddr, const uint8_t* src, uint32_t size) = 0;
};

LoadStatus LoadIntoMemory(const Image& img, IMemorySink& sink);

} // namespace loader
} // namespace ppsspp