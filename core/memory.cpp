#include "memory.h"

#include <cstdlib>
#include <cstring>

namespace ppsspp {
namespace core {

namespace {
constexpr uint32_t kRamBase     = 0x08800000u;
constexpr uint32_t kVramBase    = 0x04000000u;
constexpr uint32_t kScratchBase = 0x10000000u;

// Диапазоны, которые отображаются в реальную память.
// Всё остальное (I/O, MMIO, неразмеченное) даёт MemFault::Unmapped.
struct Region {
    uint32_t base;
    size_t   size;
};

constexpr Region kRegions[] = {
    { kRamBase,     PSP_RAM_SIZE     },
    { kVramBase,    PSP_VRAM_SIZE    },
    { kScratchBase, PSP_SCRATCH_SIZE },
};
} // namespace

Memory::Memory()
    : ram_(nullptr), vram_(nullptr), scratch_(nullptr),
      initialized_(false), lastFault_(MemFault::None),
      lastFaultAddr_(0), readOps_(0), writeOps_(0) {}

Memory::~Memory() {
    Shutdown();
}

void Memory::Init() {
    if (initialized_) {
        std::memset(ram_, 0, PSP_RAM_SIZE);
        std::memset(vram_, 0, PSP_VRAM_SIZE);
        std::memset(scratch_, 0, PSP_SCRATCH_SIZE);
        return;
    }

    ram_     = static_cast<uint8_t*>(std::calloc(PSP_RAM_SIZE, 1));
    vram_    = static_cast<uint8_t*>(std::calloc(PSP_VRAM_SIZE, 1));
    scratch_ = static_cast<uint8_t*>(std::calloc(PSP_SCRATCH_SIZE, 1));

    initialized_ = (ram_ != nullptr && vram_ != nullptr && scratch_ != nullptr);
    if (!initialized_) {
        // Откатываем частичное выделение, чтобы не течь памятью.
        std::free(ram_);
        std::free(vram_);
        std::free(scratch_);
        ram_ = vram_ = scratch_ = nullptr;
    }
}

void Memory::Shutdown() {
    std::free(ram_);
    std::free(vram_);
    std::free(scratch_);
    ram_ = vram_ = scratch_ = nullptr;
    initialized_ = false;
    readOps_ = writeOps_ = 0;
}

// ------------------------------------------------------------------ resolve

uint8_t* Memory::Resolve(uint32_t addr, size_t accessSize, MemFault* fault) {
    if (!initialized_) {
        *fault = MemFault::Unmapped;
        lastFault_ = *fault;
        lastFaultAddr_ = addr;
        return nullptr;
    }

    for (const Region& r : kRegions) {
        // Адрес вне диапазона этой области.
        if (addr < r.base || addr > r.base + r.size - 1) continue;

        const size_t offset = addr - r.base;

        // Запрещено пересечение границы области.
        if (offset + accessSize > r.size) {
            *fault = MemFault::Unmapped;
            lastFault_ = *fault;
            lastFaultAddr_ = addr;
            return nullptr;
        }

        uint8_t* regionBase = nullptr;
        if (r.base == kRamBase)          regionBase = ram_;
        else if (r.base == kVramBase)     regionBase = vram_;
        else                              regionBase = scratch_;

        *fault = MemFault::None;
        // Диагностика должна отражать ПОСЛЕДНЮЮ операцию: успешный доступ
        // обязан сбрасывать lastFault_. Иначе устаревшая ошибка от
        // неотображённого адреса вызывала бы ложные BusError в
        // интерпретаторе, который проверяет LastFault() после чтения.
        lastFault_ = MemFault::None;
        return regionBase + offset;
    }

    *fault = MemFault::Unmapped;
    lastFault_ = *fault;
    lastFaultAddr_ = addr;
    return nullptr;
}

const uint8_t* Memory::Resolve(uint32_t addr, size_t accessSize, MemFault* fault) const {
    return const_cast<Memory*>(this)->Resolve(addr, accessSize, fault);
}

uint8_t* Memory::GetPointer(uint32_t addr) {
    MemFault f = MemFault::None;
    return Resolve(addr, 1, &f);
}

const uint8_t* Memory::GetPointer(uint32_t addr) const {
    MemFault f = MemFault::None;
    return Resolve(addr, 1, &f);
}

// --------------------------------------------------------------------- чтение
//
// PSP (Allegrex) работает в little-endian режиме, поэтому младший байт
// слова лежит по наименьшему адресу. Порядок байтов задан явно, без сборки
// многобайтовых значений из указателя — это независимо от архитектуры хоста.

uint8_t Memory::Read8(uint32_t addr) const {
    ++readOps_;
    MemFault f;
    const uint8_t* p = Resolve(addr, 1, &f);
    return p ? *p : 0;
}

uint16_t Memory::Read16(uint32_t addr) const {
    ++readOps_;
    MemFault f;
    const uint8_t* p = Resolve(addr, 2, &f);
    if (!p) return 0;
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t Memory::Read32(uint32_t addr) const {
    ++readOps_;
    MemFault f;
    const uint8_t* p = Resolve(addr, 4, &f);
    if (!p) return 0;
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8)  |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// --------------------------------------------------------------------- запись

void Memory::Write8(uint32_t addr, uint8_t value) {
    ++writeOps_;
    MemFault f;
    uint8_t* p = Resolve(addr, 1, &f);
    if (p) *p = value;
}

void Memory::Write16(uint32_t addr, uint16_t value) {
    ++writeOps_;
    MemFault f;
    uint8_t* p = Resolve(addr, 2, &f);
    if (!p) return;
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void Memory::Write32(uint32_t addr, uint32_t value) {
    ++writeOps_;
    MemFault f;
    uint8_t* p = Resolve(addr, 4, &f);
    if (!p) return;
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

// ------------------------------------------------------------- блочные операции

void Memory::WriteBlock(uint32_t addr, const void* src, size_t size) {
    if (!src || size == 0) return;

    MemFault f;
    uint8_t* p = Resolve(addr, size, &f);
    if (p) {
        std::memcpy(p, src, size);
        writeOps_ += size;
        return;
    }

    // Обрезаем по границам области: частичная запись допустима.
    for (const Region& r : kRegions) {
        if (addr < r.base || addr > r.base + r.size - 1) continue;
        const size_t offset = addr - r.base;
        const size_t avail = r.size - offset;
        const size_t n = (size < avail) ? size : avail;
        uint8_t* regionBase = (r.base == kRamBase) ? ram_
                            : (r.base == kVramBase) ? vram_ : scratch_;
        std::memcpy(regionBase + offset, src, n);
        writeOps_ += n;
        return;
    }
}

void Memory::ReadBlock(uint32_t addr, void* dst, size_t size) const {
    if (!dst || size == 0) return;

    MemFault f;
    const uint8_t* p = Resolve(addr, size, &f);
    if (p) {
        std::memcpy(dst, p, size);
        readOps_ += size;
        return;
    }

    for (const Region& r : kRegions) {
        if (addr < r.base || addr > r.base + r.size - 1) continue;
        const size_t offset = addr - r.base;
        const size_t avail = r.size - offset;
        const size_t n = (size < avail) ? size : avail;
        const uint8_t* regionBase = (r.base == kRamBase) ? ram_
                                : (r.base == kVramBase) ? vram_ : scratch_;
        std::memcpy(dst, regionBase + offset, n);
        readOps_ += n;
        return;
    }
}

} // namespace core
} // namespace ppsspp