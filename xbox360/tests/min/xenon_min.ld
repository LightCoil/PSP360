/* ВАЖНО: именно `powerpc:common`, а не `powerpc`.
   binutils для PowerPC собирается с --enable-64-bit-default (включено
   по умолчанию на 64-битном хосте), и директива OUTPUT_ARCH(powerpc)
   без подархитектуры заставляет ld выбрать 64-битный вывод. Тогда
   линковка падает с
     "powerpc:common architecture of input file ... is incompatible
      with powerpc:common64 output"
   Проверено на собранном здесь binutils 2.32:
     OUTPUT_FORMAT("elf32-powerpc")                        - работает
     OUTPUT_FORMAT("elf32-powerpc") + ARCH(powerpc)        - падает
     OUTPUT_FORMAT("elf32-powerpc") + ARCH(powerpc:common) - работает

   Комментарии в скриптах линковки пишутся как / * ... * /;
   символ '#' здесь комментарием не является. */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

/* Xenon: 32-битный PowerPC, big-endian, без ОС.
   Базовый адрес образа 0x82000000 - стандартная точка входа для XEX. */
MEMORY
{
  ram (rwx) : ORIGIN = 0x82000000, LENGTH = 512M
}

SECTIONS
{
  . = 0x82000000;

  .text : {
    KEEP(*(.text.startup))
    *(.text .text.*)
    *(.init)
    *(.fini)
    . = ALIGN(16);
  }

  .rodata : {
    *(.rodata .rodata.*)
    . = ALIGN(16);
  }

  .data : {
    *(.data .data.*)
    *(.sdata .sdata.*)
    . = ALIGN(16);
  }

  .got : { *(.got.plt) *(.got) }
  .sdata : { *(.sdata .sdata.*) }

  /* Неинициализированные данные: в образе не хранятся, только резервируются */
  .bss (NOLOAD) : {
    __bss_start__ = .;
    *(.sbss .sbss.*)
    *(.scommon)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
    __bss_end__ = .;
  }

  __bss_size__ = __bss_end__ - __bss_start__;

  _end = .;
  PROVIDE(end = .);

  /DISCARD/ : {
    *(.comment)
    *(.note.GNU-stack)
    *(.interp)
    *(.dynamic)
    *(.dynsym)
    *(.dynstr)
    *(.hash)
    *(.plt)
  }
}
