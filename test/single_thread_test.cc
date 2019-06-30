#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <random>
#include <array>
#include "../src/util/progress-display.hh"
#include "../src/concurrent/trie.hh"

// 0: lookup, 1: insert, 2: remove

template <int Ops>
auto generate_ops(int max = 100)
{
    std::array<std::pair<int, int>, Ops> ops;
    std::random_device rd{};
    std::mt19937 gen{rd()};
    std::uniform_int_distribution<> dis_op(0, 2);
    std::uniform_int_distribution<> dis_key(0, max);
    std::generate(ops.begin(), ops.end(), [&]() {
        return std::make_pair(dis_op(gen), dis_key(gen));
    });
    return ops;
}

template <class Array>
auto single_thread_small_once_test(Array const& ops) -> bool
{
    concurrent::trie<int, int> t;
    std::unordered_map<int, int> um;
    for (auto [i, key] : ops) {
        if (i == 0) {
            auto res = t.debug_lookup(key);
            std::optional<int> gt;
            if (um.count(key))
                gt = um.at(key);
            if (res != gt) return false;
        } else if (i == 1) {
            t.debug_insert(key);
            um[key] = key;
        } else {
            auto res = t.debug_remove(key);
            std::optional<int> gt;
            if (um.count(key))
                gt = um.at(key);
            if (res != gt)
                return false;
            um.erase(key);
        }
    }
    return true;
}

template <int Ops = 8, int Repeat = 1'000'000>
void single_thread_small_test(int max = 100)
{
    std::cout << std::string(80, '=') << "\n";
    std::cout << "testing: single_thread_small_test\n";

    util::progress_display pd(Repeat);
    for (auto i = 0; i < Repeat; i++) {
        auto ops = generate_ops<Ops>(max);
        try {
            if (!single_thread_small_once_test(ops))
                throw std::logic_error{"bad case"};
        } catch (...) {
            std::cout << "test failed.\n";
            std::cout << "bad case:\n";
            for (auto [i, key] : ops)
                std::cout << "[" << i << ", " << key << "] ";
            std::cout << "\n";
            std::cout << std::string(80, '=') << "\n";
            return;
        }
        pd.tick();
        pd.display(std::cout);
    }
    std::cout << "passed.\n";
    std::cout << std::string(80, '=') << "\n";
}


int main()
{
    single_thread_small_test<20, 100'000'000>(100);
}

