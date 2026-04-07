//
// Created by Luke on 29/01/2026.
//

#ifndef DJINN_STOPWATCH_H
#define DJINN_STOPWATCH_H

#include <chrono>
#include <string>
#include <vector>
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

#define STOPWATCH(label) utils::StopWatch _sw_##__LINE__(label)

    class BuildSummary
    {
        using clock = std::chrono::high_resolution_clock;

        struct Phase
        {
            std::string name;
            long long ms = 0;
        };

        std::vector<Phase> _phases;
        clock::time_point _total_start;

    public:
        BuildSummary() : _total_start(clock::now())
        {
        }

        class ScopedPhase
        {
            BuildSummary& _summary;
            size_t _index;
            clock::time_point _start;

        public:
            ScopedPhase(BuildSummary& summary, size_t index)
                : _summary(summary), _index(index), _start(clock::now())
            {
            }

            ~ScopedPhase()
            {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - _start).count();
                _summary._phases[_index].ms += elapsed;
            }
        };

        ScopedPhase phase(const std::string& name)
        {
            for (size_t i = 0; i < _phases.size(); i++)
            {
                if (_phases[i].name == name)
                    return ScopedPhase(*this, i);
            }
            _phases.push_back({name, 0});
            return ScopedPhase(*this, _phases.size() - 1);
        }

        void print() const
        {
            auto total = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - _total_start).count();

            fprintf(stdout, "+----------------------+----------+-------+\n");
            fprintf(stdout, "| %-20s | %8s | %5s |\n", "Phase", "Time", "%");
            fprintf(stdout, "+----------------------+----------+-------+\n");
            for (const auto& [name, ms] : _phases)
            {
                double pct = total > 0 ? (ms * 100.0 / total) : 0.0;
                fprintf(stdout, "| %-20s | %5lldms  | %4.1f%% |\n", name.c_str(), ms, pct);
            }
            fprintf(stdout, "+----------------------+----------+-------+\n");
            fprintf(stdout, "| %-20s | %5lldms  |       |\n", "total", total);
            fprintf(stdout, "+----------------------+----------+-------+\n");
        }
    };
} // namespace utils

#endif //DJINN_STOPWATCH_H
