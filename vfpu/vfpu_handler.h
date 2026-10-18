// vfpu/vfpu_handler.h — эмуляция VFPU (Vector Floating Point Unit) PSP
//
// Идея и базовая структура сохранены из снапшота 10.06.2025, где был
// рабочий обработчик MOV.s / NEG.s / ABS.s. Здесь набор операций расширен,
// а обработка ошибок переведена с assert() на журналирование.
#pragma once

#include "../core/cpu_state.h"
#include "../core/decoder.h"

namespace ppsspp {
namespace vfpu {

// Операции VFPU. ВАЖНО: это НЕ поле fmt инструкции.
//
// Allegrex разносит VFPU по нескольким опкодам MIPS, и номер операции
// выбирается под-индексом внутри опкода (PPSSPP, MIPSTables.cpp):
//
//   tableVFPU0[8]  opcode 0x18, индекс = биты 25..23
//     0 = vadd,  1 = vsub,  2 = vsbn,  7 = vdiv
//   tableVFPU1[8]  opcode 0x19, индекс = биты 25..23
//     0 = vmul,  1 = vdot,  2 = vscl,  4 = vhdp, 5 = vcrs, 6 = vdet
//   tableVFPU3[8]  opcode 0x1B, индекс = биты 25..23
//     0 = vcmp,  2 = vmin,  3 = vmax,  5 = vscmp, 6 = vsge, 7 = vslt
//   tableVFPU4[32] opcode 0x34 (при битах 25..21 == 0), индекс = биты 20..16
//     0 = vmov,  1 = vabs,  2 = vneg,  3 = vidt,  4 = vsat0,
//     5 = vsat1, 6 = vzero, 7 = vone, 16 = vrcp, 17 = vrsq,
//     18 = vsin, 19 = vcos, 20 = vexp2, 21 = vlog2, 22 = vsqrt,
//     23 = vasin, 24 = vnrcp, 26 = vnsin, 27 = vrexp2
namespace VfpuFunc {
constexpr uint8_t VADD  = 0x00;  // 0x18
constexpr uint8_t VSUB  = 0x01;  // 0x18
constexpr uint8_t VDIV  = 0x07;  // 0x18
constexpr uint8_t VMUL  = 0x00;  // 0x19
constexpr uint8_t VMIN  = 0x02;  // 0x1B
constexpr uint8_t VMAX  = 0x03;  // 0x1B
constexpr uint8_t VMOV  = 0x00;  // 0x34
constexpr uint8_t VABS  = 0x01;  // 0x34
constexpr uint8_t VNEG  = 0x02;  // 0x34
} // namespace VfpuFunc

// Операции, работающие сразу со всеми четырьмя lane (формат .s4).
// В данной реализации поддерживается только работа с lane 0 (формат .s1),
// о чём сообщается в лог при первом обращении.
class VFPUHandler {
public:
    // Выполняет инструкцию VFPU. Возвращает false, если операция неизвестна.
    static bool Execute(core::CPUState& st, const core::Decoded& d);

    // Счётчик неизвестных операций (диагностика полноты реализации).
    static uint64_t unsupportedCount();
    // Счётчик операций с размером вектора .p/.t/.q: они обрабатываются
    // частично (только lane 0), и это должно быть видно, а не скрыто.
    static uint64_t partialVectorCount();
};

} // namespace vfpu
} // namespace ppsspp