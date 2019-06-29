#include <iostream>
#include <algorithm>
#include <numeric>
#include "../util/bench.hh"
#include "raw-pointer-trie.hh"
#include "raw-pointer-trie-without-vector.hh"
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

    auto constexpr size = 2'100'000;

    unordered_map um;
    util::bench_insert(um, size, 4, "unordered_map");

    sequential::raw_trie_no_vec<int, int> raw_t_no_vec;
    util::bench_insert(raw_t_no_vec, size, 4, "raw trie without vector");

    sequential::raw_trie<int, int> raw_t;
    util::bench_insert(raw_t, size, 4, "raw trie");

    sequential::trie<int, int> t;
    util::bench_insert(t, size, 4, "shared_ptr trie");
}

