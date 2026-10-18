// vfpu/vfpu_handler.cpp — исполнение инструкций VFPU
//
// Раскладка полей и таблицы операций установлены по первоисточникам
// (см. docs/ARCHITECTURE_NOTES.md и tests/test_vfpu.cpp):
//   vd = op & 0x7F        приёмник,  биты 6..0
//   vs = (op>>8) & 0x7F   источник 1, биты 14..8
//   vt = (op>>16) & 0x7F  источник 2, биты 22..16
//   размер вектора — биты 15 и 7
//
// Номера операций пересекаются между опкодами (vadd и vmul оба равны 0),
// поэтому сначала определяется внутренняя операция по паре (опкод, функция).
#include "vfpu/vfpu_handler.h"
#include "../core/logger.h"

#include <cmath>

namespace ppsspp {
namespace vfpu {

namespace {
// Счётчики — глобальные, как и раньше: обработчик stateless, а состояние
// эмулятора (CPUState) остаётся в единственном экземпляре.
uint64_t g_unsupported   = 0;
uint64_t g_partialVector = 0;

// Внутреннее представление операции: после отображения пары
// (опкод, функция) в одну сущность.
enum class Op { Unknown, Mov, Abs, Neg, Add, Sub, Mul, Div, Min, Max };

constexpr int kLanes[4] = { 1, 2, 3, 4 };

Op MapOp(uint32_t opcode, uint8_t func) {
    switch (opcode) {
    case core::Op::VFPU_A:   // tableVFPU0[8], индекс = биты 25..23
        if (func == VfpuFunc::VADD) return Op::Add;
        if (func == VfpuFunc::VSUB) return Op::Sub;
        if (func == VfpuFunc::VDIV) return Op::Div;
        return Op::Unknown;

    case core::Op::VFPU_B:   // tableVFPU1[8]
        if (func == VfpuFunc::VMUL) return Op::Mul;
        return Op::Unknown;

    case core::Op::VFPU_C:   // tableVFPU3[8]
        if (func == VfpuFunc::VMIN) return Op::Min;
        if (func == VfpuFunc::VMAX) return Op::Max;
        return Op::Unknown;

    case core::Op::VFPU_D:   // tableVFPU4[32], индекс = биты 20..16
        if (func == VfpuFunc::VMOV) return Op::Mov;
        if (func == VfpuFunc::VABS) return Op::Abs;
        if (func == VfpuFunc::VNEG) return Op::Neg;
        return Op::Unknown;

    default:
        return Op::Unknown;
    }
}

bool IsBinary(Op op) {
    return op == Op::Add || op == Op::Sub || op == Op::Div ||
           op == Op::Mul || op == Op::Min  || op == Op::Max;
}
} // namespace

uint64_t VFPUHandler::unsupportedCount()   { return g_unsupported; }
uint64_t VFPUHandler::partialVectorCount() { return g_partialVector; }

bool VFPUHandler::Execute(core::CPUState& st, const core::Decoded& d) {
    if (!d.isVfpu) return false;

    const Op op = MapOp(d.opcode, d.vfpuFunc);
    if (op == Op::Unknown) {
        ++g_unsupported;
        LOG_DEBUG("VFPU: неизвестная операция opcode=0x%02X func=0x%02X "
                  "в PC=0x%08X (word=0x%08X)", d.opcode, d.vfpuFunc, d.pc, d.word);
        return false;
    }

    // Размер вектора .s/.p/.t/.q. Реализован только lane 0; для остальных
    // размеров результат неполон, и это фиксируется явно, а не молча.
    const int lanes = kLanes[d.vfpuSize & 3u];
    if (lanes > 1) {
        ++g_partialVector;
        LOG_DEBUG("VFPU: размер вектора %d lane в PC=0x%08X обрабатывается "
                  "частично (реализован только lane 0)", lanes, d.pc);
    }

    const uint32_t dst  = d.vfpuVd;
    const uint32_t src1 = d.vfpuVs;
    const uint32_t src2 = d.vfpuVt;

    const float a = st.vpr[src1][0];
    const float b = IsBinary(op) ? st.vpr[src2][0] : 0.0f;

    float r = 0.0f;
    switch (op) {
    case Op::Add: r = a + b; break;
    case Op::Sub: r = a - b; break;
    case Op::Mul: r = a * b; break;
    // Деление на ноль в IEEE-754 даёт inf — это корректное поведение.
    case Op::Div: r = a / b; break;
    case Op::Min: r = (a < b) ? a : b; break;
    case Op::Max: r = (a > b) ? a : b; break;
    case Op::Mov: r = a; break;
    case Op::Abs: r = std::fabs(a); break;
    case Op::Neg: r = -a; break;
    default: break;
    }

    st.vpr[dst][0] = r;
    return true;
}

} // namespace vfpu
} // namespace ppsspp