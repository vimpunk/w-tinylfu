#ifndef DETAIL_HEADER
#define DETAIL_HEADER

#include <bitset>


namespace detail
{
    // This is Bob Jenkins' One-at-a-Time hash, see:
    // http://www.burtleburtle.net/bob/hash/doobs.html
    template<typename T>
    uint32_t get_hash(const T& t) noexcept
    {
        const char* data = reinterpret_cast<const char*>(&t);
        uint32_t    hash = 0;
        int         size = sizeof(T);

        for(auto i = 0; i < size; ++i)
        {
            hash += *(data + i);
            hash += (hash << 10);
            hash ^= (hash >> 6);
        }

        hash += (hash << 3);
        hash ^= (hash >> 11);
        hash += (hash << 15);

        return hash;
    }

    /* Returns the number of set bits in x. Also known as Hamming Weight. */
    template<
        typename T,
        typename std::enable_if<std::is_integral<T>::value, int>::type = 0
    > int get_popcount(T x) noexcept
    {
        return std::bitset<sizeof(T) * 8>(x).count();
    }

    // From: http://graphics.stanford.edu/~seander/bithacks.html
    uint32_t get_nearest_power_of_two(uint32_t x) noexcept
    {
        --x;
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        ++x;
        return x;
    }
} // namespace detail


#endif

