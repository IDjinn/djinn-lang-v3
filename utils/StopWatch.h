//
// Created by Luke on 29/01/2026.
//

#ifndef DJINN_STOPWATCH_H
#define DJINN_STOPWATCH_H

#include <chrono>
#include <string>
#include "Logger.h"

namespace utils {
    class StopWatch {
        using clock = std::chrono::high_resolution_clock;
        using TimePoint = clock::time_point;

        std::string _label;
        TimePoint _start;

    public:
        explicit StopWatch(std::string label) : _label(std::move(label)), _start(clock::now()) {
        }

        ~StopWatch() {
            dump();
        }

        void dump() const {
            const auto end = clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - _start).count();
            LOG_DEBUG("%s took %lldms", _label.c_str(), ms);
        }

        template<typename Duration = std::chrono::milliseconds>
        Duration elapsed() const {
            return std::chrono::duration_cast<Duration>(clock::now() - _start).count();
        }

        void reset() {
            _start = clock::now();
        }
    };
} // namespace utils

#define STOPWATCH(label) utils::StopWatch _sw_##__LINE__(label)

#endif //DJINN_STOPWATCH_H