//
// Created by Luke on 07/01/2026.
//

#ifndef DJINN_LOGGER_H
#define DJINN_LOGGER_H

#include <cstdio>
#include <cstdarg>
#include <chrono>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace logger {
    enum class Level { TRACE, DEBUG, INFO, WARN, ERROR };

#ifndef LOGGER_LEVEL
    constexpr auto LOGGING_LEVEL = Level::DEBUG;
#else
    constexpr auto LOGGING_LEVEL = LOGGER_LEVEL;
#endif

    inline const char *levelStr[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
    inline const char *levelColor[] = {"\033[90m", "\033[36m", "\033[32m", "\033[33m", "\033[31m"};


    inline void vlog(Level level, const char *formatter, va_list args) {
        if (level < LOGGING_LEVEL) return;

        auto *out = level >= Level::ERROR ? stderr : stdout;
        const auto idx = static_cast<int>(level);

        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto *tm = std::localtime(&time);

        std::fprintf(out, "%s[%02d:%02d:%02d.%03d] [%s]\033[0m ",
                     levelColor[idx], tm->tm_hour, tm->tm_min, tm->tm_sec, static_cast<int>(ms.count()), levelStr[idx]);
        std::vfprintf(out, formatter, args);
        std::fprintf(out, "\n");
        std::fflush(out);
    }

    inline void log(const Level level, const char *formatter, ...) {
        va_list args;
        va_start(args, formatter);
        vlog(level, formatter, args);
        va_end(args);
    }

    inline void trace(const char *formatter, ...) {
        va_list arguments;
        va_start(arguments, formatter);
        vlog(Level::TRACE, formatter, arguments);
        va_end(arguments);
    }

    inline void debug(const char *formatter, ...) {
        va_list arguments;
        va_start(arguments, formatter);
        vlog(Level::DEBUG, formatter, arguments);
        va_end(arguments);
    }

    inline void info(const char *formatter, ...) {
        va_list arguments;
        va_start(arguments, formatter);
        vlog(Level::INFO, formatter, arguments);
        va_end(arguments);
    }

    inline void warn(const char *formatter, ...) {
        va_list arguments;
        va_start(arguments, formatter);
        vlog(Level::WARN, formatter, arguments);
        va_end(arguments);
    }

    inline void error(const char *formatter, ...) {
        va_list arguments;
        va_start(arguments, formatter);
        vlog(Level::ERROR, formatter, arguments);
        va_end(arguments);
    }
} // namespace logger

#endif //DJINN_LOGGER_H
