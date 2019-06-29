#pragma once
#include <chrono>

namespace util
{

struct timer
{
    // and other clock e.g. system_clock, steady_clock
    using clock_type = std::chrono::high_resolution_clock;
    using time_point_type = std::chrono::time_point<clock_type>;
    using time_type = double;
    using elapsed_type = time_type;
    using duration_type = std::chrono::duration<elapsed_type>;

    void start() { _start = clock_type::now(); }

    void stop()
    {
        _end = clock_type::now();
        duration_type elapsed = _end - _start;
        tot += elapsed.count();
    }

    void reset()
    {
        tot = 0;
    }

    elapsed_type elapsed_seconds() const
    {
        return tot;
    }

    elapsed_type elapsed_milliseconds() const
    {
        return tot * 1000.;
    }

private:
    time_point_type _start;
    time_point_type _end;
    elapsed_type tot{0};
};

} // namespace util

