#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <atomic>
#include <any>

namespace concurrent
{

enum class node
{
    base,
    anode,
    snode,
    notxn,
    fsnode,
    fvnode,
    fnode,
    enode,
};

template <class Key, class T>
struct trie
{
    using key_type   = Key;
    using value_type = T;
    using hash_type  = int;

    struct base_node
    {
        virtual auto type() const -> node { return node::base; }
    };

    struct notxn : base_node
    {
        auto type() const -> node override { return node::notxn; }
    };

    struct snode : base_node
    {
        snode(hash_type hash, key_type const& key, value_type const& value)
            : hash(hash), key(key), value(value), txn(std::make_shared<notxn>()) {}

        auto type() const -> node override { return node::snode; }

        hash_type hash;
        key_type key;
        value_type value;
        std::shared_ptr<base_node> txn;
    };

    // TODO narrow (4) or wide (16) array. we can maintain an extra counter to
    // count non empty node.
    struct anode : base_node
    {
        anode(int size) : values(size) {}

        auto type() const -> node override { return node::anode; }

        std::vector<std::shared_ptr<base_node>> values;
    };

    struct fsnode : base_node
    {
        auto type() const -> node override { return node::fsnode; }
    };

    struct fvnode : base_node
    {
        auto type() const -> node override { return node::fvnode; }
    };

    struct fnode : base_node
    {
        fnode(std::shared_ptr<anode> const& an)
            : frozen(an) {}

        auto type() const -> node override { return node::fnode; }

        std::shared_ptr<anode> frozen;
    };

    struct enode : base_node
    {
        enode(
            std::shared_ptr<anode> const& parent,
            int parent_pos,
            std::shared_ptr<anode> const& narrow,
            hash_type hash,
            int level
        ) : parent(parent), parent_pos(parent_pos), narrow(narrow), hash(hash), level(level)
        {
        }

        auto type() const -> node override { return node::enode; }

        std::shared_ptr<anode> parent;
        int parent_pos;
        std::shared_ptr<anode> narrow;
        hash_type hash;
        std::shared_ptr<anode> wide;
        int level;
    };


