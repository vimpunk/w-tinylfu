#include "../wtinylfu.hpp"
#include "../bloom_filter.hpp"
#include <iostream>

struct big_object
{
    char data[4096];
};

int main()
{
#define NUM_ENTRIES 1024
#define SELECTED_BEGIN 100
#define SELECTED_END 120

    wtinylfu_cache<int, big_object> cache(NUM_ENTRIES);

    for(auto i = 0; i < NUM_ENTRIES; ++i) {
        cache.insert(i, big_object());
    }

    for(auto i = 0; i < NUM_ENTRIES; ++i) {
        assert(cache[i]);
    }

    // Repeatedly access a few elements, pumping up their access frequencies.
    for(auto i = 0; i < 10; ++i) {
        for(auto s = SELECTED_BEGIN; s < SELECTED_END; ++s) {
            cache[s];
        }
    }

    // Insert enough new entries (with new keys) to leave just num_selected in cache.
    for(auto i = 0; i < NUM_ENTRIES - (SELECTED_END - SELECTED_BEGIN); ++i) {
        cache.insert(i + NUM_ENTRIES, big_object());
    }

    // Make sure selected entries were not evicted.
    for(auto s = SELECTED_BEGIN; s < SELECTED_END; ++s) {
        assert(cache[s]);
    }
}
