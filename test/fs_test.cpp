#include "../frequency_sketch.h"

#include <iostream>
#include <cassert>


int main()
{
    FrequencySketch<int> fs(100);

    fs.record_access(5);
    fs.record_access(5);
    fs.record_access(5);
    //std::cout << fs.get_frequency(5) << '\n';


    for(auto i = 0; i < 300; ++i)
    {
        fs.record_access(i);
    }
    for(auto i = 0; i < 330; ++i)
    {
        const auto freq = fs.get_frequency(i);
        if(i < 300)
            assert(freq > 0);
        std::cout << "freq(" << i << "): " << freq << "\n\n";
    }



}
