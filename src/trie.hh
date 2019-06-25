#pragma once
#include <vector>
#include <memory>
#include <optional>
#include <atomic>
#include <any>

namespace concurrent
{

template <class Key, class T>
struct trie
{
    using key_type   = Key;
    using value_type = T;
    using hash_type  = int;

    struct base_node;

    struct snode : base_node
    {
        template <class T>
        base_node(hash_type hash, key_type const& key, value_type const& value, T* txn)
            : hash(hash), key(key), value(value), txn(std::make_shared<T>(txn)) {}

        hash_type hash;
        key_type key;
        value_type value;
        std::shared_ptr<base_node> txn;
    };

    struct notxn : base_node
    {
    };

    struct anode : base_node
    {
        std::vector<std::shared_ptr<base_node>> values;
    };

    struct fsnode : base_node
    {
    };

    struct fvnode : base_node
    {
    };

    struct fnode : base_node
    {
        // FIXME
        std::shared_ptr<anode> frozen;
    };

    struct enode : base_node
    {
        // TODO
        enode() {}

        std::shared_ptr<anode> parent;
        int parent_pos;
        std::shared_ptr<anode> narrow;
        hash_type hash;
        std::shared_ptr<anode> wide;
        int level;
    };


    std::optional<value_type> lookup(key_type const& key, hash_type hash, int level, std::shared_ptr<anode> const& cur)
    {
        auto pos = (hash >> level) & ((cur->values).size() - 1);
        // TODO: maybe put fense here
        auto old = std::atomic_load(&cur->values[pos]);
        if (!old || std::dynamic_pointer_cast<fvnode>(old))
            return {};
        if (auto u = std::dynamic_pointer_cast<anode>(old); u)
            return lookup(key, hash, level + 4, u);
        if (auto u = std::dynamic_pointer_cast<snode>(old); u) {
            if (u->key == key)
                return u->value;
            else
                return {};
        }
        if (auto u = std::dynamic_pointer_cast<enode>(old); u)
            return lookup(key, hash, level + 4, u->narrow);
        if (auto u = std::dynamic_pointer_cast<fnode>(old); u)
            return lookup(key, hash, level + 4, u->frozen);
    }

    bool insert(key_type const& key, value_type const& value, hash_type hash, int level, std::shared_ptr<anode> const& cur, std::shared_ptr<anode> const& prev)
    {
        auto pos = (hash >> level) & ((cur->values).size() - 1);
        auto old = sdt::atomic_load(&cur->values[pos]);
        if (!old) {
            std::shared_ptr<base_node> sn{std::make_shared<snode>(hash, key, value, new notxn)};
            if (!std::atomic_compare_exchange_weak(&std::atomic_load(&cur->values[pos], &old, sn)))
                return true;
            else
                return insert(key, value, hash, level, cur, prev);
        }
        if (auto u = std::dynamic_pointer_cast<anode>(old); u)
            return insert(key, value, hash, level + 4, old, cur);
        if (auto u = std::dynamic_pointer_cast<snode>(old); u) {
            auto txn = std::atomic_load(&u->txn);
            if (std::dynamic_pointer_cast<notxn>(txn)) {
                if (old->key == key) {
                    std::shared_ptr<base_node> sn{std::make_shared<snode>(hash, key, value, 0)};
                    if (std::atomic_compare_exchange_weak(&u->txn, &txn, sn)) {
                        std::atomic_compare_exchange_weak(&cur->values[pos], &old, sn);
                        return true;
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                } else if (cur->values.size() == 4) {
                    auto ppos = (h >> (level - 4)) & (prev->values.size() - 1);
                    std::shared_ptr<base_node> en{std::make_shared<enode>(prev, ppos, cur, hash, level)};
                    if (std::atomic_compare_exchange_weak(&prev->values[ppos], &cur, en)) {
                        complete_expansion(en);
                        auto wide = std::atomic_load(en->wide);
                        return insert(key, value, hash, level, wide, prev);
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                } else {
                    std::shared_ptr<base_node> sn{std::make_shared<snode>(hash, key, value, new notxn)};
                    auto an = create_anode(old, sn, level + 4);
                    // TODO
                }
            } else if (txn == 1) {
                // TODO
            } else {
                std::atomic_compare_exchange_weak(&cur->values[pos], &old, txn);
                return insert(key, value, hash, level, cur, prev);
            }
        }
        if (auto u = std::dynamic_pointer_cast<enode>(old); u)
            complete_expansion(old);
        return false;
    }

    void insert(key_type const& key, value_type const& value, hash_type hash)
    {
        if (!insert(key, value, hash, 0, root, nullptr))
            insert(key, value, hash);
    }

    std::shared_ptr<anode> root;
};

} // namespace concurrent

