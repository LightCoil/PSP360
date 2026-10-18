// core/logger.h — минимальный логгер без внешних зависимостей.
#pragma once

#include <cstdint>

namespace ppsspp {
namespace core {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Off   = 5,
};

// Формат вывода: "%04d-%02d-%02d %02d:%02d:%02d.%03d [%s] "
bool InitLogger(const char* logPath = nullptr);
void ShutdownLogger();
void SetLogLevel(LogLevel level);
LogLevel GetLogLevel();

void LogWrite(LogLevel level, const char* file, int line, const char* fmt, ...);

} // namespace core
} // namespace ppsspp

#define PSP_LOG(level, ...) \
    ::ppsspp::core::LogWrite(level, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_TRACE(...) PSP_LOG(::ppsspp::core::LogLevel::Trace, __VA_ARGS__)
#define LOG_DEBUG(...) PSP_LOG(::ppsspp::core::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  PSP_LOG(::ppsspp::core::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  PSP_LOG(::ppsspp::core::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) PSP_LOG(::ppsspp::core::LogLevel::Error, __VA_ARGS__)