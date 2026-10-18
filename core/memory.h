// core/memory.h — подсистема памяти PSP
//
// ВНИМАНИЕ: в предыдущих версиях проекта (D:\PSP360\core\memory.cpp)
// ram_/vram_ присваивались фиктивные адреса 0xEC000000/0xEC800000 без
// выделения памяти, а Reset() делал memset по неинициализированному
// указателю. Здесь — настоящее выделение, проверки границ и отображение
// адресного пространства PSP.
//
// ПОРЯДОК БАЙТОВ: little-endian. Allegrex работает в LE-режиме (Config.LEL),
// ELF-образы PSP — MIPSEL, поэтому и данные, и выборка инструкций
// little-endian. Сборка приведением указателя к uint16_t*/uint32_t* намеренно
// не используется: она зависит от платформы, а код должен работать и на
// хосте, и на PowerPC-хосте Xbox 360.
#pragma once

#include <cstdint>
#include <cstddef>

namespace ppsspp {
namespace core {

// Размеры областей памяти PSP.
constexpr size_t PSP_RAM_SIZE     = 32u * 1024u * 1024u; // 32 МБ
constexpr size_t PSP_VRAM_SIZE    =  2u * 1024u * 1024u; //  2 МБ
constexpr size_t PSP_SCRATCH_SIZE = 16u * 1024u;         // 16 КБ

// Коды ошибок доступа.
enum class MemFault : uint32_t {
    None = 0,
    Unmapped,
    ReadOnly,
    Misaligned,
};

class Memory {
public:
    Memory();
    ~Memory();

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    // Инициализация (повторный вызов очищает память).
    void Init();

    void Shutdown();
    bool IsInitialized() const { return initialized_; }

    // ---- чтение ----
    uint8_t  Read8(uint32_t addr) const;
    uint16_t Read16(uint32_t addr) const;
    uint32_t Read32(uint32_t addr) const;

    // ---- запись ----
    void Write8(uint32_t addr, uint8_t value);
    void Write16(uint32_t addr, uint16_t value);
    void Write32(uint32_t addr, uint32_t value);

    // Блочные операции (с автоматической обрезкой по границам области).
    void WriteBlock(uint32_t addr, const void* src, size_t size);
    void ReadBlock(uint32_t addr, void* dst, size_t size) const;

    // Прямой указатель на память по адресу PSP. nullptr, если адрес не отображён.
    uint8_t* GetPointer(uint32_t addr);
    const uint8_t* GetPointer(uint32_t addr) const;

    // Диагностика последней ошибки доступа.
    MemFault LastFault() const { return lastFault_; }
    uint32_t LastFaultAddress() const { return lastFaultAddr_; }

    // Счётчики (для профилирования и тестов).
    uint64_t readOps() const { return readOps_; }
    uint64_t writeOps() const { return writeOps_; }

private:
    // Разрешает адрес в указатель + возвращает оставшийся размер.
    uint8_t* Resolve(uint32_t addr, size_t accessSize, MemFault* fault);
    const uint8_t* Resolve(uint32_t addr, size_t accessSize, MemFault* fault) const;

    uint8_t* ram_;
    uint8_t* vram_;
    uint8_t* scratch_;

    bool initialized_;
    MemFault lastFault_;
    uint32_t lastFaultAddr_;
    // mutable: счётчики обновляются и в константных методах чтения.
    mutable uint64_t readOps_;
    uint64_t writeOps_;
};

} // namespace core
} // namespace ppsspp