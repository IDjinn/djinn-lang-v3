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
#include <string>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#define ENABLE_COLOR 0
#define ENABLE_STACKTRACE_ON_ERROR 1

namespace logger
{
    enum class Level { TRACE, DEBUG, INFO, WARN, ERROR };

    extern const char* LEVEL_NAMES[];
    extern const char* LEVEL_COLORS[];

    struct Config
    {
        Level level = Level::INFO;
        std::string format;
    };

    Config& get_config();
    std::mutex& get_mutex();

    Level parse_level(const std::string& str);
    void configure(const std::string& level, const std::string& format = "");
    void log(Level level, const char* filePath, int line, const char* fmt, ...);
    void log_with_stacktrace(Level level, const char* filePath, int line,
                             const std::stacktrace& trace, const char* fmt, ...);

    constexpr const char* relative_path(const char* path)
    {
#ifdef DJINN_SOURCE_DIR
        const char* base = DJINN_SOURCE_DIR;
        const char* p = path;
        const char* b = base;
        while (*p && *b)
        {
            char pc = (*p == '\\') ? '/' : *p;
            char bc = (*b == '\\') ? '/' : *b;
            if (pc != bc) break;
            p++;
            b++;
        }
        if (*b == '\0')
        {
            if (*p == '/' || *p == '\\') p++;
            return p;
        }
#endif
        return path;
    }
} // namespace logger

#define DJINN_REL_FILE logger::relative_path(__FILE__)

#define LOG_TRACE(...) logger::log(logger::Level::TRACE, DJINN_REL_FILE, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) logger::log(logger::Level::DEBUG, DJINN_REL_FILE, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  logger::log(logger::Level::INFO,  DJINN_REL_FILE, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  logger::log(logger::Level::WARN,  DJINN_REL_FILE, __LINE__, __VA_ARGS__)

#if ENABLE_STACKTRACE_ON_ERROR
#define LOG_ERROR(...) logger::log_with_stacktrace(logger::Level::ERROR, DJINN_REL_FILE, __LINE__, \
    std::stacktrace::current(), __VA_ARGS__)
#else
#define LOG_ERROR(...) logger::log(logger::Level::ERROR, DJINN_REL_FILE, __LINE__, __VA_ARGS__)
#endif

#endif //DJINN_LOGGER_H
