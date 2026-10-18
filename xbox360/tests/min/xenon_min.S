# xenon_min.S - минимальная программа для Xbox 360 (Xenon), ассемблерная часть.
#
# Ни от чего, кроме пустоты, не зависит: ни libxenon, ни системных
# вызовов, ни графики. Задача - проверить, что ассемблер и линкер
# тулчейна действительно производят код целевой архитектуры.
#
# ВНИМАНИЕ: gas для PowerPC не понимает комментарии '//'.
# Комментарии пишутся через '#'.

    .section .text
    .globl  _start
    .type   _start, @function
_start:
    # Записываем маркер в UART Xenon: базовый адрес 0x8008200,
    # регистры UART отображены с шагом 16 байт, младший байт -
    # данные. Это аппаратный порт, а не вызов библиотеки.
    lis     r4, 0x8008
    ori     r4, r4, 0x8200
    li      r5, 'O'
    stb     r5, 0(r4)

    # Проверка конвенции о вызовах: аргументы в r3..r10,
    # результат в r3. Вызываем проверочную функцию из C-файла.
    li      r3, 0x1111
    li      r4, 0x2222
    bl      min_check
    cmpwi   r3, 0
    bne     1f
    li      r5, 'P'
    stb     r5, 0(r4)
1:
    # Возврат невозможен - вызывающего кода нет.
2:  b       2b
    .size   _start, .-_start

    .section .rodata
    .align  3
msg:
    .asciz  "PSPX-MIN-OK"
msg_end:

    .section .data
    .align  3
    .globl  _end
_end:
    .long   0

    .section .bss
    .align  3
stack_bottom:
    .space  0x4000
stack_top:

    .section .note.GNU-stack,"",@progbits
