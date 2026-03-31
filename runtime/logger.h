//
// Created by lucas on 07/03/2026.
//
#pragma once

#ifndef DJINN_LOGGER_H
#define DJINN_LOGGER_H

#include <stdio.h>

typedef enum
{
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} LogLevel;

typedef struct
{
    FILE* output;
    LogLevel level;
    const char* name;
} Logger;

Logger* logger_create(const char* name, int output, LogLevel level);

void logger_set_level(Logger* logger, LogLevel level);

void logger_trace(Logger* logger, const char* fmt, ...);
void logger_debug(Logger* logger, const char* fmt, ...);
void logger_info(Logger* logger, const char* fmt, ...);
void logger_warn(Logger* logger, const char* fmt, ...);
void logger_error(Logger* logger, const char* fmt, ...);
void logger_fatal(Logger* logger, const char* fmt, ...);

void logger_destroy(Logger* logger);

#endif //DJINN_LOGGER_H