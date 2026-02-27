//
// Created by Luke on 07/01/2026.
//

#ifndef DJINN_LOGGER_H
#define DJINN_LOGGER_H

#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <mutex>
#include <stacktrace>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#define LOGGER_LEVEL Level::WARN
#define ENABLE_COLOR 0
#define ENABLE_STACKTRACE_ON_ERROR 1

namespace logger
{
    enum class Level { TRACE, DEBUG, INFO, WARN, ERROR };

#ifndef LOGGER_LEVEL
    constexpr auto LOGGING_LEVEL = Level::INFO;
#else
    constexpr auto LOGGING_LEVEL = LOGGER_LEVEL;
#endif

    inline const char* levelStr[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
    inline const char* levelColor[] = {"\033[90m", "\033[36m", "\033[32m", "\033[33m", "\033[31m"};

    inline std::mutex& get_mutex()
    {
        static std::mutex mtx;
        return mtx;
    }

    inline void log(Level level, const char* filePath, int line, const char* fmt, ...)
    {
        if (level < LOGGING_LEVEL) return;

        std::lock_guard lock(get_mutex());

        auto* out = level >= Level::ERROR ? stderr : stdout;
        const auto idx = static_cast<int>(level);

        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto* tm = std::localtime(&time);

#if ENABLE_COLOR
        std::fprintf(out, "%s[%02d:%02d:%02d %03d] [%s:%d] [%s]\033[0m ",
                     levelColor[idx], tm->tm_hour, tm->tm_min, tm->tm_sec, static_cast<int>(ms.count()),
                     filePath, line, levelStr[idx]);
#else
        std::fprintf(out, "[%02d:%02d:%02d %03d] [%s:%d] [%s] ",
                     tm->tm_hour, tm->tm_min, tm->tm_sec, static_cast<int>(ms.count()),
                     filePath, line, levelStr[idx]);
#endif
        va_list args;
        va_start(args, fmt);
        std::vfprintf(out, fmt, args);
        va_end(args);
        std::fprintf(out, "\n");
        std::fflush(out);
    }

    inline void log_with_stacktrace(Level level, const char* filePath, int line,
                                    const std::stacktrace& trace, const char* fmt, ...)
    {
        if (level < LOGGING_LEVEL) return;

        std::lock_guard lock(get_mutex());

        auto* out = level >= Level::ERROR ? stderr : stdout;
        const auto idx = static_cast<int>(level);

        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto* tm = std::localtime(&time);

#if ENABLE_COLOR
        std::fprintf(out, "%s[%02d:%02d:%02d %03d] [%s:%d] [%s]\033[0m ",
                     levelColor[idx], tm->tm_hour, tm->tm_min, tm->tm_sec, static_cast<int>(ms.count()),
                     filePath, line, levelStr[idx]);
#else
        std::fprintf(out, "[%02d:%02d:%02d %03d] [%s:%d] [%s] ",
                     tm->tm_hour, tm->tm_min, tm->tm_sec, static_cast<int>(ms.count()),
                     filePath, line, levelStr[idx]);
#endif
        va_list args;
        va_start(args, fmt);
        std::vfprintf(out, fmt, args);
        va_end(args);
        std::fprintf(out, "\n");

        // Print stacktrace
        std::fprintf(out, "  Stacktrace:\n");
        for (const auto& entry : trace)
        {
            std::fprintf(out, "    %s\n", std::to_string(entry).c_str());
        }
        std::fflush(out);
    }
} // namespace logger

#define LOG_TRACE(...) logger::log(logger::Level::TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) logger::log(logger::Level::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  logger::log(logger::Level::INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  logger::log(logger::Level::WARN,  __FILE__, __LINE__, __VA_ARGS__)

#if ENABLE_STACKTRACE_ON_ERROR
#define LOG_ERROR(...) logger::log_with_stacktrace(logger::Level::ERROR, __FILE__, __LINE__, \
    std::stacktrace::current(), __VA_ARGS__)
#else
#define LOG_ERROR(...) logger::log(logger::Level::ERROR, __FILE__, __LINE__, __VA_ARGS__)
#endif

#endif //DJINN_LOGGER_H