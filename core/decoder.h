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
constexpr uint8_t COP2    = 0x12;  // перенос VFPU-регистров (lv.s, sv.s, mftv)
constexpr uint8_t COP3    = 0x13;

// VFPU Allegrex. В отличие от обычных сопроцессоров MIPS, VFPU занимает
// несколько опкодов MIPS; под-индекс выбирает операцию внутри опкода.
// Источники: PPSSPP Core/MIPS/MIPSTables.cpp (tableVFPU*, encodingBits[]).
constexpr uint8_t VFPU_A  = 0x18;  // tableVFPU0[8]: vadd vsub vsbn ... vdiv
constexpr uint8_t VFPU_B  = 0x19;  // tableVFPU1[8]: vmul vdot vscl vhdp ...
constexpr uint8_t VFPU_C  = 0x1B;  // tableVFPU3[8]: vcmp vmin vmax vsge vslt
constexpr uint8_t VFPU_D  = 0x34;  // tableVFPU4Jump/tableVFPU4: vmov vabs vneg

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

// Поля инструкций сопроцессоров.
//
// COP0 (опкод 0x10). Селектор операции берётся из битов 25..21 —
// это подтверждено эталонной реализацией:
//
//   PPSPP, Core/MIPS/MIPSTables.cpp
//     encodingBits[Cop0] = EncodingBitsInfo(21, 5);   // -> биты 25..21
//     tableCop0[32] = // 010000 xxxxx ..... ................
//        [0] = mfc0   [4] = mtc0   [10] = rdpgpr
//        [11] = mfmc0 (di/ei)       [14] = wrpgpr
//        [16..31] = ENCODING(Cop0CO)  (бит 25 = 1: eret/wait/tlb*)
//   PPSPP, Core/MIPS/Interpreter.cpp, Int_Cop0
//     switch ((op >> 21) & 0x1f) { case 0: //mfc0  case 4: //mtc0 ... }
//
// MIPS32 Vol. III задаёт иную раскладку: rs = 0, вид операции в битах
// 4..0 (MF = 0, MT = 4, CF = 2, CT = 6), биты 10..5 обязаны быть нулевыми.
// Поддерживаются обе схемы. Инструкция распознаётся, только если
// зарезервированные биты 10..3 равны нулю; всё остальное отвергается.
//
//   rs5 = биты 25..21   rt = биты 20..16 (GPR)   rd = биты 15..11 (CP0)
//   code = биты 2..0 (подфункция; для MF/MT должна быть 0)
//   бит 25 = CO: 1 означает сопроцессор 1 (FPU), а не регистры CPU
enum Cop0Kind : uint8_t {
    COP0_UNKNOWN = 0,   // не распознано
    COP0_MFC0,          // чтение регистра CP0
    COP0_MTC0,          // запись регистра CP0
    COP0_CFC0,          // чтение контрольного регистра (не реализовано)
    COP0_CTC0,          // запись контрольного регистра (не реализовано)
};

namespace CopField {
// Поле битов 4..0 по MIPS32 Vol. III.
constexpr uint8_t CF = 0x02;   // cfc0
constexpr uint8_t CT = 0x06;   // ctc0
constexpr uint8_t MF = 0x00;   // mfc0
constexpr uint8_t MT = 0x04;   // mtc0
} // namespace CopField

namespace CopRs {
// Селектор по битам 25..21 (PPSPP).
constexpr uint8_t MFC0 = 0x00;
constexpr uint8_t MTC0 = 0x04;
constexpr uint8_t RDPGPR = 0x0A;
constexpr uint8_t MFMC0 = 0x0B;   // di / ei на Allegrex
constexpr uint8_t WRPGPR = 0x0E;
} // namespace CopRs

// Зарезервированное поле COP0: биты 10..3.
constexpr uint32_t COP0_RESERVED_MASK = 0x000007F8u;
// Бит CO (бит 25): 1 = сопроцессор 1, а не регистры CPU.
constexpr uint32_t COP0_CO_BIT = 0x02000000u;

// Разобранная инструкция.
struct Decoded {
    uint32_t pc;
    uint32_t word;

    uint8_t  opcode;
    uint8_t  rs, rt, rd, shamt, funct;
    uint16_t imm;      // непосредственное значение как есть (16 бит)
    uint32_t target;   // адрес для J/JAL
    uint8_t  cop;      // Coprocessor

    // ---- сопроцессоры ----
    // COP0: copRs — биты 25..21 (селектор по PPSPP), copOp — биты 4..0
    // (поле MIPS32), copKind — итоговый вид операции после проверки
    // зарезервированных битов. copCode — биты 2..0 (подфункция).
    uint8_t  copRs;
    uint8_t  copOp;
    uint8_t  copCode;
    uint8_t  copKind;

    // ---- VFPU (COP2-подобные опкоды 0x18/0x19/0x1B/0x34) ----
    //
    // Раскладка подтверждена PPSSPP (Core/MIPS/MIPSVFPUUtils.h):
    //     _VD = op & 0x7F        биты 6..0   приёмник
    //     _VS = (op>>8) & 0x7F   биты 14..8  источник 1
    //     _VT = (op>>16) & 0x7F  биты 22..16 источник 2
    // Биты 15 и 7 НЕ входят в поля регистров — они несут размер вектора
    // (VSuff(): ((op>>15)&1)<<1 | ((op>>7)&1); 0=.s 1=.p 2=.t 3=.q).
    uint8_t  vfpuVd;    // приёмник, 0..127
    uint8_t  vfpuVs;    // источник 1, 0..127
    uint8_t  vfpuVt;    // источник 2, 0..127
    uint8_t  vfpuSize;  // 0 = .s (один lane), 1 = .p, 2 = .t, 3 = .q
    uint8_t  vfpuFunc;  // номер операции внутри таблицы опкода
    bool     isVfpu;   // инструкция принадлежит подсистеме VFPU

    // Поля COP2 (lv.s, sv.s, mftv, cfti ...). Здесь биты имеют обычный
    // для MIPS смысл: rt = биты 20..16, rd = биты 15..11.
    uint8_t  vs, vt;
    uint8_t  fmt;

    bool     isBranch;
    uint32_t branchTarget;

    Decoded();
};

// Декодирует одну инструкцию.
Decoded Decode(uint32_t pc, uint32_t word);

} // namespace core
} // namespace ppsspp