//
// Created by Luke on 29/01/2026.
//

#ifndef DJINN_STOPWATCH_H
#define DJINN_STOPWATCH_H

#include <chrono>
#include <string>
#include "Logger.h"
#include "../runtime/logger.h"

#define INIT_STOPWATCH_WITH_LEVEL(label, log_level) utils::StopWatch((label), __FILE__, __LINE__, (log_level))
#define INIT_STOPWATCH(label) INIT_STOPWATCH_WITH_LEVEL((label), (logger::Level::DEBUG))

namespace utils
{
    class StopWatch
    {
        using clock = std::chrono::high_resolution_clock;
        using TimePoint = clock::time_point;

        logger::Level _log_level = logger::Level::DEBUG;
        std::string file;
        uint32_t line;

        std::string _label;
        TimePoint _start;

    public:
        explicit StopWatch(std::string label, const std::string& file, const uint32_t line,
                           const logger::Level logger_level) :
            _log_level(logger_level),
            file(file),
            line(line),
            _label(std::move(label)),
            _start(clock::now())
        {
        }

        ~StopWatch()
        {
            dump();
        }

        void dump() const
        {
            const auto end = clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - _start).count();
            logger::log(_log_level, file.c_str(), static_cast<int>(line), "%s took %lldms", _label.c_str(), ms);
        }

        template <typename Duration = std::chrono::milliseconds>
        Duration elapsed() const
        {
            return std::chrono::duration_cast<Duration>(clock::now() - _start).count();
        }

        void reset()
        {
            _start = clock::now();
        }
    };
} // namespace utils

#define STOPWATCH(label) utils::StopWatch _sw_##__LINE__(label)

#endif //DJINN_STOPWATCH_H
