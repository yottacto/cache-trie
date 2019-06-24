#pragma once
#include <vector>
#include <atomic>

namespace concurrent
{

struct base_node
{
    virtual ~base_node() = default;
};

template <class Key, class T>
struct snode : base_node
{
    using key_type   = Key;
    using value_type = T;
    using hash_type  = int;

    hash_type hash;
    key_type key;
    value_type value;
};

struct anode : base_node
{
    std::vector<std::atomic<base_node*>> value;
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
    anode* frozen;
};

struct enode : base_node
{
    using hash_type = int;

    anode* parent;
    int parent_pos;
    anode* narrow;
    hash_type hash;
    int level;
    anode* wide;
};

} // namespace concurrent

