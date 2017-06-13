#include "../../src/wtinylfu/wtinylfu.hpp"
#include <iostream>

struct big_object
{
    char data[4096];
};

int main()
{
    wtinylfu_cache<int, big_object> cache(1024);

    for(auto i = 0; i < 1024; ++i) {
        cache.insert(i, big_object());
    }

    for(auto i = 0; i < 1024; ++i) {
        if(!cache[i]) {
            std::cout << i << "th element failed\n";
            assert(false);
        }
    }
}
