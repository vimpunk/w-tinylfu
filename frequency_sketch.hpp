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

#ifndef FREQUENCY_SKETCH_HEADER
#define FREQUENCY_SKETCH_HEADER

#include "detail.hpp"

#include <vector>
#include <cmath>
#include <stdexcept>
#include <limits>

/**
 * A probabilistic set for estimating the popularity (frequency) of an element within an
 * access frequency based time window. The maximum frequency of an element is limited
 * to 15 (4-bits).
 *
 * NOTE: the capacity will be the nearest power of two of the input capacity (for various
 * efficiency and hash distribution gains).
 *
 * This is a slightly altered version of Caffeine's implementation:
 * https://github.com/ben-manes/caffeine
 *
 * The white paper:
 * http://dimacs.rutgers.edu/~graham/pubs/papers/cm-full.pdf
 */
template<typename T>
class frequency_sketch
{
    // Holds 64 bit blocks, each of which holds sixteen 4 bit counters. For simplicity's
    // sake, the 64 bit blocks are partitioned into four 16 bit sub-blocks, and the four
    // counters corresponding to some T is within a single such sub-block.
    std::vector<uint64_t> table_;

    // Incremented with each call to record_access, if the frequency of the item could
    // be incremented, and halved when sampling size is reached.
    int size_;

public:
    explicit frequency_sketch(int capacity)
    {
        change_capacity(capacity);
    }

    void change_capacity(const int n)
    {
        if(n <= 0)
        {
            throw std::invalid_argument("frequency_sketch capacity must be larger than 0");
        }
        table_.resize(detail::nearest_power_of_two(n));
        size_ = 0;
    }

    bool contains(const T& t) const noexcept
    {
        return frequency(t) > 0;
    }

    int frequency(const T& t) const noexcept
    {
        const uint32_t hash = detail::hash(t);
        int frequency = std::numeric_limits<int>::max();

        for(auto i = 0; i < 4; ++i)
        {
            frequency = std::min(frequency, get_count(hash, i));
        }

        return frequency;
    }

    void record_access(const T& t) noexcept
    {
        const uint32_t hash = detail::hash(t);
        bool was_added = false;

        for(auto i = 0; i < 4; ++i)
        {
            was_added |= try_increment_counter_at(hash, i);
        }

        if(was_added && (++size_ == sampling_size()))
        {
            reset();
        }
    }

private:
    int get_count(const uint32_t hash, const int counter_index) const noexcept
    {
        const int table_index = this->table_index(hash, counter_index);
        const int offset = counter_offset(hash, counter_index);
        return (table_[table_index] >> offset) & 0xfL;
    }

    /**
     * Returns the table index where the counter associated with $hash at
     * $counter_index resides (since each item is mapped to four different counters in
     * $table_, an index is necessary to differentiate between each).
     */
    int table_index(const uint32_t hash, const int counter_index) const noexcept
    {
        static constexpr uint64_t seeds[] = {
            0xc3a5c85c97cb3127L,
            0xb492b66fbe98f273L,
            0x9ae16a3b2f90404fL,
            0xcbf29ce484222325L
        };
        uint64_t h = seeds[counter_index] * hash;
        h += h >> 32;
        return h & (table_.size() - 1);
    }

    /**
     * Increments ${counter_index}th counter by 1 if it's below the maximum value (15).
     * Returns true if the counter was incremented.
     */
    bool try_increment_counter_at(const uint32_t hash, const int counter_index)
    {
        const int index = table_index(hash, counter_index);
        const int offset = counter_offset(hash, counter_index);
        if(can_increment_counter_at(index, offset))
        {
            table_[index] += 1L << offset;
            return true;
        }
        return false;
    }

    /**
     * $table_ holds 64 bit blocks, while counters are 4 bit wide, i.e. there are 16
     * counters in a block.
     * This function determines the start offset of the ${counter_index}th counter
     * associated with $hash.
     * Offset may be [0, 60] and is a multiple of 4. $counter_index must be [0, 3].
     */
    int counter_offset(const uint32_t hash, const int counter_index) const noexcept
    {
        return (offset_multiplier(hash) + counter_index) << 2;
    }

    /**
     * $table_ holds 64 bit blocks, and each block is partitioned into four 16 bit
     * parts, starting at 0, 16, 32 and 48. Each part is further divided into four 4 bit
     * sub-parts (e.g. 0, 4, 8, 12), which are the start offsets of the counters.
     *
     * All counters of an item are within the same logical 16 bit part (though most
     * likely not in the same 64 bit block if the hash does its job). Which 16 bit part
     * an item is placed into is determined by its two least significant bits, which
     * this function determines.
     *
     * The return value may be 0, 4, 8 or 12.
     */
    int offset_multiplier(const uint32_t hash) const noexcept
    {
        return (hash & 3) << 2;
    }

    /** Returns true if the counter has not reached the limit of 15. */
    bool can_increment_counter_at(const int table_index, const int offset) const noexcept
    {
        const uint64_t mask = 0xfL << offset;
        return (table_[table_index] & mask) != mask;
    }

    /** Halves every counter and adjusts $size_. */
    void reset() noexcept
    {
        for(auto& counters : table_)
        {
            // Do a 'bitwise_and' on each (4 bit) counter with 0111 (7) so as to
            // eliminate the bit that got shifted over from the counter to the left to
            // the leftmost position of the current counter.
            counters = (counters >> 1) & 0x7777777777777777L;
        }
        size_ /= 2;
    }

    /**
     * The reset operation is launched when $size_ reaches the value returned by this
     * function.
     */
    int sampling_size() const noexcept
    {
        return table_.size() * 10;
    }
};

#endif
