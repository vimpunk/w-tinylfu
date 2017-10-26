/* Copyright 2017 https://github.com/mandreyel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef DETAIL_HEADER
#define DETAIL_HEADER

#include <bitset>

namespace detail
{
    // This is Bob Jenkins' One-at-a-Time hash, see:
    // http://www.burtleburtle.net/bob/hash/doobs.html
    template<typename T>
    constexpr uint32_t hash(const T& t) noexcept
    {
        const char* data = reinterpret_cast<const char*>(&t);
        uint32_t hash = 0;

        for(auto i = 0; i < int(sizeof t); ++i)
        {
            hash += data[i];
            hash += hash << 10;
            hash ^= hash >> 6;
        }

        hash += hash << 3;
        hash ^= hash >> 11;
        hash += hash << 15;

        return hash;
    }

    /** Returns the number of set bits in x. Also known as Hamming Weight. */
    template<
        typename T,
        typename std::enable_if<std::is_integral<T>::value, int>::type = 0
    > constexpr int popcount(T x) noexcept
    {
        return std::bitset<sizeof(T) * 8>(x).count();
    }

    // From: http://graphics.stanford.edu/~seander/bithacks.html
    constexpr uint32_t nearest_power_of_two(uint32_t x) noexcept
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