    auto lookup(
        key_type const& key,
        hash_type hash,
        int level,
        std::shared_ptr<anode> const& cur
    ) -> std::optional<value_type>
    {
        auto pos = (hash >> level) & ((cur->values).size() - 1);
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

    auto insert(
        key_type const& key,
        value_type const& value,
        hash_type hash,
        int level,
        std::shared_ptr<anode> const& cur,
        std::shared_ptr<anode> const& prev
    ) -> bool
    {
        auto pos = (hash >> level) & ((cur->values).size() - 1);
        auto old = std::atomic_load(&cur->values[pos]);
        if (!old) {
            std::shared_ptr<base_node> sn{std::make_shared<snode>(hash, key, value)};
            auto obj = std::atomic_load(&cur->values[pos]);
            if (!std::atomic_compare_exchange_weak(&obj, &old, sn))
                return true;
            else
                return insert(key, value, hash, level, cur, prev);
        } else if (old->type() == node::anode) {
            auto anold = std::static_pointer_cast<anode>(old);
            return insert(key, value, hash, level + 4, anold, cur);
        } else if (old->type() == node::snode) {
            // TODO dynamic_pointer_cast here?
            auto u = std::static_pointer_cast<snode>(old);
            auto txn = std::atomic_load(&u->txn);
            if (txn->type() == node::notxn) {
                if (u->key == key) {
                    std::shared_ptr<base_node> sn{std::make_shared<snode>(hash, key, value)};
                    auto obj = std::atomic_load(&u->txn);
                    if (std::atomic_compare_exchange_weak(&obj, &txn, sn)) {
                        auto obj = std::atomic_load(&cur->values[pos]);
                        std::atomic_compare_exchange_weak(&obj, &old, sn);
                        return true;
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                } else if (cur->values.size() == 4) {
                    auto ppos = (hash >> (level - 4)) & (prev->values.size() - 1);
                    std::shared_ptr<base_node> en{std::make_shared<enode>(prev, ppos, cur, hash, level)};
                    auto uen = std::static_pointer_cast<enode>(en);
                    auto obj = std::atomic_load(&prev->values[ppos]);
                    auto bcur = std::static_pointer_cast<base_node>(cur);
                    if (std::atomic_compare_exchange_weak(&obj, &bcur, en)) {
                        // TODO atomic_load en?
                        complete_expansion(en);
                        auto wide = std::atomic_load(&uen->wide);
                        return insert(key, value, hash, level, wide, prev);
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                } else {
                    std::shared_ptr<base_node> sn{std::make_shared<snode>(hash, key, value)};
                    auto an = create_anode(old, sn, level + 4);
                    auto obj = std::atomic_load(&u->txn);
                    if (std::atomic_compare_exchange_weak(&obj, &txn, an)) {
                        auto obj = std::atomic_load(&cur->values[pos]);
                        std::atomic_compare_exchange_weak(&obj, &old, an);
                        return true;
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                }
            } else if (txn->type() == node::fsnode) {
                return false;
            } else {
                auto obj = std::atomic_load(&cur->values[pos]);
                std::atomic_compare_exchange_weak(&obj, &old, txn);
                return insert(key, value, hash, level, cur, prev);
            }
        } else if (old->type() == node::enode) {
            complete_expansion(old);
        }
        return false;
    }

    void insert(key_type const& key, value_type const& value, hash_type hash)
    {
        if (!insert(key, value, hash, 0, std::atomic_load(&root), nullptr))
            insert(key, value, hash);
    }

    void sequential_insert(
        std::shared_ptr<snode> const& sn,
        std::shared_ptr<anode> const& wide,
        int level
    )
    {
        // FIXME later, the naming of wide
        auto mask = wide->values.size() - 1;
        auto pos = (sn->hash >> level) & mask;
        if (!wide->values[pos])
            wide->values[pos] = sn;
        else
            sequential_insert(sn, wide, level, pos);
    }

    void sequential_insert(
        std::shared_ptr<snode> const& sn,
        std::shared_ptr<anode> const& wide,
        int level,
        int pos
    )
    {
        auto old = wide->values[pos];
        if (old->type() == node::snode) {
            auto oldsn = std::static_pointer_cast<snode>(old);
            auto an = create_anode(sn, oldsn, level + 4);
            wide->values[pos] = an;
        } else if (old->type() == node::anode) {
            auto oldan = std::static_pointer_cast<anode>(old);
            auto mask = oldan->values.size() - 1;
            auto npos = (sn->hash >> (level + 4)) & mask;
            if (!oldan->values[npos]) {
                oldan->values[npos] = sn;
            } else if (oldan->values.size() == 4) {
                std::shared_ptr<base_node> an{std::make_shared<anode>(16)};
                auto uan = std::static_pointer_cast<anode>(an);
                sequential_transfer(oldan, uan, level + 4);
                wide->values[pos] = an;
                sequential_insert(sn, wide, level, pos);
            } else {
                sequential_insert(sn, oldan, level + 4, npos);
            }
        }
    }

    void sequential_transfer(
        std::shared_ptr<anode> const& source,
        std::shared_ptr<anode> const& wide,
        int level
    )
    {
        auto mask = wide->values.size() - 1;
        auto i = 0;
        while (i < source->values.size()) {
            auto _node = source->values[i];
            // TODO we leave lnode here (for same key)
            if (_node->type() == node::fvnode) {
                // TODO we can skip, the slot was empty?
            } else if (is_frozen_snode(_node)) {
                auto oldsn = std::static_pointer_cast<snode>(_node);
                std::shared_ptr<base_node> sn{std::make_shared<snode>(
                    oldsn->hash,
                    oldsn->key,
                    oldsn->value
                )};
                auto usn = std::static_pointer_cast<snode>(sn);
                auto pos = (usn->hash >> level) & mask;
                if (!wide->values[pos])
                    wide->values[pos] = sn;
                else
                    sequential_insert(usn, wide, level, pos);
            } else if (_node->type() == node::fnode) {
                auto fn = std::static_pointer_cast<fnode>(_node);
                auto an = std::static_pointer_cast<anode>(fn->frozen);
                sequential_transfer(an, wide, level);
            } else {
                // TODO throw an error, source array node should have been
                // frozen.
            }
            i += 1;
        }
    }

    auto is_frozen_snode(std::shared_ptr<base_node> const& node)
    {
        if (node->type() == node::snode) {
            auto sn = std::static_pointer_cast<snode>(node);
            auto txn = std::atomic_load(&sn->txn);
            return txn->type() == node::fsnode;
        } else {
            return false;
        }
    }

    // TODO is it sequential?
    auto create_anode(
        std::shared_ptr<base_node> const& sn1,
        std::shared_ptr<base_node> const& sn2,
        int level
    ) -> std::shared_ptr<base_node>
    {
        auto usn1 = std::static_pointer_cast<snode>(sn1);
        auto usn2 = std::static_pointer_cast<snode>(sn2);
        auto hash1 = usn1->hash;
        auto hash2 = usn2->hash;
        if (hash1 == hash2) {
            // TODO not dealing with same hash yet
            // TODO throw error now
            return {};
        } else {
            auto pos1 = (hash1 >> level) & (4 - 1);
            auto pos2 = (hash2 >> level) & (4 - 1);
            if (pos1 != pos2) {
                std::shared_ptr<base_node> an{std::make_shared<anode>(4)};
                auto uan = std::static_pointer_cast<anode>(an);
                uan->values[pos1] = sn1;
                uan->values[pos2] = sn2;
                return an;
            } else {
                std::shared_ptr<base_node> an{std::make_shared<anode>(16)};
                auto uan = std::static_pointer_cast<anode>(an);
                sequential_insert(std::static_pointer_cast<snode>(sn1), uan, level);
                sequential_insert(std::static_pointer_cast<snode>(sn2), uan, level);
                return an;
            }
        }
    }

    void complete_expansion(std::shared_ptr<base_node> const& en)
    {
        // TODO static_cast here?
        auto u = std::static_pointer_cast<enode>(en);
        // TODO need atomic_load?
        freeze(std::atomic_load(&u->narrow));
        std::shared_ptr<base_node> wide{std::make_shared<anode>(16)};
        auto awide = std::static_pointer_cast<anode>(wide);
        // TODO atomic_load?
        awide->values[u->level] = std::atomic_load(&u->narrow);
        std::shared_ptr<anode> empty;
        // TODO maybe create the dereived first for better performance
        auto uwide = std::dynamic_pointer_cast<anode>(wide);
        auto obj = std::atomic_load(&u->wide);
        if (!std::atomic_compare_exchange_weak(&obj, &empty, uwide))
            // FIXME ?
            wide = std::atomic_load(&u->wide);
        // FIXME atomic_load?
        auto tobj = std::atomic_load(&u->parent->values[u->parent_pos]);
        auto ben = std::static_pointer_cast<base_node>(en);
        std::atomic_compare_exchange_weak(&tobj, &ben, wide);
    }

    void freeze(std::shared_ptr<anode> const& cur)
    {
        auto i = 0;
        while (i < cur->values.size()) {
            auto _node = std::atomic_load(&cur->values[i]);
            if (!_node) {
                std::shared_ptr<base_node> fvn = std::make_shared<fvnode>();
                auto obj = std::atomic_load(&cur->values[i]);
                if (!std::atomic_compare_exchange_weak(&obj, &_node, fvn))
                    i -= 1;
            } else if (_node->type() == node::snode) {
                auto u = std::static_pointer_cast<snode>(_node);
                auto txn = std::atomic_load(&u->txn);
                if (txn->type() == node::notxn) {
                    std::shared_ptr<base_node> fsn = std::make_shared<fsnode>();
                    auto obj = std::atomic_load(&u->txn);
                    if (!std::atomic_compare_exchange_weak(&obj, &txn, fsn))
                        i -= 1;
                } else if (txn->type() != node::fsnode) {
                    // TODO not fully understood.
                    // explain: copy txn to cur[i] and do another iteration to
                    // help commit the changes first.
                    auto obj = std::atomic_load(&cur->values[i]);
                    std::atomic_compare_exchange_weak(&obj, &_node, txn);
                    i -= 1;
                }
            } else if (_node->type() == node::anode) {
                auto u = std::static_pointer_cast<anode>(_node);
                std::shared_ptr<base_node> fn{std::make_shared<fnode>(u)};
                auto obj = std::atomic_load(&cur->values[i]);
                std::atomic_compare_exchange_weak(&obj, &_node, fn);
                i -= 1;
            } else if (_node->type() == node::fnode) {
                auto u = std::static_pointer_cast<fnode>(_node);
                freeze(std::atomic_load(&u->frozen));
            } else if (_node->type() == node::enode) {
                complete_expansion(_node);
                i -= 1;
            }
            i += 1;
        }
    }

    void print_prefix(std::string const& prefix) const
    {
        int n = prefix.size();
        for (auto i = 0; i < n - 1; i++)
            std::cout << prefix[i] << "   ";
        if (prefix.back() == ' ')
            std::cout << "└── ";
        else
            std::cout << "├── ";
    }

    void print_node(std::shared_ptr<base_node> const& u) const
    {
        if (u->type() == node::base) {
            std::cout << "(base)\n";
        } else if (u->type() == node::anode) {
            auto au = std::static_pointer_cast<anode>(u);
            std::cout << "(anode, size=" << au->values.size() << ")\n";
        } else if (u->type() == node::snode) {
            auto su = std::static_pointer_cast<snode>(u);
            std::cout << "(snode, value=" << su->value << ")\n";
        } else if (u->type() == node::notxn) {
            std::cout << "(notxn)\n";
        } else if (u->type() == node::fsnode) {
            std::cout << "(fsnode)\n";
        } else if (u->type() == node::fvnode) {
            std::cout << "(fvnode)\n";
        } else if (u->type() == node::fnode) {
            std::cout << "(fnode)\n";
        } else if (u->type() == node::enode) {
            std::cout << "(enode)\n";
        }
    }

    void print(std::shared_ptr<base_node> const& u, std::string const& prefix) const
    {
        print_prefix(prefix);
        print_node(u);
        if (u->type() == node::anode) {
            auto au = std::static_pointer_cast<anode>(u);
            auto n = au->values.size();
            for (auto i = 0u; i < n; i++)
                print(au->values[i], prefix + (i == n - 1 ? ' ' : '|'));
        }
    }

    void print() const
    {
        print(root, {});
    }

    std::shared_ptr<anode> root;
};

} // namespace concurrent

