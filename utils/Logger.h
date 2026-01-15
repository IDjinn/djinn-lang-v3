//
// Created by Luke on 07/01/2026.
//

#ifndef DJINN_LOGGER_H
#define DJINN_LOGGER_H

enum class LoggerLevel {
    TRACE,
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

#define LOGGER_LEVEL LoggerLevel::TRACE

namespace logger {
#define LOG_ENABLED(level) LOGGER_LEVEL > level
#define TRACE(message, args) LOG_ENABLED(LoggerLevel::INFO) &&std::printf(message, args);
#define DEBUG(message, args) LOG_ENABLED(LoggerLevel::DEBUG) &&std::printf(message, args);
#define INFO(message, args)  LOG_ENABLED(LoggerLevel::INFO) &&std::printf(message, args);
#define WARN(message, args)  LOG_ENABLED(LoggerLevel::WARNING) &&std::printf(message, args);
#define ERROR(message, args) LOG_ENABLED(LoggerLevel::ERROR) &&std::printf(message, args);
}

#endif //DJINN_LOGGER_H