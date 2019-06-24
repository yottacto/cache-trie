#pragma once
#include <optional>
#include <atomic>
#include <any>
#include "base.hh"

namespace concurrent
{

template <class T>
auto make_optional(T* v) -> std::optional<T>
{
    if (v)
        return *v;
    return {};
}

template <class Key, class T>
struct trie
{
    using key_type   = Key;
    using value_type = T;
    using hash_type  = int;

    std::optional<value_type> lookup(key_type const& key, hash_type hash, int level, anode* cur)
    {
        auto pos = (hash >> level) & (cur->value.size() - 1);
        // TODO: maybe put fense here
        auto old = make_optional(cur->value[pos]);
        if (!old.has_value() || dynamic_cast<fvnode*>(&*old))
            return {};
        if (auto u = dynamic_cast<anode*>(&*old); u)
            return lookup(key, hash, level + 4, old);
        if (auto u = dynamic_cast<snode<Key, T>*>(&*old); u) {
            if (u->key == key)
                return u->value;
            else
                return {};
        }
        if (auto u = dynamic_cast<enode*>(&*old); u) {
            auto an = u->narrow;
            return lookup(key, hash, level + 4, an);
        }
        if (auto u = dynamic_cast<fnode*>(&*old); u)
            return lookup(key, hash, level + 4, u->frozen);
    }

    bool insert(key_type const& key, hash_type hash, value_type const& value, int levle, anode* cur, anode* prev)
    {
    }

    void insert(key_type const& key, hash_type hash, value_type const& value)
    {
        if (!insert(key, value, hash, 0, root, nullptr))
            insert(key, hash, value);
    }

    anode* root;
};

} // namespace concurrent

