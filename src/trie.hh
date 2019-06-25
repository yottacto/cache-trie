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
        template <class U>
        snode(hash_type hash, key_type const& key, value_type const& value, U* txn)
            : hash(hash), key(key), value(value), txn(std::make_shared<U>(txn)) {}

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
        anode(int size) : values(size) {}

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
        fnode(std::shared_ptr<anode> const& an)
            : frozen(an) {}
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
        auto old = std::atomic_load(&cur->values[pos]);
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
                    if (std::atomic_compare_exchange_weak(&std::atomic_load(&u->txn), &txn, sn)) {
                        std::atomic_compare_exchange_weak(&std::atomic_load(&cur->values[pos]), &old, sn);
                        return true;
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                } else if (cur->values.size() == 4) {
                    auto ppos = (hash >> (level - 4)) & (prev->values.size() - 1);
                    std::shared_ptr<base_node> en{std::make_shared<enode>(prev, ppos, cur, hash, level)};
                    if (std::atomic_compare_exchange_weak(&std::atomic_load(&prev->values[ppos]), &cur, en)) {
                        complete_expansion(en);
                        auto wide = std::atomic_load(en->wide);
                        return insert(key, value, hash, level, wide, prev);
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                } else {
                    std::shared_ptr<base_node> sn{std::make_shared<snode>(hash, key, value, new notxn)};
                    auto an = create_anode(old, sn, level + 4);
                    if (std::atomic_compare_exchange_weak(&std::atomic_load(&u->txn), &txn, an)) {
                        std::atomic_compare_exchange_weak(&std::atomic_load(&cur->values[pos], &old, an));
                        return true;
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                }
            } else if (std::dynamic_pointer_cast<fsnode>(txn)) {
                return false;
            } else {
                std::atomic_compare_exchange_weak(&std::atomic_load(&cur->values[pos]), &old, txn);
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

    void complete_expansion(std::shared_ptr<base_node> const& en)
    {
        auto u = std::dynamic_pointer_cast<enode>(en);
        freeze(u->narrow);
        std::shared_ptr<base_node> wide{std::make_shared<anode>(16)};
        // TODO copy?
        wide->values[u->level] = u->narrow;
        std::shared_ptr<anode> empty;
        auto uwide = std::dynamic_pointer_cast<anode>(wide);
        if (!std::atomic_compare_exchange_weak(&std::atomic_load(&u->wide), &empty, uwide))
            // FIXME ?
            wide = std::atomic_load(&u->wide);
        // FIXME atomic_load?
        std::atomic_compare_exchange_weak(&u->parent->values[u->parent_pos], &en, wide);
    }

    void freeze(std::shared_ptr<anode> const& cur)
    {
        auto i = 0;
        while (i < cur->values.size()) {
            // FIXME atomic_load?
            auto node = std::atomic_load(&cur->values[i]);
            if (!node) {
                std::shared_ptr<base_node> fvn = std::make_shared<fvnode>();
                if (!std::atomic_compare_exchange_weak(&std::atomic_load(&cur->values[i]), &node, fvn))
                    i -= 1;
            } else if (auto u = std::dynamic_pointer_cast<snode>(node); u) {
                auto txn = std::atomic_load(&u->txn);
                if (std::dynamic_pointer_cast<notxn>(txn)) {
                    std::shared_ptr<base_node> fsn = std::make_shared<fsnode>();
                    if (!std::atomic_compare_exchange_weak(&std::atomic_load(&u->txn), &txn, fsn))
                        i -= 1;
                } else if (!std::dynamic_pointer_cast<fsnode>(txn)) {
                    std::atomic_compare_exchange_weak(&std::atomic_load(&cur->values[i]), &node, txn);
                    i -= 1;
                }
            } else if (auto au = std::dynamic_pointer_cast<anode>(node); au) {
                std::shared_ptr<base_node> fn{std::make_shared<fnode>(au)};
                std::atomic_compare_exchange_weak(&std::atomic_load(&cur->values[i]), &node, fn);
                i -= 1;
            } else if (auto fu = std::dynamic_pointer_cast<fnode>(node); fu) {
                freeze(fu->frozen);
            } else if (std::dynamic_pointer_cast<enode>(node)) {
                complete_expansion(node);
                i -= 1;
            }
            i += 1;
        }
    }

    std::shared_ptr<anode> root;
};

} // namespace concurrent

