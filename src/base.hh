#pragma once
#include <vector>
#include <any>

namespace concurrent
{

struct base_node
{
};

struct snode : base_node
{
};

// store atomic elements
using anode = std::vector<std::atomic<base_node*>>;

struct fsnode : base_node
{
};

struct fvnode : base_node
{
};

struct fnode : base_node
{
};

struct enode : base_node
{
};

} // namespace concurrent

