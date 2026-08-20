#include "logger.h"
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "djinn_runtime.h"

static const char* level_names[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static LogLevel parse_level(const char* str)
{
    if (strcmp(str, "TRACE") == 0) return LOG_TRACE;
    if (strcmp(str, "DEBUG") == 0) return LOG_DEBUG;
    if (strcmp(str, "INFO") == 0) return LOG_INFO;
    if (strcmp(str, "WARN") == 0) return LOG_WARN;
    if (strcmp(str, "ERROR") == 0) return LOG_ERROR;
    if (strcmp(str, "FATAL") == 0) return LOG_FATAL;
    return LOG_INFO;
}

static void emit_datetime(FILE* out, const char* pattern, size_t len, const struct tm* t, int millis)
{
    size_t i = 0;
    while (i < len)
    {
        if (i + 4 <= len && strncmp(pattern + i, "YYYY", 4) == 0)
        {
            fprintf(out, "%04d", t->tm_year + 1900);
            i += 4;
        }
        else if (i + 4 <= len && strncmp(pattern + i, "mmmm", 4) == 0)
        {
            fprintf(out, "%04d", millis);
            i += 4;
        }
        else if (i + 3 <= len && strncmp(pattern + i, "mmm", 3) == 0)
        {
            fprintf(out, "%03d", millis);
            i += 3;
        }
        else if (i + 2 <= len && strncmp(pattern + i, "dd", 2) == 0)
        {
            fprintf(out, "%02d", t->tm_mday);
            i += 2;
        }
        else if (i + 2 <= len && strncmp(pattern + i, "MM", 2) == 0)
        {
            fprintf(out, "%02d", t->tm_mon + 1);
            i += 2;
        }
        else if (i + 2 <= len && strncmp(pattern + i, "YY", 2) == 0)
        {
            fprintf(out, "%02d", (t->tm_year + 1900) % 100);
            i += 2;
        }
        else if (i + 2 <= len && strncmp(pattern + i, "HH", 2) == 0)
        {
            fprintf(out, "%02d", t->tm_hour);
            i += 2;
        }
        else if (i + 2 <= len && strncmp(pattern + i, "mm", 2) == 0)
        {
            fprintf(out, "%02d", t->tm_min);
            i += 2;
        }
        else if (i + 2 <= len && strncmp(pattern + i, "ss", 2) == 0)
        {
            fprintf(out, "%02d", t->tm_sec);
            i += 2;
        }
        else
        {
            fputc(pattern[i], out);
            i++;
        }
    }
}

static int is_datetime_token(const char* s, size_t len)
{
    return (len >= 2) && (
        strstr(s, "dd") || strstr(s, "MM") || strstr(s, "YY") ||
        strstr(s, "HH") || strstr(s, "mm") || strstr(s, "ss")
    );
}

static void emit_format(FILE* out, const char* format, LogLevel level,
                        const char* name, const char* fmt, va_list args)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    struct tm t;
#ifdef _WIN32
    localtime_s(&t, &ts.tv_sec);
#else
    localtime_r(&ts.tv_sec, &t);
#endif
    int millis = (int)(ts.tv_nsec / 1000000);

    size_t i = 0;
    size_t flen = strlen(format);
    while (i < flen)
    {
        if (format[i] == '{')
        {
            size_t end = i + 1;
            while (end < flen && format[end] != '}') end++;
            if (end >= flen)
            {
                fputc('{', out);
                i++;
                continue;
            }

            size_t tlen = end - i - 1;
            const char* token = format + i + 1;

            if (tlen == 3 && strncmp(token, "...", 3) == 0)
                vfprintf(out, fmt, args);
            else if (tlen == 4 && strncmp(token, "file", 4) == 0)
                fprintf(out, "%s", name);
            else if (tlen == 5 && strncmp(token, "level", 5) == 0)
                fprintf(out, "%s", level_names[level]);
            else if (is_datetime_token(token, tlen))
            {
                char buf[64];
                if (tlen < sizeof(buf))
                {
                    memcpy(buf, token, tlen);
                    buf[tlen] = '\0';
                    emit_datetime(out, buf, tlen, &t, millis);
                }
            }
            else
            {
                fputc('{', out);
                fwrite(token, 1, tlen, out);
                fputc('}', out);
            }

            i = end + 1;
        }
        else
        {
            fputc(format[i], out);
            i++;
        }
    }
}

static void log_internal(Logger* logger, LogLevel level, const char* fmt, va_list args)
{
    if (level < logger->level)
        return;

    if (logger->format[0] != '\0')
    {
        emit_format(logger->output, logger->format, level, logger->name, fmt, args);
    }
    else
    {
        char timebuf[32];
        time_t now = time(NULL);
        struct tm t;
#ifdef _WIN32
        localtime_s(&t, &now);
#else
        localtime_r(&now, &t);
#endif
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &t);

        fprintf(logger->output, "[%s] [%-5s] [%s] ",
                timebuf, level_names[level], logger->name);
        vfprintf(logger->output, fmt, args);
    }

    fprintf(logger->output, "\n");
    fflush(logger->output);
}

Logger* logger_create(const char* name, int output, LogLevel level)
{
    Logger* logger = malloc(sizeof(Logger));
    logger->name = name;
    logger->output = (output == 2) ? stderr : stdout;
    logger->level = level;
    logger->format[0] = '\0';
    return logger;
}

void logger_configure_from_properties(Logger* logger, const char* props_path)
{
    FILE* f = NULL;
#ifdef _WIN32
    if (fopen_s(&f, props_path, "r") != 0 || !f) return;
#else
    f = fopen(props_path, "r");
    if (!f) return;
#endif

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = '\0';

        char* eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;

        if (strcmp(key, "logger.level") == 0)
            logger->level = parse_level(val);
        else if (strcmp(key, "logger.format") == 0)
            logger_set_format(logger, val);
    }

    fclose(f);
}

void logger_set_level(Logger* logger, LogLevel level)
{
    logger->level = level;
}

void logger_set_format(Logger* logger, const char* format)
{
    if (format)
    {
#ifdef _WIN32
        strncpy_s(logger->format, DJINN_LOG_FORMAT_MAX, format, _TRUNCATE);
#else
        strncpy(logger->format, format, DJINN_LOG_FORMAT_MAX - 1);
        logger->format[DJINN_LOG_FORMAT_MAX - 1] = '\0';
#endif
    }
    else
        logger->format[0] = '\0';
}

void logger_trace(Logger* logger, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_internal(logger, LOG_TRACE, fmt, args);
    va_end(args);
}

void logger_debug(Logger* logger, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_internal(logger, LOG_DEBUG, fmt, args);
    va_end(args);
}

void logger_info(Logger* logger, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_internal(logger, LOG_INFO, fmt, args);
    va_end(args);
}

void logger_warn(Logger* logger, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_internal(logger, LOG_WARN, fmt, args);
    va_end(args);
}

void logger_error(Logger* logger, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_internal(logger, LOG_ERROR, fmt, args);
    va_end(args);
}

void logger_fatal(Logger* logger, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_internal(logger, LOG_FATAL, fmt, args);
    va_end(args);
}

void logger_destroy(Logger* logger)
{
    free(logger);
}
