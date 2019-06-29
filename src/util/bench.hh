#pragma once
#include <iostream>
#include <algorithm>
#include <numeric>
#include "timer.hh"

namespace util
{

template <class T>
auto bench_insert_onne(T& a, int size)
{
    std::vector<int> v(size);
    std::iota(v.begin(), v.end(), 0);
    std::random_shuffle(v.begin(), v.end());

    util::timer t;
    t.start();
    for (auto i : v)
        a.debug_insert(i);
    t.stop();

    return t.elapsed_milliseconds();
}

template <class T>
auto bench_insert(T& a, int size, int repeat = 20)
{
    // warmup
    double warm_sum_time = 0;
    for (auto i = 0; i < 4; i++)
        warm_sum_time += bench_insert_onne(a, size);

    std::cout << "warm avg: " << warm_sum_time/4 << "\n";

    double sum_time = 0;
    for (auto i = 0; i < repeat; i++)
        sum_time += bench_insert_onne(a, size);
    auto res = sum_time / repeat;
    std::cout << "random insert [" << size << "] elements, time "
        << res << "ms\n";
    return res;
}

} // namespace util

