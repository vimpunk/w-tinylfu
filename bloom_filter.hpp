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

#ifndef BLOOM_FILTER_HEADER
#define BLOOM_FILTER_HEADER

#include "detail.hpp"

#include <vector>
#include <cmath>

/**
 * Standard 1 bit Bloom filter.
 * http://www.cs.princeton.edu/courses/archive/spr05/cos598E/bib/bloom_filters.pdf
 */
template<
    typename T,
    typename Hash = std::hash<T>
> class bloom_filter
{
    std::vector<bool> bitset_;
    int capacity_;
    int num_hashes_;

public:
    explicit bloom_filter(int capacity, double false_positive_error_rate = 0.01)
        : bloom_filter(capacity, false_positive_error_rate,
            best_bitset_size(capacity, false_positive_error_rate),
            best_num_hashes(capacity, false_positive_error_rate))
    {}

    bloom_filter(int capacity, double false_positive_error_rate, 
        int bitset_size, int num_hashes)
        : bitset_(bitset_size)
        , capacity_(capacity)
        , num_hashes_(num_hashes)
    {}

    /**
     * A truthy return value indicates that the item may or may not have been accessed.
     * A falsy return value guarantees that the item has not been accessed.
     */
    bool contains(const T& t) const noexcept
    {
        // idea use a single 64bit hash and use the upper and lower parts as the two
        // base hashes TODO
        const uint32_t hash1 = detail::hash(t);
        const uint32_t hash2 = Hash()(t);
        for(auto i = 0; i < num_hashes_; ++i)
        {
            if(!bitset_[double_hash(hash1, hash2, i)]) { return false; }
        }
        return true;
    }

    void record_access(const T& t)
    {
        const uint32_t hash1 = detail::hash(t);
        const uint32_t hash2 = Hash()(t);
        for(auto i = 0; i < num_hashes_; ++i)
        {
            bitset_[double_hash(hash1, hash2, i)] = true;
        }
    }

    void clear() noexcept
    {
        bitset_.clear();
    }

protected: 
    // From: http://matthias.vallentin.net/blog/2011/06/a-garden-variety-of-bloom-filters/
    static int best_bitset_size(const int capacity, const double error_rate) noexcept
    {
        return std::ceil(-1 * capacity * std::log(error_rate) / std::pow(std::log(2), 2));
    }

    static int best_num_hashes(const int capacity, const double error_rate) noexcept
    {
        const auto bitset_size = best_bitset_size(capacity, error_rate);
        return std::round(std::log(2) * bitset_size / double(capacity));
    }

    uint32_t double_hash(uint32_t hash1, uint32_t hash2, int i) const noexcept
    {
        return (hash1 + i * hash2) % bitset_.size();
    }
};

#endif
