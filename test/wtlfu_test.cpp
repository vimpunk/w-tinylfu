#include "../wtinylfu.h"

#include <iostream>
#include <random>


inline int randint(int min, int max)
{
    std::random_device              rd;
    std::mt19937                    gen(rd());
    std::uniform_int_distribution<> dist(min, max);

    return dist(gen);
}

struct HeavyObject
{
    int data[1024];

    HeavyObject()
    {
        for(auto& i : data)
        {
            i = randint(0, unsigned(-1) >> 1);
        }
    }
};


int main()
{
    std::cout << "sizeof(WTinyLFUCache): " << sizeof(WTinyLFUCache<int, int>) << '\n';

    const int CACHE_CAPACITY = 100;
    WTinyLFUCache<int, HeavyObject> cache(CACHE_CAPACITY);

    for(auto i = 0; i < CACHE_CAPACITY; ++i)
    {
        cache.insert(i, HeavyObject());
    }

    //cache.debug();

    // all of the values should be in cache at this point
    for(auto i = 0; i < CACHE_CAPACITY; ++i)
    {
        assert(cache.get(i) != nullptr);
    }

    //cache.debug();

    // shouldn't be in the cache
    assert(cache.get(CACHE_CAPACITY + 100) == nullptr);
}
