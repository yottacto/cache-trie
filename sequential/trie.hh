#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <optional>

namespace sequential
{

template <class Key, class T>
struct trie
{
    using key_type   = Key;
    using value_type = T;
    using hash_type  = int;

    struct node
    {
        node(hash_type hash, key_type const& key, value_type const& value)
            : hash(hash), key(key), value(value) {}

        node(int size) : values(size) {}

        auto is_leaf() const { return values.empty(); }

        hash_type hash;
        key_type key;
        value_type value;
        std::vector<std::shared_ptr<node>> values;
    };

    auto lookup(
        key_type const& key,
        hash_type hash,
        int level,
        std::shared_ptr<node> const& cur
    ) const -> std::optional<value_type>
    {
        auto pos = (hash >> level) & (cur->values.size() - 1);
        auto u = cur->values[pos];
        if (!u) return {};
        if (!u->is_leaf()) {
            return lookup(key, hash, level + 4, u);
        } else {
            if (u->key == key)
                return u->value;
            else
                return {};
        }
    }

    void insert(
        key_type const& key,
        value_type const& value,
        hash_type hash,
        int level,
        std::shared_ptr<node> const& cur,
        std::shared_ptr<node> const& prev
    )
    {
        // std::cerr << "inserting: hash=" << hash << ", level=" << level << "\n";
        auto pos = (hash >> level) & ((cur->values).size() - 1);
        auto u = cur->values[pos];
        if (!u) {
            auto v{std::make_shared<node>(hash, key, value)};
            cur->values[pos] = v;
        } else if (!u->is_leaf()) {
            insert(key, value, hash, level + 4, u, cur);
        } else {
            if (u->key == key) {
                cur->values[pos] = std::make_shared<node>(hash, key, value);
            } else if (cur->values.size() == 4) {
                auto ppos = (hash >> (level - 4)) & (prev->values.size() - 1);
                complete_expansion(prev, ppos, cur, level);
                insert(key, value, hash, level, prev->values[ppos], prev);
            } else {
                auto sn{std::make_shared<node>(hash, key, value)};
                auto an = create_anode(u, sn, level + 4);
                cur->values[pos] = an;
            }
        }
    }

    void insert(key_type const& key, value_type const& value, hash_type hash)
    {
        insert(key, value, hash, 0, std::atomic_load(&root), nullptr);
    }

    // TODO key_type = value_type = hash_type
    void debug_insert(hash_type hash)
    {
        insert(hash, hash, hash);
    }

    // TODO key_type = value_type = hash_type
    void debug_lookup(hash_type hash) const
    {
        std::cout << "lookup[" << hash << "] = ";
        auto res = lookup(hash, hash, 0, root);
        if (res)
            std::cout << *res << "\n";
        else
            std::cout << "null\n";
    }

    void sequential_insert(
        std::shared_ptr<node> const& sn,
        std::shared_ptr<node> const& wide,
        int level
    )
    {
        auto mask = wide->values.size() - 1;
        auto pos = (sn->hash >> level) & mask;
        if (!wide->values[pos])
            wide->values[pos] = sn;
        else
            sequential_insert(sn, wide, level, pos);
    }

    void sequential_insert(
        std::shared_ptr<node> const& sn,
        std::shared_ptr<node> const& wide,
        int level,
        int pos
    )
    {
        auto u = wide->values[pos];
        if (u->is_leaf()) {
            auto an = create_anode(sn, u, level + 4);
            wide->values[pos] = an;
        } else {
            auto mask = u->values.size() - 1;
            auto npos = (sn->hash >> (level + 4)) & mask;
            if (!u->values[npos]) {
                u->values[npos] = sn;
            } else if (u->values.size() == 4) {
                auto an{std::make_shared<node>(16)};
                sequential_transfer(u, an, level + 4);
                wide->values[pos] = an;
                sequential_insert(sn, wide, level, pos);
            } else {
                sequential_insert(sn, u, level + 4, npos);
            }
        }
    }

    void sequential_transfer(
        std::shared_ptr<node> const& source,
        std::shared_ptr<node> const& wide,
        int level
    )
    {
        auto mask = wide->values.size() - 1;
        auto i = 0;
        while (i < source->values.size()) {
            auto _node = source->values[i];
            if (!_node) {
                // skip empty node
            } else if (_node->is_leaf()) {
                auto sn{std::make_shared<node>(
                    _node->hash,
                    _node->key,
                    _node->value
                )};
                auto pos = (_node->hash >> level) & mask;
                if (!wide->values[pos])
                    wide->values[pos] = sn;
                else
                    sequential_insert(sn, wide, level, pos);
            } else {
                sequential_transfer(_node, wide, level);
            }
            i += 1;
        }
    }

    auto create_anode(
        std::shared_ptr<node> const& sn1,
        std::shared_ptr<node> const& sn2,
        int level
    ) -> std::shared_ptr<node>
    {
        auto hash1 = sn1->hash;
        auto hash2 = sn2->hash;
        if (hash1 == hash2) {
            // TODO not dealing with same hash yet
            // TODO throw error now
            return {};
        } else {
            auto pos1 = (hash1 >> level) & (4 - 1);
            auto pos2 = (hash2 >> level) & (4 - 1);
            if (pos1 != pos2) {
                auto an{std::make_shared<node>(4)};
                an->values[pos1] = sn1;
                an->values[pos2] = sn2;
                return an;
            } else {
                auto an{std::make_shared<node>(16)};
                sequential_insert(sn1, an, level);
                sequential_insert(sn2, an, level);
                return an;
            }
        }
    }

    void complete_expansion(
        std::shared_ptr<node> const& prev,
        int ppos,
        std::shared_ptr<node> const& cur,
        int level
    )
    {
        auto wide{std::make_shared<node>(16)};
        sequential_transfer(cur, wide, level);
        prev->values[ppos] = wide;
    }

    void print_prefix(std::string const& prefix) const
    {
        if (prefix.empty())
            return;

        int n = prefix.size();
        for (auto i = 0; i < n - 1; i++)
            std::cout << (prefix[i] == ' ' ? " " : "│") << "   ";
        if (prefix.back() == ' ')
            std::cout << "└── ";
        else
            std::cout << "├── ";
    }

    void print_node(std::shared_ptr<node> const& u) const
    {
        if (!u) {
            std::cout << "(empty)\n";
        } else if (!u->is_leaf()) {
            std::cout << "(anode, size=" << u->values.size() << ")\n";
        } else {
            std::cout << "(snode, value=" << u->value << ")\n";
        }
    }

    void print(std::shared_ptr<node> const& u, std::string const& prefix) const
    {
        print_prefix(prefix);
        print_node(u);
        if (u && !u->is_leaf()) {
            auto n = u->values.size();
            for (auto i = 0u; i < n; i++)
                print(u->values[i], prefix + (i == n - 1 ? ' ' : '|'));
        }
    }

    void print() const
    {
        print(root, {});
    }

    std::shared_ptr<node> root{std::make_shared<node>(16)};
};

} // namespace concurrent

