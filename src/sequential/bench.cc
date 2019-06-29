#include <iostream>
#include <algorithm>
#include <numeric>
#include "../util/bench.hh"
#include "trie.hh"

#include <unordered_map>
#include <map>

struct map
{
    void debug_insert(int x)
    {
        value[x] = x;
    }

    std::map<int, int> value;
};

struct unordered_map
{
    void debug_insert(int x)
    {
        value[x] = x;
    }

    std::unordered_map<int, int> value;
};

int main()
{
    map m;
    util::bench_insert(m, 1'100'000, 10);

    unordered_map um;
    util::bench_insert(um, 1'100'000, 10);

    sequential::trie<int, int> t;
    util::bench_insert(t, 1'100'000, 10);
}

