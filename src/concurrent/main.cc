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

    t.debug_remove(15);

    t.debug_insert(16);
    t.debug_insert(32);
    t.debug_insert(48);
    t.debug_insert(64);

    // t.debug_remove(16);
    // t.debug_remove(32);
    // t.debug_remove(48);
    // t.debug_remove(64);

    // t.debug_insert(17);
    // t.debug_insert(33);
    // t.debug_insert(49);
    // t.debug_insert(65);

    t.debug_insert(80);
    t.debug_insert(96);
    t.debug_insert(112);
    t.debug_insert(128);
    t.debug_insert(144);
    t.debug_insert(160);
    t.debug_insert(176);
    t.debug_insert(192);
    t.debug_insert(208);
    t.debug_insert(224);
    t.debug_insert(240);

    t.debug_insert(256);

    t.debug_remove(256);

    t.print();
}

