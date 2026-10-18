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

// Саб-операции VFPU (поле fmt — биты 5..0 инструкции).
enum VfpuFmt : uint8_t {
    VFMT_VMOVE = 0x00,  // vD = vS
    VFMT_VNEG  = 0x01,  // vD = -vS
    VFMT_VABS  = 0x02,  // vD = |vS|
    VFMT_VADD  = 0x03,  // vD = vS + vT
    VFMT_VSUB  = 0x04,  // vD = vS - vT
    VFMT_VMUL  = 0x06,  // vD = vS * vT
    VFMT_VDIV  = 0x07,  // vD = vS / vT
    VFMT_VMIN  = 0x08,
    VFMT_VMAX  = 0x09,
};

// Операции, работающие сразу со всеми четырьмя lane (формат .s4).
// В данной реализации поддерживается только работа с lane 0 (формат .s1),
// о чём сообщается в лог при первом обращении.
class VFPUHandler {
public:
    // Выполняет инструкцию VFPU. Возвращает false, если операция неизвестна.
    static bool Execute(core::CPUState& st, const core::Decoded& d);

    // Счётчик неизвестных операций (диагностика полноты реализации).
    static uint64_t unsupportedCount();
    static void ResetStats();
};

} // namespace vfpu
} // namespace ppsspp