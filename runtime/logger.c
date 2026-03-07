#include "logger.h"
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

static const char* level_names[] = {
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

static void get_timestamp(char* buffer, size_t size)
{
    const time_t now = time(NULL);
    const struct tm* t = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", t);
}

static void log_internal(Logger* logger, LogLevel level, const char* fmt, va_list args)
{
    if (level < logger->level)
        return;

    char timebuf[32];
    get_timestamp(timebuf, sizeof(timebuf));

    fprintf(logger->output, "[%s] [%-5s] [%s] ",
            timebuf,
            level_names[level],
            logger->name);

    vfprintf(logger->output, fmt, args);

    fprintf(logger->output, "\n");
    fflush(logger->output);
}

Logger* logger_create(const char* name, int output, LogLevel level)
{
    Logger* logger = malloc(sizeof(Logger));

    logger->name = name;
    logger->output = (output == 2) ? stderr : stdout;
    logger->level = level;

    return logger;
}

void logger_set_level(Logger* logger, LogLevel level)
{
    logger->level = level;
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
