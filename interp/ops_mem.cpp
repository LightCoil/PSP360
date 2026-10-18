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
        // Эталон — QEMU gen_lxl (target/mips/tcg/translate.c):
        //   t1 = (addr & 3) ^ 3;  shift = t1 * 8;      // little-endian
        //   rt = (memword << shift) | (rt & ~(~0u << shift))
        // Побайтно (W = addr & ~3, n = addr & 3, b[k] = байт по адресу W+k):
        //   rt[3 - n + p] = b[p]   для p = 0..n
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const uint32_t base = addr & ~3u;
        const uint32_t n    = addr & 3u;
        uint32_t value = cpu.Reg(d.rt);
        for (uint32_t p = 0; p <= n; ++p) {
            const uint32_t dstShift = 8u * (3u - n + p);
            value = (value & ~(0xFFu << dstShift)) |
                    (mem_.Read8(base + p) << dstShift);
        }
        if (d.rt != 0) cpu.SetReg(d.rt, value);
        return true;
    }
    case core::Op::LWR: {
        // LWR: rt[k-n] = b[k]  для k = n..3
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const uint32_t base = addr & ~3u;
        const uint32_t n    = addr & 3u;
        uint32_t value = cpu.Reg(d.rt);
        for (uint32_t k = n; k <= 3u; ++k) {
            const uint32_t shift = 8u * (k - n);
            value = (value & ~(0xFFu << shift)) | (mem_.Read8(base + k) << shift);
        }
        if (d.rt != 0) cpu.SetReg(d.rt, value);
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
        // Обратная операция к LWL (та же раскладка байтов):
        //   b[p] = rt[3 - n + p]   для p = 0..n
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const uint32_t base = addr & ~3u;
        const uint32_t n    = addr & 3u;
        const uint32_t src  = cpu.Reg(d.rt);
        for (uint32_t p = 0; p <= n; ++p) {
            const uint32_t srcShift = 8u * (3u - n + p);
            mem_.Write8(base + p,
                        static_cast<uint8_t>((src >> srcShift) & 0xFFu));
        }
        return true;
    }
    case core::Op::SWR: {
        // Обратная операция к LWR: b[k] = rt[k-n]  для k = n..3
        const uint32_t addr = cpu.Reg(d.rs) + static_cast<uint32_t>(simm);
        const uint32_t base = addr & ~3u;
        const uint32_t n    = addr & 3u;
        const uint32_t src  = cpu.Reg(d.rt);
        for (uint32_t k = n; k <= 3u; ++k) {
            const uint32_t shift = 8u * (k - n);
            mem_.Write8(base + k, static_cast<uint8_t>((src >> shift) & 0xFFu));
        }
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