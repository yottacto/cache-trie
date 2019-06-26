#include <iostream>
#include "trie.hh"

int main()
{
    concurrent::trie<int, int> t;
    t.insert(0, 0, 0);
    t.print();
}

