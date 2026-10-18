#include "logger.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/time.h>
#endif

namespace ppsspp {
namespace core {

namespace {
FILE*      g_file    = nullptr;
LogLevel   g_level   = LogLevel::Info;
bool       g_owned   = false;

const char* LevelName(LogLevel l) {
    switch (l) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    default:              return "OFF  ";
    }
}

// Метки времени для всех платформ.
void GetTimestamp(char* buf, size_t n) {
    struct timespec ts;
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const unsigned long long ticks =
        (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) |
         static_cast<unsigned long long>(ft.dwLowDateTime);
    // 100 нс -> с, затем Unix-эпоха
    ts.tv_sec  = static_cast<time_t>(ticks / 10000000ULL - 11644473600ULL);
    ts.tv_nsec = static_cast<long>((ticks % 10000000ULL) * 100ULL);
#else
    timespec_get(&ts, TIME_UTC);
#endif
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &ts.tv_sec);
#else
    localtime_r(&ts.tv_sec, &tmv);
#endif
    std::snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                  static_cast<int>(ts.tv_nsec / 1000000));
}

// Обрезает путь до имени файла.
const char* BaseName(const char* path) {
    const char* slash = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') slash = p + 1;
    }
    return slash;
}
} // namespace

bool InitLogger(const char* logPath) {
    ShutdownLogger();
    if (logPath && logPath[0]) {
        g_file = std::fopen(logPath, "w");
        g_owned = (g_file != nullptr);
    }
    return g_file != nullptr || logPath == nullptr;
}

void ShutdownLogger() {
    if (g_file) {
        if (g_owned) std::fclose(g_file);
        g_file = nullptr;
        g_owned = false;
    }
}

void SetLogLevel(LogLevel level) { g_level = level; }
LogLevel GetLogLevel() { return g_level; }

void LogWrite(LogLevel level, const char* file, int line, const char* fmt, ...) {
    if (static_cast<int>(level) < static_cast<int>(g_level)) return;
    if (level == LogLevel::Off) return;

    char ts[40];
    GetTimestamp(ts, sizeof(ts));

    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    if (g_file) {
        std::fprintf(g_file, "%s [%s] %s:%d  %s\n", ts, LevelName(level),
                     BaseName(file), line, body);
        std::fflush(g_file);
    } else {
        std::fprintf(stderr, "%s [%s] %s:%d  %s\n", ts, LevelName(level),
                     BaseName(file), line, body);
    }
}

} // namespace core
} // namespace ppsspp