// xenon_min.c - минимальная программа для Xbox 360, собираемая
// целевым GCC. Проверяет сам компилятор: генерацию кода, соглашения
// о вызовах, доступность newlib/libgcc.
//
// Диагностика идёт через UART Xenon (аппаратный порт 0x8008200),
// а не через printf: так результат не зависит от того, удалось ли
// собрать libc с нужными syscalls.

#include <stdint.h>

#define UART_BASE 0x8008200u

// Запись одного байта в UART Xenon. Регистры UART отображены с
// шагом 16 байт; байт данных - младший байт регистра.
static inline void uart_put(char c)
{
    volatile uint8_t *reg = (volatile uint8_t *)(uintptr_t)(UART_BASE);
    *reg = (uint8_t)c;
}

static void puts_(const char *s)
{
    while (*s) {
        uart_put(*s++);
    }
}

// Минимальный набор проверок: значения вычисляются компилятором и
// должны быть верными при любой допустимой оптимизации.
static uint32_t add3(uint32_t a, uint32_t b, uint32_t c)
{
    return a + b + c;
}

static uint32_t rotl32(uint32_t v, unsigned n)
{
    return (v << n) | (v >> (32u - n));
}

static uint64_t mul64(uint32_t a, uint32_t b)
{
    return (uint64_t)a * (uint64_t)b;
}

static int slt_check(int a, int b)
{
    // Знаковое сравнение: критично для проверки расширения операндов
    return (a < b) ? 1 : 0;
}

static uint32_t big_switch(uint32_t x)
{
    switch (x) {
    case 0x00000000u: return 0x11111111u;
    case 0xdeadbeefu: return 0x22222222u;
    case 0xffffffffu: return 0x33333333u;
    default:          return 0x44444444u;
    }
}

void _start(void)
{
    puts_("PSPX-MIN-OK\n");

    volatile uint32_t r1 = add3(1u, 2u, 3u);
    volatile uint32_t r2 = rotl32(0x12345678u, 8u);
    volatile uint64_t r3 = mul64(0x10000u, 0x10000u);
    volatile int      r4 = slt_check(-1, 1);
    volatile uint32_t r5 = big_switch(0xdeadbeefu);

    // Печатаем признаки в виде 0/1, чтобы их можно было проверить
    // визуально в выводе UART, не полагаясь на printf.
    uart_put(r1 == 6u       ? '1' : '0');
    uart_put(r2 == 0x34567812u ? '1' : '0');
    uart_put(r3 == 0x100000000ull ? '1' : '0');
    uart_put(r4 == 1        ? '1' : '0');
    uart_put(r5 == 0x22222222u ? '1' : '0');
    uart_put('\n');

    // Возврат в бесконечный цикл: на реальной консоли программа не
    // должна возвращаться в вызывающий код, которого здесь нет.
    for (;;) {
    }
}
