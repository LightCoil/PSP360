// Loader/memory_sink.h — адаптер подсистемы памяти под интерфейс загрузчика
#pragma once

#include "../core/memory.h"
#include "loader.h"

namespace ppsspp {
namespace loader {

// Записывает образ в подсистему памяти PSP.
class MemorySink : public IMemorySink {
public:
    explicit MemorySink(core::Memory& mem) : mem_(mem) {}

    void WriteBytes(uint32_t vaddr, const uint8_t* src, uint32_t size) override {
        if (!src || size == 0) return;
        // WriteBlock сама обрезает запись по границам области.
        mem_.WriteBlock(vaddr, src, size);
    }

private:
    core::Memory& mem_;
};

} // namespace loader
} // namespace ppsspp