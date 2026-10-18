// interp/ops_mem.cpp — загрузка и сохранение данных
#include "interpreter.h"
#include "../core/logger.h"

namespace ppsspp {
namespace interp {

using core::Decoded;

bool Interpreter::ExecMemory(Decoded& d, StepResult& r) {
    core::CPUState& cpu = cpu_;
    const int32_t simm = static_cast<int16_t>(d.imm);

    // Загрузка
    switch (d.opcode) {
    case core::Op::LB: {
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const int8_t value = static_cast<int8_t>(mem_.Read8(addr));
        if (d.rt != 0) cpu.SetReg(d.rt,static_cast<uint32_t>(
            static_cast<int32_t>(value)));
        return true;
    }
    case core::Op::LBU: {
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        if (d.rt != 0) cpu.SetReg(d.rt,mem_.Read8(addr));
        return true;
    }
    case core::Op::LH: {
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const int16_t value = static_cast<int16_t>(mem_.Read16(addr));
        if (d.rt != 0) cpu.SetReg(d.rt,static_cast<uint32_t>(
            static_cast<int32_t>(value)));
        return true;
    }
    case core::Op::LHU: {
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        if (d.rt != 0) cpu.SetReg(d.rt,mem_.Read16(addr));
        return true;
    }
    case core::Op::LW: {
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const uint32_t value = mem_.Read32(addr);
        if (mem_.LastFault() != core::MemFault::None) {
            LOG_ERROR("LW: ошибка доступа по адресу 0x%08X", addr);
            r = StepResult::BusError;
            return true;
        }
        if (d.rt != 0) cpu.SetReg(d.rt,value);
        return true;
    }
    // LWL/LWR — загрузка невыровненного слова. На PSP не используются
    // (операции выровнены), реализованы для полноты.
    case core::Op::LWL: {
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const uint32_t shift = (addr & 3u) * 8u;
        uint32_t value = cpu.Reg(d.rt);
        const uint32_t loaded = mem_.Read32(addr & ~3u);
        const uint32_t mask = (shift == 0) ? 0xFFFFFFFFu : ~(0xFFFFFFFFu << shift);
        value = (value & mask) | (loaded << shift);
        if (d.rt != 0) cpu.SetReg(d.rt,value);
        return true;
    }
    case core::Op::LWR: {
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const uint32_t shift = (3u - (addr & 3u)) * 8u;
        uint32_t value = cpu.Reg(d.rt);
        const uint32_t loaded = mem_.Read32(addr & ~3u);
        const uint32_t mask = (shift == 0) ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> shift);
        value = (value & mask) | (loaded >> shift);
        if (d.rt != 0) cpu.SetReg(d.rt,value);
        return true;
    }

    // Сохранение
    case core::Op::SB:
        mem_.Write8(cpu.Reg(d.rs) + static_cast<uint32_t>(simm),
                    static_cast<uint8_t>(cpu.Reg(d.rt)));
        return true;
    case core::Op::SH:
        mem_.Write16(cpu.Reg(d.rs) + static_cast<uint32_t>(simm),
                     static_cast<uint16_t>(cpu.Reg(d.rt)));
        return true;
    case core::Op::SW: {
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        mem_.Write32(addr, cpu.Reg(d.rt));
        if (mem_.LastFault() != core::MemFault::None) {
            LOG_ERROR("SW: ошибка доступа по адресу 0x%08X", addr);
            r = StepResult::BusError;
        }
        return true;
    }
    case core::Op::SWL: {
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t base = addr & ~3u;
        uint32_t value = mem_.Read32(base);
        const uint32_t mask = (shift == 0) ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> shift);
        value = (value & mask) | (cpu.Reg(d.rt) >> shift);
        mem_.Write32(base, value);
        return true;
    }
    case core::Op::SWR: {
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const uint32_t shift = (3u - (addr & 3u)) * 8u;
        const uint32_t base = addr & ~3u;
        uint32_t value = mem_.Read32(base);
        const uint32_t mask = (shift == 0) ? 0xFFFFFFFFu : ~(0xFFFFFFFFu << shift);
        value = (value & mask) | (cpu.Reg(d.rt) << shift);
        mem_.Write32(base, value);
        return true;
    }
    case core::Op::PREF:
        return true;  // подсказка кэшу — no-op

    default:
        return false;
    }
}

} // namespace interp
} // namespace ppsspp