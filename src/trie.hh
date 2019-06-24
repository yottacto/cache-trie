#pragma once
#include <optional>
#include <atomic>
#include <any>
#include "base.hh"

namespace concurrent
{

template <class Key, class T>
struct trie
{
    using key_type   = Key;
    using value_type = T;
    using hash_type  = int;

    std::optional<value_type> lookup(key_type const& key, hash_type hash, int level, anode cur)
    {
        auto pos = (hash >> level) & (cur.size() - 1);
        auto old = cur[pos];
        if (old.has_value() || old.type() == typeid(fvnode))
            return {};
        if (old.type() == typeid(anode))
            return lookup(key, hash, level + 4, std::any_cast<std::atmoic<>>())
    }
};

} // namespace concurrent

