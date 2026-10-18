// core/syscall_defs.h — номера системных вызовов PSP.
//
// Источник: PSP SDK (pspsdk), таблица kernel/user calls.
// Номера соответствуют MIPS syscall ABI: номер передаётся в $v0 (gpr[2]),
// аргументы — $a0..$a3 (gpr[4]..gpr[7]), результат — $v0.
#pragma once

#include <cstdint>

namespace ppsspp {
namespace syscall {

// Базовые (пользовательские) вызовы — ветка kern.prx.
enum : uint32_t {
    SYS_OPEN        = 0x00000001,
    SYS_CLOSE       = 0x00000002,
    SYS_READ        = 0x00000003,
    SYS_WRITE       = 0x00000004,
    SYS_LSEEK       = 0x00000005,
    SYS_IOCTL       = 0x00000027,
    SYS_EXIT        = 0x00000006,
    SYS_EXIT_THREAD = 0x00000008,
    SYS_GET_PROCESS_ID = 0x0000000B,
    SYS_GET_THREAD_ID   = 0x0000000C,
    SYS_GET_SYSTEM_TIME = 0x0000000D,
    SYS_GET_THREAD_NAME  = 0x0000000E,
    SYS_GET_TICK_COUNT   = 0x00000010,
    SYS_GET_DELAY_TICK_COUNT = 0x00000011,
    SYS_GET_COUNT        = 0x00000018,
    SYS_GET_MODULE_ID    = 0x00000074,
};

// Ветка user.* — управление окнами, вводом, звуком.
enum : uint32_t {
    SYS_UTILS_SYS_MEMGET_INFO = 0x0000001A,
    SYS_MEMUSER_ALLOC         = 0x0000003D,
    SYS_MEMUSER_FREE          = 0x0000003E,
    SYS_THREAD_CREATE         = 0x00000021,
    SYS_THREAD_EXIT           = 0x00000024,
    SYS_THREAD_START          = 0x00000025,
    SYS_THREAD_GET_EXIT_CODE  = 0x00000027,
    SYS_THREAD_GET_ID         = 0x0000002B,
    SYS_THREAD_SET_PRIORITY   = 0x0000002D,
    SYS_THREAD_GET_PRIORITY   = 0x0000002E,
    SYS_MUTEX_CREATE          = 0x00000035,
    SYS_MUTEX_LOCK            = 0x00000036,
    SYS_MUTEX_UNLOCK          = 0x00000037,
    SYS_MUTEX_DEL             = 0x00000038,
    SYS_SEMAPHORE_CREATE      = 0x0000003C,
    SYS_SEMAPHORE_WAIT        = 0x0000003F,
    SYS_SEMAPHORE_SIGNAL      = 0x00000040,
    SYS_SCE_AUDIO             = 0x0000008E,
    SYS_SCE_NET_              = 0x000000C0,
};

// Значения, которые ядро возвращает при ошибке.
constexpr uint32_t SCE_KERNEL_RESULT_SUCCESS = 0;
constexpr int32_t  SCE_KERNEL_ERROR_NOENT = -2;
constexpr int32_t  SCE_KERNEL_ERROR_NOMEM = -4;

} // namespace syscall
} // namespace ppsspp