#include "Logger.h"

namespace logger
{
    const char* LEVEL_NAMES[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
    const char* LEVEL_COLORS[] = {"\033[90m", "\033[36m", "\033[32m", "\033[33m", "\033[31m"};

    Config& get_config()
    {
        static Config cfg;
        return cfg;
    }

    std::mutex& get_mutex()
    {
        static std::mutex mtx;
        return mtx;
    }

    Level parse_level(const std::string& str)
    {
        if (str == "TRACE") return Level::TRACE;
        if (str == "DEBUG") return Level::DEBUG;
        if (str == "INFO")  return Level::INFO;
        if (str == "WARN")  return Level::WARN;
        if (str == "ERROR") return Level::ERROR;
        return Level::INFO;
    }

    void configure(const std::string& level, const std::string& format)
    {
        auto& cfg = get_config();
        cfg.level = parse_level(level);
        cfg.format = format;
    }

    static bool is_datetime_token(const std::string& s)
    {
        return s.find("dd") != std::string::npos
            || s.find("MM") != std::string::npos
            || s.find("YY") != std::string::npos
            || s.find("HH") != std::string::npos
            || s.find("mm") != std::string::npos
            || s.find("ss") != std::string::npos;
    }

    static void emit_datetime(FILE* out, const std::string& pattern, const std::tm* tm, int millis)
    {
        size_t i = 0;
        while (i < pattern.size())
        {
            if (pattern.compare(i, 4, "YYYY") == 0) {
                std::fprintf(out, "%04d", tm->tm_year + 1900);
                i += 4;
            } else if (pattern.compare(i, 4, "mmmm") == 0) {
                std::fprintf(out, "%04d", millis);
                i += 4;
            } else if (pattern.compare(i, 3, "mmm") == 0) {
                std::fprintf(out, "%03d", millis);
                i += 3;
            } else if (pattern.compare(i, 2, "dd") == 0) {
                std::fprintf(out, "%02d", tm->tm_mday);
                i += 2;
            } else if (pattern.compare(i, 2, "MM") == 0) {
                std::fprintf(out, "%02d", tm->tm_mon + 1);
                i += 2;
            } else if (pattern.compare(i, 2, "YY") == 0) {
                std::fprintf(out, "%02d", (tm->tm_year + 1900) % 100);
                i += 2;
            } else if (pattern.compare(i, 2, "HH") == 0) {
                std::fprintf(out, "%02d", tm->tm_hour);
                i += 2;
            } else if (pattern.compare(i, 2, "mm") == 0) {
                std::fprintf(out, "%02d", tm->tm_min);
                i += 2;
            } else if (pattern.compare(i, 2, "ss") == 0) {
                std::fprintf(out, "%02d", tm->tm_sec);
                i += 2;
            } else {
                std::fputc(pattern[i], out);
                i++;
            }
        }
    }

    static void emit_format(FILE* out, const std::string& format, Level level,
                            const char* filePath, int line, const std::tm* tm, int millis,
                            const char* fmt, va_list args)
    {
        const auto idx = static_cast<int>(level);
        size_t i = 0;
        while (i < format.size())
        {
            if (format[i] == '{')
            {
                auto end = format.find('}', i);
                if (end == std::string::npos) { std::fputc('{', out); i++; continue; }

                auto token = format.substr(i + 1, end - i - 1);

                if (token == "...")
                    std::vfprintf(out, fmt, args);
                else if (token == "file")
                    std::fprintf(out, "%s", filePath);
                else if (token == "line")
                    std::fprintf(out, "%d", line);
                else if (token == "level")
                    std::fprintf(out, "%s", LEVEL_NAMES[idx]);
                else if (is_datetime_token(token))
                    emit_datetime(out, token, tm, millis);
                else
                    std::fprintf(out, "{%s}", token.c_str());

                i = end + 1;
            }
            else
            {
                std::fputc(format[i], out);
                i++;
            }
        }
    }

    void log(Level level, const char* filePath, int line, const char* fmt, ...)
    {
        const auto& cfg = get_config();
        if (level < cfg.level) return;

        std::lock_guard lock(get_mutex());

        auto* out = level >= Level::ERROR ? stderr : stdout;

        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto* tm = std::localtime(&time);
        const int millis = static_cast<int>(ms.count());

        va_list args;
        va_start(args, fmt);

        if (!cfg.format.empty())
        {
            emit_format(out, cfg.format, level, filePath, line, tm, millis, fmt, args);
        }
        else
        {
            const auto idx = static_cast<int>(level);
#if ENABLE_COLOR
            std::fprintf(out, "%s[%02d:%02d:%02d %03d] [%s:%d] [%s]\033[0m ",
                         LEVEL_COLORS[idx], tm->tm_hour, tm->tm_min, tm->tm_sec, millis,
                         filePath, line, LEVEL_NAMES[idx]);
#else
            std::fprintf(out, "[%02d:%02d:%02d %03d] [%s:%d] [%s] ",
                         tm->tm_hour, tm->tm_min, tm->tm_sec, millis,
                         filePath, line, LEVEL_NAMES[idx]);
#endif
            std::vfprintf(out, fmt, args);
        }

        va_end(args);
        std::fprintf(out, "\n");
        std::fflush(out);
    }

    void log_with_stacktrace(Level level, const char* filePath, int line,
                             const std::stacktrace& trace, const char* fmt, ...)
    {
        const auto& cfg = get_config();
        if (level < cfg.level) return;

        std::lock_guard lock(get_mutex());

        auto* out = level >= Level::ERROR ? stderr : stdout;

        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto* tm = std::localtime(&time);
        const int millis = static_cast<int>(ms.count());

        va_list args;
        va_start(args, fmt);

        if (!cfg.format.empty())
        {
            emit_format(out, cfg.format, level, filePath, line, tm, millis, fmt, args);
        }
        else
        {
            const auto idx = static_cast<int>(level);
#if ENABLE_COLOR
            std::fprintf(out, "%s[%02d:%02d:%02d %03d] [%s:%d] [%s]\033[0m ",
                         LEVEL_COLORS[idx], tm->tm_hour, tm->tm_min, tm->tm_sec, millis,
                         filePath, line, LEVEL_NAMES[idx]);
#else
            std::fprintf(out, "[%02d:%02d:%02d %03d] [%s:%d] [%s] ",
                         tm->tm_hour, tm->tm_min, tm->tm_sec, millis,
                         filePath, line, LEVEL_NAMES[idx]);
#endif
            std::vfprintf(out, fmt, args);
        }

        va_end(args);
        std::fprintf(out, "\n");

        std::fprintf(out, "  Stacktrace:\n");
        for (const auto& entry : trace)
        {
            std::fprintf(out, "    %s\n", std::to_string(entry).c_str());
        }
        std::fflush(out);
    }
} // namespace logger
