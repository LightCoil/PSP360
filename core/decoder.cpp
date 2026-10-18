#include "decoder.h"

namespace ppsspp {
namespace core {

Decoded::Decoded()
    : pc(0), word(0), opcode(0), rs(0), rt(0), rd(0), shamt(0), funct(0),
      imm(0), target(0), cop(COPNONE),
      copRs(0), copOp(0), copCode(0), copKind(COP0_UNKNOWN),
      vfpuVd(0), vfpuVs(0), vfpuVt(0), vfpuSize(0), vfpuFunc(0),
      isVfpu(false),
      vs(0), vt(0), fmt(0),
      isBranch(false), branchTarget(0) {}

Decoded Decode(uint32_t pc, uint32_t word) {
    Decoded d;
    d.pc = pc;
    d.word = word;

    d.opcode = static_cast<uint8_t>((word >> 26) & 0x3F);
    d.rs     = static_cast<uint8_t>((word >> 21) & 0x1F);
    d.rt     = static_cast<uint8_t>((word >> 16) & 0x1F);
    d.rd     = static_cast<uint8_t>((word >> 11) & 0x1F);
    d.shamt  = static_cast<uint8_t>((word >> 6)  & 0x1F);
    d.funct  = static_cast<uint8_t>(word & 0x3F);
    d.imm    = static_cast<uint16_t>(word & 0xFFFF);

    // Знаковое расширение непосредственного значения
    const int32_t simm = static_cast<int16_t>(d.imm);

    // Ветвление: адрес = PC + 4 + (simm << 2)
    d.branchTarget = pc + 4 + static_cast<uint32_t>(static_cast<int32_t>(simm << 2));

    // Переход: старшие 4 бита берутся из PC+4
    d.target = ((pc + 4) & 0xF0000000u) | ((word & 0x03FFFFFFu) << 2);

    switch (d.opcode) {
    case Op::SPECIAL:
        d.cop = COPNONE;
        // JR / JALR — тоже переходы
        d.isBranch = (d.funct == Funct::JR || d.funct == Funct::JALR);
        d.branchTarget = 0; // вычисляется в момент выполнения
        break;

    case Op::BEQ:
    case Op::BNE:
    case Op::BLEZ:
    case Op::BGTZ:
        d.cop = COPNONE;
        d.isBranch = true;
        break;

    case Op::J:
    case Op::JAL:
        d.cop = COPNONE;
        d.isBranch = true;
        d.branchTarget = d.target;
        break;

    case Op::REGIMM:
        d.cop = COPNONE;
        d.isBranch = true;
        break;

    case Op::COP0: {
        // COP0: биты 25..21 — селектор (PPSPP), биты 4..0 — поле MIPS32,
        // биты 10..3 зарезервированы и обязаны быть нулевыми.
        d.cop     = COP0;
        d.copRs   = static_cast<uint8_t>((word >> 21) & 0x1Fu);
        d.copOp   = static_cast<uint8_t>(word & 0x1Fu);
        d.copCode = static_cast<uint8_t>(word & 0x07u);

        d.copKind = COP0_UNKNOWN;
        // Зарезервированное поле COP0 — биты 10..3 (между rd и op).
        const bool reservedZero = (word & COP0_RESERVED_MASK) == 0;
        // Бит 25 (CO) = 1 -> сопроцессор 1: eret/wait/tlb*, не регистры CPU.
        const bool cpuRegs = (word & COP0_CO_BIT) == 0;

        if (reservedZero && cpuRegs) {
            const uint8_t low = d.copOp;
            // Поле op занимает биты 4..0 целиком: MF=0, CF=2, MT=4, CT=6.
            // Значения 8..15 (бит 3 установлен) — это cache, а не cfc0,
            // поэтому отдельной проверки подфункции не требуется.

            // Схема PPSPP: селектор в битах 25..21 (Int_Cop0:
            // switch ((op >> 21) & 0x1f) { case 0: mfc0  case 4: mtc0 }).
            if (d.copRs == CopRs::MFC0 && low == CopField::MF) {
                d.copKind = COP0_MFC0;
            } else if (d.copRs == CopRs::MTC0 &&
                       (low == CopField::MF || low == CopField::MT)) {
                // Принимаются оба варианта записи младших бит: PPSPP их
                // не проверяет, MIPS32 требует MT (4).
                d.copKind = COP0_MTC0;
            } else if (d.copRs == CopRs::MFC0 && low == CopField::MT) {
                // Схема MIPS32 Vol. III: rs = 0, вид операции в битах 4..0.
                d.copKind = COP0_MTC0;
            } else if (d.copRs == CopRs::MFC0 && low == CopField::CF) {
                d.copKind = COP0_CFC0;
            } else if (d.copRs == CopRs::MFC0 && low == CopField::CT) {
                d.copKind = COP0_CTC0;
            }
        }
        break;
    }
    case Op::COP1: d.cop = COP1; break;
    case Op::COP2:
        // Перенос регистров VFPU: lv.s, sv.s, mftv, cfti ...
        // Здесь действует обычная раскладка MIPS.
        d.cop = COP2;
        d.vt  = static_cast<uint8_t>((word >> 16) & 0x1F);   // биты 20..16
        d.vs  = static_cast<uint8_t>((word >> 11) & 0x1F);   // биты 15..11
        d.fmt = d.funct;
        break;
    case Op::COP3: d.cop = COP3; break;

    // ---- VFPU ----------------------------------------------------------
    // Раскладка полей по PPSSPP (см. docs/ARCHITECTURE_NOTES.md):
    //   приёмник   — биты 6..0
    //   источник 1 — биты 14..8
    //   источник 2 — биты 22..16
    //   размер     — биты 15 и 7
    // Под-индекс операции берётся по опкоду (MIPSTables.cpp):
    //   0x18/0x19/0x1B — биты 25..23,  0x34 — биты 20..16.
    case Op::VFPU_A:
    case Op::VFPU_B:
    case Op::VFPU_C:
        d.cop = COP2;
        d.isVfpu  = true;
        d.vfpuVd  = static_cast<uint8_t>(word & 0x7Fu);
        d.vfpuVs  = static_cast<uint8_t>((word >> 8) & 0x7Fu);
        d.vfpuVt  = static_cast<uint8_t>((word >> 16) & 0x7Fu);
        d.vfpuSize = static_cast<uint8_t>((((word >> 15) & 1u) << 1) |
                                           ((word >> 7) & 1u));
        d.vfpuFunc = static_cast<uint8_t>((word >> 23) & 0x7u);
        break;

    case Op::VFPU_D:
        d.cop = COP2;
        d.isVfpu  = true;
        d.vfpuVd  = static_cast<uint8_t>(word & 0x7Fu);
        d.vfpuVs  = static_cast<uint8_t>((word >> 8) & 0x7Fu);
        // Для 0x34 биты 22..21 выбирают таблицу; в используемой таблице
        // (биты 25..21 == 0) номер операции лежит в битах 20..16.
        d.vfpuVt  = static_cast<uint8_t>((word >> 16) & 0x7Fu);
        d.vfpuSize = static_cast<uint8_t>((((word >> 15) & 1u) << 1) |
                                           ((word >> 7) & 1u));
        d.vfpuFunc = static_cast<uint8_t>((word >> 16) & 0x1Fu);
        break;

    default:
        d.cop = COPNONE;
        break;
    }

    return d;
}

} // namespace core
} // namespace ppsspp