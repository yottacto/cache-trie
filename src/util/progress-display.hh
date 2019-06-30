#pragma once
#include <algorithm>
#include <iomanip>
#include <cstddef>

namespace util
{

struct progress_display
{
    using size_type = std::size_t;

    explicit progress_display(size_type total, size_type len = 60, size_type count = 0)
        : total{total}, len{len}, count{count} {}

    void tick() { count = std::min(total, count + 1); }
    void tick(size_t c) { count = std::min(total, count + c); }

    template <class Stream>
    void display(Stream& os) const
    {
        os << "[";
        auto progress = static_cast<double>(count) / static_cast<double>(total);
        size_type pos = len * progress;
        os << std::string(pos, '#') << std::string(len - pos, '-') << "]";
        os << std::setw(4) << static_cast<size_type>(progress * 100.) << "%\r";
        os.flush();
    }

    void reset()
    {
        count = 0;
    }

    size_type total;
    size_type len;
    size_type count;
};

} // namespace util

