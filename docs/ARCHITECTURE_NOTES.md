# Проверенные архитектурные данные PSP / Allegrex

Источники (зафиксированы 18.10.2026):

1. **PSP CPU Overview** — «yet another PlayStationPortable Documentation»
   (Hitmen): `https://hitmen.c02.at/files/yapspd/psp_doc/chap4.html`
2. **PPSSPP** (`hrydgard/ppsspp`, ветка `master`) — эталонная реализация:
   - `Core/MIPS/MIPSVFPUUtils.h` — макросы извлечения полей VFPU
   - `Core/MIPS/MIPSTables.cpp` — таблицы `tableVFPU*`, `encodingBits[]`, `mipsTables[]`
   - `Core/MIPS/MIPSDisVFPU.cpp` — `GetVecSize()` / `VSuff()`
3. **MIPS32 Architecture For Programmers Vol. III** — раскладка CP0.

Ниже — только то, что подтверждено этими источниками. Догадок нет.

---

## 1. Порядок байтов

Allegrex работает в **little-endian** режиме (Config.LEL). ELF-образы PSP —
MIPSEL (`EI_DATA = ELFDATA2LSB`). Следовательно и данные, и выборка инструкций
little-endian.

> Проект исторически использовал big-endian в `Memory` при том, что загрузчик
> читал MIPSEL. Это взаимно противоречиво. Исправлено в пользу little-endian.

## 2. VFPU: раскладка полей

Из `MIPSVFPUUtils.h` (PPSSPP master), дословно:

```c
#define _VD (op & 0x7F)         // приёмник,      биты 6..0
#define _VS ((op>>8) & 0x7F)    // источник 1,    биты 14..8
#define _VT ((op>>16) & 0x7F)   // источник 2,    биты 22..16
```

**Все три поля по 7 бит** (регистры 0..127). Биты 15 и 7 НЕ входят в поля
регистров — они несут размер вектора:

`GetVecSize()` / `VSuff()` (PPSSPP):

```c
int a = (op>>7)&1;    // бит 7  — младший бит размера
int b = (op>>15)&1;   // бит 15 — старший бит размера
a += (b<<1);
// 0 = .s (1 lane), 1 = .p (2), 2 = .t (3), 3 = .q (4)
```

Это совпадает с официальной таблицей Hitmen: `vadd.s` = `0x60000000`,
`vadd.p` = `0x60000080` (бит 7), `vadd.t` = `0x60008000` (бит 15),
`vadd.q` = `0x60008080`.

## 3. VFPU: опкоды

VFPU **не** живёт целиком в COP2 (0x12). Ключ выбирается по опкоду MIPS
(биты 31..26) и под-индексу:

| Опкод | Под-индекс | Таблица | Поддерживаемые сейчас |
|-------|-------------|---------|------------------------|
| `0x18` `011000` | биты 25..23 | `tableVFPU0[8]` | 0=vadd, 1=vsub, 7=vdiv |
| `0x19` `011001` | биты 25..23 | `tableVFPU1[8]` | 0=vmul |
| `0x1B` `011011` | биты 25..23 | `tableVFPU3[8]` | 2=vmin, 3=vmax |
| `0x34` `110100` | биты 25..21 | `tableVFPU4Jump[32]` | → `tableVFPU4` |
| `0x34`, биты 25..21 == 0 | биты 20..16 | `tableVFPU4[32]` | 0=vmov, 1=vabs, 2=vneg |
| `0x12` `010010` | биты 25..21 | `tableCop2[32]` | lv.s, sv.s, mftv, cfti … |

Контрольные слова (`tableVFPU*` + Hitmen), все поля регистров нулевые:

```
vadd.s 0x60000000   vsub.s 0x60800000   vdiv.s 0x63800000
vmul.s 0x64000000   vmin.s 0x6D000000   vmax.s 0x6D800000
vmov   0xD0000000   vabs.s 0xD0010000   vneg.s 0xD0020000
```

Следствия, важные для декодера:

* источник 1 — **биты 14..8**, а НЕ 21..25 (как в обычном R-type);
* источник 2 — **биты 22..16**;
* приёмник — **биты 6..0**;
* бит 7 и бит 15 зарезервированы под размер вектора и **обнуляются**
  при извлечении регистра;
* поле размера вектора не совпадает с `fmt = word & 0x3F`.

## 4. CP0 (Hitmen, MIPS32 Vol. III)

| Рег | Имя | Примечание |
|-----|-----|------------|
| 8 | BadVAddr | |
| 9 | Count | |
| 11 | Compare | |
| 12 | **Status** | |
| 13 | **Cause** | |
| 14 | **EPC** | |
| 15 | PRId | |
| 16 | Config | |
| 25 | **EBase** | база векторов исключений |
| 30 | ErrorEPC | |

**Отдельного регистра Interrupt Mask на Allegrex нет.** Маска прерываний —
поле **IM в Status, биты 15..8** (8 линий, по 1 биту на линию).

Status (MIPS32):
```
bit 0     IE      Interrupt Enable
bit 1     EXL     Exception Level
bit 2     ERL     Error Level
bit 22    BE      Boot Exception
bits15..8 IM[7:0] Interrupt Mask
```

Cause (MIPS32):
```
bits 6..2 ExcCode
bit  31    BD      Branch Delay
bits15..10 IP[5:0]
```
Линии прерывания PSP выделены в Cause биты 8..15 (8 линий), что согласуется
с 8 битами IM в Status и со схемой векторов ниже.

## 5. Векторы исключений

Формула MIPS32:

```
General:  vector = EBase + 0x100 + ExcCode * 0x20
Interrupt: vector = EBase + 0x100 + irq    * 0x20
```

Согласуется с Hitmen (`SC_TABLE`, `EXC_8_SYSCALL`, `EXC_31_ERROR`).

Коды исключений (Cause.ExcCode, биты 6..2), нужные проекту:

| ExcCode | Имя | Вектор при EBase=0 |
|---------|-----|---------------------|
| 0 | Int (прерывание) | `0x100 + irq*0x20` |
| 4 | AdEL (ошибка адреса при чтении) | `0x180` |
| 5 | AdES (ошибка адреса при записи) | `0x1A0` |
| 8 | Sys (syscall) | `0x200` |
| 9 | Bp (break) | `0x220` |
| 10 | RI (недопустимая инструкция) | `0x240` |
| 12 | **Ov — Arithmetic Overflow** | `0x280` |

> В ранней редакции этого документа для переполнения был указан код 4 —
> это AdEL. Корректный код арифметического переполнения — **12** (Ov).

Именно поэтому адрес возврата при исключении кладётся в EPC вместе с BD=1:
инструкция находится в слоте задержки, поэтому EPC указывает на саму
инструкцию ветвления, а не на инструкцию в слоте.

## 6. PBP

Формат PBP (Little-endian):

```
0x00  magic "\0PBP"
0x04  version (0x00010000 | 0x00020000)
0x08  offsets[8]   : 0 param.sfo, 1 icon0, 2 icon1, 3 pic0, 4 pic1,
                    5 snd0,     6 data.psp, 7 data.psar
0x28  data.psp size      (v0x100)
0x2C  data.psar size     (v0x200)
```

Смещение `0` означает «секции нет». Часть писателей PBP кладёт на место
отсутствующей секции `0xFFFFFFFF` — это **тоже** «секции нет» и такое значение
**нельзя** использовать как границу буфера.