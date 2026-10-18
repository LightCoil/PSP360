// core/decoder.h — декодирование инструкций MIPS32 (PSP Allegrex)
#pragma once

#include <cstdint>

namespace ppsspp {
namespace core {

// Код сопроцессора.
enum Coprocessor : uint8_t {
    COPNONE = 0,
    COP0    = 1,
    COP1    = 2,
    COP2    = 3,  // VFPU на PSP
    COP3    = 4,
};

// Основные поля опкодов MIPS32.
namespace Op {
constexpr uint8_t SPECIAL = 0x00;
constexpr uint8_t REGIMM  = 0x01;
constexpr uint8_t J       = 0x02;
constexpr uint8_t JAL     = 0x03;
constexpr uint8_t BEQ     = 0x04;
constexpr uint8_t BNE     = 0x05;
constexpr uint8_t BLEZ    = 0x06;
constexpr uint8_t BGTZ    = 0x07;
constexpr uint8_t ADDI    = 0x08;
constexpr uint8_t ADDIU   = 0x09;
constexpr uint8_t SLTI    = 0x0A;
constexpr uint8_t SLTIU   = 0x0B;
constexpr uint8_t ANDI    = 0x0C;
constexpr uint8_t ORI     = 0x0D;
constexpr uint8_t XORI    = 0x0E;
constexpr uint8_t LUI     = 0x0F;
constexpr uint8_t COP0    = 0x10;
constexpr uint8_t COP1    = 0x11;
constexpr uint8_t COP2    = 0x12;  // VFPU
constexpr uint8_t COP3    = 0x13;
constexpr uint8_t LB      = 0x20;
constexpr uint8_t LH      = 0x21;
constexpr uint8_t LWL     = 0x22;
constexpr uint8_t LW      = 0x23;
constexpr uint8_t LBU     = 0x24;
constexpr uint8_t LHU     = 0x25;
constexpr uint8_t LWR     = 0x26;
constexpr uint8_t SB      = 0x28;
constexpr uint8_t SH      = 0x29;
constexpr uint8_t SWL     = 0x2A;
constexpr uint8_t SW      = 0x2B;
constexpr uint8_t SWR     = 0x2E;
constexpr uint8_t LWC1    = 0x31;
constexpr uint8_t SWC1    = 0x39;
constexpr uint8_t PREF    = 0x33;
} // namespace Op

// Коды функций R-type.
namespace Funct {
constexpr uint8_t SLL     = 0x00;
constexpr uint8_t SRL     = 0x02;
constexpr uint8_t SRA     = 0x03;
constexpr uint8_t SLLV    = 0x04;
constexpr uint8_t SRLV    = 0x06;
constexpr uint8_t SRAV    = 0x07;
constexpr uint8_t JR      = 0x08;
constexpr uint8_t JALR    = 0x09;
constexpr uint8_t MOVZ    = 0x0A;
constexpr uint8_t MOVN    = 0x0B;
constexpr uint8_t SYSCALL = 0x0C;
constexpr uint8_t BREAK    = 0x0D;
constexpr uint8_t SYNC    = 0x0F;
constexpr uint8_t MFHI    = 0x10;
constexpr uint8_t MTHI    = 0x11;
constexpr uint8_t MFLO    = 0x12;
constexpr uint8_t MTLO    = 0x13;
constexpr uint8_t MULT    = 0x18;
constexpr uint8_t MULTU   = 0x19;
constexpr uint8_t DIV     = 0x1A;
constexpr uint8_t DIVU    = 0x1B;
constexpr uint8_t ADD     = 0x20;
constexpr uint8_t ADDU    = 0x21;
constexpr uint8_t SUB     = 0x22;
constexpr uint8_t SUBU    = 0x23;
constexpr uint8_t AND     = 0x24;
constexpr uint8_t OR      = 0x25;
constexpr uint8_t XOR     = 0x26;
constexpr uint8_t NOR     = 0x27;
constexpr uint8_t SLT     = 0x2A;
constexpr uint8_t SLTU    = 0x2B;
} // namespace Funct

// Коды rt для опкодов COP0 / COP2.
namespace CopField {
constexpr uint8_t MFC0 = 0x00;
constexpr uint8_t MTC0 = 0x04;
constexpr uint8_t MFC2 = 0x00;
constexpr uint8_t MTC2 = 0x04;
constexpr uint8_t CFC2 = 0x02;
constexpr uint8_t CTC2 = 0x06;
} // namespace CopField

// Разобранная инструкция.
struct Decoded {
    uint32_t pc;
    uint32_t word;

    uint8_t  opcode;
    uint8_t  rs, rt, rd, shamt, funct;
    uint16_t imm;      // непосредственное значение как есть (16 бит)
    uint32_t target;   // адрес для J/JAL
    uint8_t  cop;      // Coprocessor

    // VFPU: биты 25..21 = vt (приёмник), 20..16 = vs (источник), 5..0 = fmt
    uint8_t  vs, vt;
    uint8_t  fmt;      // саб-операция VFPU

    bool     isBranch;
    uint32_t branchTarget;

    Decoded();
};

// Декодирует одну инструкцию.
Decoded Decode(uint32_t pc, uint32_t word);

} // namespace core
} // namespace ppsspp