#include <iostream>
#include <algorithm>
#include <numeric>
#include "../util/bench.hh"
#include "raw-pointer-trie.hh"
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
    // map m;
    // util::bench_insert(m, 1'100'000, 10);

    auto constexpr size = 1'100'000;

    util::bench_insert<unordered_map>(
        size, 4, "unordered_map"
    );

    util::bench_insert<sequential::raw_trie<int, int>>(
        size, 4, "raw trie"
    );

    util::bench_insert<sequential::raw_trie_mem_pool<int, int, size>>(
        size, 4, "raw trie with memory pool"
    );

    // util::bench_insert<sequential::trie<int, int>>(
    //     size, 4, "shared_ptr trie"
    // );
}

