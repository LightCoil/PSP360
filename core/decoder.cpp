#include "decoder.h"

namespace ppsspp {
namespace core {

Decoded::Decoded()
    : pc(0), word(0), opcode(0), rs(0), rt(0), rd(0), shamt(0), funct(0),
      imm(0), target(0), cop(COPNONE), vs(0), vt(0), fmt(0),
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

    case Op::COP0: d.cop = COP0; break;
    case Op::COP1: d.cop = COP1; break;
    case Op::COP2:
        d.cop = COP2;
        // В COP2 биты 11..6 = vt (приёмник), 20..16 = vs (источник)
        d.vt  = static_cast<uint8_t>((word >> 6) & 0x1F);
        d.vs  = static_cast<uint8_t>((word >> 16) & 0x1F);
        d.fmt = d.funct;
        break;
    case Op::COP3: d.cop = COP3; break;

    default:
        d.cop = COPNONE;
        break;
    }

    return d;
}

} // namespace core
} // namespace ppsspp