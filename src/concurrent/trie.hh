#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <utility>
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
    xnode,
};

std::ostream& operator<<(std::ostream& os, node const& n)
{
    std::vector<std::string> name{
        "base",
        "anode",
        "snode",
        "notxn",
        "fsnode",
        "fvnode",
        "fnode",
        "enode",
    };
    os << name[static_cast<int>(n)];
    return os;
}

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

    struct xnode : base_node
    {
        xnode(
            std::shared_ptr<anode> const& parent,
            int parent_pos,
            std::shared_ptr<anode> const& stale,
            hash_type hash,
            int level
        ) : parent(parent), parent_pos(parent_pos), stale(stale), hash(hash), level(level)
        {
        }

        auto type() const -> node override { return node::xnode; }

        std::shared_ptr<anode> parent;
        int parent_pos;
        std::shared_ptr<anode> stale;
        hash_type hash;
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
        if (!old || old->type() == node::fvnode) {
            return {};
        } else if (old->type() == node::anode) {
            auto oldan = std::static_pointer_cast<anode>(old);
            return lookup(key, hash, level + 4, oldan);
        } else if (old->type() == node::snode) {
            auto oldsn = std::static_pointer_cast<snode>(old);
            if (oldsn->key == key)
                return oldsn->value;
            else
                return {};
        } else if (old->type() == node::enode) {
            auto olden = std::static_pointer_cast<enode>(old);
            return lookup(key, hash, level + 4, olden->narrow);
        } else if (old->type() == node::fnode) {
            auto oldfn = std::static_pointer_cast<fnode>(old);
            return lookup(key, hash, level + 4, oldfn->frozen);
        }

        // else {
        //     // TODO throw error, unexpected case
        // }

        return {};
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
        // std::cerr << "inserting: hash=" << hash << ", level=" << level << "\n";
        auto pos = (hash >> level) & ((cur->values).size() - 1);
        auto old = std::atomic_load(&cur->values[pos]);
        if (!old) {
            std::shared_ptr<base_node> sn{std::make_shared<snode>(hash, key, value)};
            if (std::atomic_compare_exchange_weak(&cur->values[pos], &old, sn))
                return true;
            else
                return insert(key, value, hash, level, cur, prev);
        } else if (old->type() == node::anode) {
            auto an = std::static_pointer_cast<anode>(old);
            return insert(key, value, hash, level + 4, an, cur);
        } else if (old->type() == node::snode) {
            auto u = std::static_pointer_cast<snode>(old);
            auto txn = std::atomic_load(&u->txn);
            if (txn->type() == node::notxn) {
                if (u->key == key) {
                    std::shared_ptr<base_node> sn{std::make_shared<snode>(hash, key, value)};
                    if (std::atomic_compare_exchange_weak(&u->txn, &txn, sn)) {
                        std::atomic_compare_exchange_weak(&cur->values[pos], &old, sn);
                        return true;
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                } else if (cur->values.size() == 4) {
                    auto ppos = (hash >> (level - 4)) & (prev->values.size() - 1);
                    std::shared_ptr<base_node> en{std::make_shared<enode>(prev, ppos, cur, hash, level)};
                    auto uen = std::static_pointer_cast<enode>(en);
                    auto bcur = std::static_pointer_cast<base_node>(cur);
                    if (std::atomic_compare_exchange_weak(&prev->values[ppos], &bcur, en)) {
                        // TODO atomic_load en?
                        complete_expansion(en);
                        auto wide = std::atomic_load(&uen->wide);
                        return insert(key, value, hash, level, wide, prev);
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                } else {
                    auto an = create_anode(
                        u->hash, u->key, u->value,
                        hash, key, value,
                        level + 4
                    );
                    if (std::atomic_compare_exchange_weak(&u->txn, &txn, an)) {
                        std::atomic_compare_exchange_weak(&cur->values[pos], &old, an);
                        return true;
                    } else {
                        return insert(key, value, hash, level, cur, prev);
                    }
                }
            } else if (txn->type() == node::fsnode) {
                return false;
            } else {
                std::atomic_compare_exchange_weak(&cur->values[pos], &old, txn);
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

    auto remove(
        key_type const& key,
        hash_type hash,
        int level,
        std::shared_ptr<anode> const& cur,
        std::shared_ptr<anode> const& prev
    ) -> std::pair<bool, std::optional<value_type>>
    {
        auto mask = (cur->values.size()) - 1;
        auto pos = (hash >> level) & mask;
        auto old = std::atomic_load(&cur->values[pos]);
        if (!old) {
            return {true, {}};
        } else if (old->type() == node::anode) {
            auto oldan = std::static_pointer_cast<anode>(old);
            return remove(key, hash, level + 4, oldan, cur);
        } else if (old->type() == node::snode) {
            auto oldsn = std::static_pointer_cast<snode>(old);
            auto txn = std::atomic_load(&oldsn->txn);
            if (txn->type() == node::notxn) {
                if (oldsn->hash == hash && oldsn->key == key) {
                    std::shared_ptr<base_node> empty{nullptr};
                    if (std::atomic_compare_exchange_weak(&oldsn->txn, &txn, empty)) {
                        std::atomic_compare_exchange_weak(&cur->values[pos], &old, empty);
                        return {true, oldsn->value};
                    } else {
                        return remove(key, hash, level, cur, prev);
                    }
                } else {
                    return {true, {}};
                }
            } else if (txn->type() == node::fsnode) {
                return {false, {}};
            } else {
                std::atomic_compare_exchange_weak(&cur->values[pos], &old, txn);
                return remove(key, hash, level, cur, prev);
            }
        } else if (old->type() == node::enode) {
            complete_expansion(old);
            return {false, {}};
        } else if (old->type() == node::xnode) {
            complete_compression(old);
            return {false, {}};
        } else if (old->type() == node::fnode || old->type() == node::fvnode) {
            return {false, {}};
        }

        // else {
        //     // TODO throw error, unexpected case
        // }

        return {false, {}};
    }

    auto remove(key_type const& key, hash_type hash) -> std::optional<value_type>
    {
        auto res = remove(key, hash, 0, std::atomic_load(&root), nullptr);
        if (res.first)
            return res.second;
        else
            return remove(key, hash);
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

    void sequential_transfer_narrow(
        std::shared_ptr<anode> const& source,
        std::shared_ptr<anode> const& narrow
    )
    {
        auto i = 0;
        while (i < 4) {
            auto _node = source->values[i];
            if (_node->type() == node::fvnode) {
            } else if (is_frozen_snode(_node)) {
                auto oldsn = std::static_pointer_cast<snode>(_node);
                std::shared_ptr<base_node> sn{std::make_shared<snode>(
                    oldsn->hash,
                    oldsn->key,
                    oldsn->value
                )};
                narrow->values[i] = sn;
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

    // create fresh snode
    auto create_anode(
        hash_type h1, key_type const& k1, value_type const& v1,
        hash_type h2, key_type const& k2, value_type const& v2,
        int level
    ) -> std::shared_ptr<base_node>
    {
        return create_anode(
            std::make_shared<snode>(h1, k1, v1),
            std::make_shared<snode>(h2, k2, v2),
            level
        );
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

    void complete_expansion(std::shared_ptr<base_node> const& u)
    {
        auto en = std::static_pointer_cast<enode>(u);
        freeze(std::atomic_load(&en->narrow));
        std::shared_ptr<base_node> wide{std::make_shared<anode>(16)};
        auto awide = std::static_pointer_cast<anode>(wide);
        sequential_transfer(std::atomic_load(&en->narrow), awide, en->level);
        std::shared_ptr<anode> empty;
        // TODO maybe create the dereived first for better performance
        if (!std::atomic_compare_exchange_weak(&en->wide, &empty, awide))
            // FIXME ?
            wide = std::atomic_load(&en->wide);
        std::atomic_compare_exchange_weak(&en->parent->values[en->parent_pos], &u, wide);
    }

    auto complete_compression(std::shared_ptr<base_node> const& u) -> bool
    {
        auto xn = std::static_pointer_cast<xnode>(u);
        auto parent = std::atomic_load(&xn->parent);
        auto parent_pos = xn->parent_pos;
        auto level = xn->level;

        auto stale = std::atomic_load(&xn->stale);
        auto compressed = freeze_and_compress(stale, level);

        if (std::atomic_compare_exchange_weak(parent->values[parent_pos], u, compressed)) {
            if (!compressed)
                decrement_count(parent);
            return !compressed || compressed->type() == node::snode;
        }
        return false;
    }

    void freeze(std::shared_ptr<anode> const& cur)
    {
        auto i = 0;
        while (i < cur->values.size()) {
            auto _node = std::atomic_load(&cur->values[i]);
            if (!_node) {
                std::shared_ptr<base_node> fvn = std::make_shared<fvnode>();
                if (!std::atomic_compare_exchange_weak(&cur->values[i], &_node, fvn))
                    i -= 1;
            } else if (_node->type() == node::snode) {
                auto u = std::static_pointer_cast<snode>(_node);
                auto txn = std::atomic_load(&u->txn);
                if (txn->type() == node::notxn) {
                    std::shared_ptr<base_node> fsn = std::make_shared<fsnode>();
                    if (!std::atomic_compare_exchange_weak(&u->txn, &txn, fsn))
                        i -= 1;
                } else if (txn->type() != node::fsnode) {
                    // TODO not fully understood.
                    // explain: copy txn to cur[i] and do another iteration to
                    // help commit the changes first.
                    std::atomic_compare_exchange_weak(&cur->values[i], &_node, txn);
                    i -= 1;
                }
            } else if (_node->type() == node::anode) {
                auto u = std::static_pointer_cast<anode>(_node);
                std::shared_ptr<base_node> fn{std::make_shared<fnode>(u)};
                std::atomic_compare_exchange_weak(&cur->values[i], &_node, fn);
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

    auto freeze_and_compress(std::shared_ptr<anode> const& cur, int level) -> std::shared_ptr<base_node>
    {
        std::shared_ptr<base_node> single;
        auto i = 0;
        while (i < cur->values.size()) {
            auto _node = std::atomic_load(&cur->values[i]);
            if (!_node) {
                std::shared_ptr<base_node> fvn = std::make_shared<fvnode>();
                if (!std::atomic_compare_exchange_weak(&cur->values[i], &_node, fvn))
                    i -= 1;
            } else if (_node->type() == node::snode) {
                auto sn = std::static_pointer_cast<snode>(_node);
                auto txn = std::atomic_load(&sn->txn);
                if (txn->type() == node::notxn) {
                    std::shared_ptr<base_node> fsn = std::make_shared<fsnode>();
                    if (!std::atomic_compare_exchange_weak(&sn->txn, &txn, fsn)) {
                        i -= 1;
                    } else {
                        if (!single) single = sn;
                        else single = cur;
                    }
                } else if (txn->type() != node::fsnode) {
                    single = cur;
                } else {
                    single = cur;
                    std::atomic_compare_exchange_weak(&cur->values[i], &_node, txn);
                    i -= 1;
                }
            } else if (_node->type() == node::anode) {
                single = cur;
                auto an = std::static_pointer_cast<anode>(_node);
                std::shared_ptr<base_node> fn{std::make_shared<fnode>(an)};
                std::atomic_compare_exchange_weak(&cur->values[i], &_node, fn);
                i -= 1;
            } else if (_node->type() == node::fnode) {
                single = cur;
                auto fn = std::static_pointer_cast<fnode>(_node);
                freeze(std::atomic_load(&fn->frozen));
            } else if (_node->type() == node::fvnode) {
                single = cur;
            } else if (_node->type() == node::enode) {
                single = cur;
                complete_expansion(_node);
                i -= 1;
            } else if (_node->type() == node::xnode) {
                single = cur;
                auto xn = std::static_pointer_cast<xnode>(_node);
                complete_compression(xn);
                i -= 1;
            }
            i += 1;
        }
        if (single->type() == node::snode) {
            auto oldsn = std::static_pointer_cast<snode>(single);
            single = std::make_shared<snode>(oldsn->hash, oldsn->key, oldsn->value);
            return single;
        } else if (single) {
            return compress_frozen(cur, level);
        } else {
            return single;
        }
    }

    auto compress_frozen(std::shared_ptr<anode> const& frozen, int level) -> std::shared_ptr<base_node>
    {
        std::shared_ptr<base_node> single;
        auto i = 0;
        while (i < frozen->values.size()) {
            auto old = std::atomic_load(&frozen->values[i]);
            if (old->type() != node::fvnode) {
                if (!single && old->type() == node::snode) {
                    single = old;
                } else {
                    if (frozen->values.size() == 16) {
                        auto wide{std::make_shared<anode>(16)};
                        sequential_transfer(frozen, wide, level);
                        return wide;
                    } else {
                        auto narrow{std::make_shared<anode>(4)};
                        sequential_transfer_narrow(frozen, narrow);
                        return narrow;
                    }
                }
            }
            i += 1;
        }
        if (single) {
            // TODO ?
            auto oldsn = std::static_pointer_cast<snode>(single);
            single = std::make_shared<snode>(oldsn->hash, oldsn->key, oldsn->value);
        }
        return single;
    }


    // TODO key_type = value_type = hash_type
    auto debug_lookup(hash_type hash) -> std::optional<value_type>
    {
        return lookup(hash, hash, 0, std::atomic_load(&root));
    }

    // TODO key_type = value_type = hash_type
    void debug_insert(hash_type hash)
    {
        insert(hash, hash, hash);
    }

    // TODO key_type = value_type = hash_type
    auto debug_remove(hash_type hash) -> std::optional<value_type>
    {
        return remove(hash, hash);
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

    void print_node(std::shared_ptr<base_node> const& u) const
    {
        if (!u) {
            std::cout << "(empty)\n";
        } else if (u->type() == node::base) {
            std::cout << "(base)\n";
        } else if (u->type() == node::anode) {
            auto au = std::static_pointer_cast<anode>(u);
            std::cout << "(anode, size=" << au->values.size() << ")\n";
        } else if (u->type() == node::snode) {
            auto su = std::static_pointer_cast<snode>(u);
            std::cout << "(snode, value=" << su->value << ", txn=" << su->txn->type() << ")\n";
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
        if (u && u->type() == node::anode) {
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

    std::shared_ptr<anode> root{std::make_shared<anode>(16)};
};

} // namespace concurrent

