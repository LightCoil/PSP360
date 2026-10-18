#include "vfpu_handler.h"
#include "../core/logger.h"

#include <cmath>

namespace ppsspp {
namespace vfpu {

namespace {
uint64_t g_unsupported = 0;
bool     g_warnedS4    = false;
} // namespace

uint64_t VFPUHandler::unsupportedCount() { return g_unsupported; }
void     VFPUHandler::ResetStats() { g_unsupported = 0; g_warnedS4 = false; }

bool VFPUHandler::Execute(core::CPUState& st, const core::Decoded& d) {
    // На PSP: opcode 0x12 (COP2), источник — биты 20..16, приёмник — биты 11..6.
    const uint32_t src = d.vs & 127u;
    const uint32_t dst = d.vt & 127u;
    const uint32_t src2 = d.rt & 127u;   // второй операнд для двухаргументных операций

    auto lane0 = [&](uint32_t reg) -> float { return st.vpr[reg][0]; };

    switch (d.fmt) {
    case VFMT_VMOVE:
        st.vpr[dst][0] = lane0(src);
        return true;

    case VFMT_VNEG:
        st.vpr[dst][0] = -lane0(src);
        return true;

    case VFMT_VABS:
        st.vpr[dst][0] = std::fabs(lane0(src));
        return true;

    case VFMT_VADD:
        st.vpr[dst][0] = lane0(src) + lane0(src2);
        return true;

    case VFMT_VSUB:
        st.vpr[dst][0] = lane0(src) - lane0(src2);
        return true;

    case VFMT_VMUL:
        st.vpr[dst][0] = lane0(src) * lane0(src2);
        return true;

    case VFMT_VDIV:
        // Деление на ноль в IEEE-754 даёт inf — это корректное поведение.
        st.vpr[dst][0] = lane0(src) / lane0(src2);
        return true;

    case VFMT_VMIN:
        st.vpr[dst][0] = (lane0(src) < lane0(src2)) ? lane0(src) : lane0(src2);
        return true;

    case VFMT_VMAX:
        st.vpr[dst][0] = (lane0(src) > lane0(src2)) ? lane0(src) : lane0(src2);
        return true;

    default:
        ++g_unsupported;
        LOG_DEBUG("VFPU: неизвестная операция fmt=0x%02X в PC=0x%08X (word=0x%08X)",
                  d.fmt, d.pc, d.word);
        return false;
    }
}

} // namespace vfpu
} // namespace ppsspp