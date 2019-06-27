// ml:opt = 0
// ml:ccf += -g
#include <iostream>
#include "trie.hh"

int main()
{
    concurrent::trie<int, int> t;
    t.debug_insert(0);
    t.debug_insert(4);
    t.debug_insert(1);
    t.debug_insert(15);
    t.debug_insert(16);
    t.print();
}

