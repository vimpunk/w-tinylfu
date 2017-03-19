#ifndef FREQUENCY_SKETCH_HEADER
#define FREQUENCY_SKETCH_HEADER

#include "utils.h"

#include <vector>
#include <cmath>
#include <iostream>


//-------------------------------------------------------------------------------------//
template<
    typename T
> class FrequencySketch
//-------------------------------------------------------------------------------------//
// A probabilistic set for estimating the popularity (frequency) of an element within a
// access frequency based time window. The maximum frequency of an element is limited
// to 15 (4-bits).
//
// NOTE: the capacity will be the nearest power of two of the input capacity (for various
// efficiency and hash distribution gains).
//
// This is a slightly altered version of Caffeine's implementation:
// https://github.com/ben-manes/caffeine
//
// The white paper:
// http://dimacs.rutgers.edu/~graham/pubs/papers/cm-full.pdf
//-------------------------------------------------------------------------------------//
{
    using counter_t = uint64_t;

    // Holds 64 bit blocks, each of which holds 16 counters. For simplicity's sake the
    // 64 bit blocks are partitioned into four 16 bit sub-blocks, and the four counters
    // corresponding to some T is within a single such sub-block.
    std::vector<counter_t> m_table;
    // Incremented with each call to record_access, halved when sampling size is reached.
    int m_size;

public:

    explicit FrequencySketch(long capacity)
    {
        change_capacity(capacity);
    }


    void change_capacity(const long capacity)
    {
        if(capacity <= 0)
        {
            throw std::invalid_argument("FrequencySketch capacity must be larger than 0");
        }
        m_table.resize(detail::get_nearest_power_of_two(capacity));
        m_size = 0;
    }


    bool has(const T& t) const noexcept
    {
        return get_frequency(t) > 0;
    }


    int get_frequency(const T& t) const noexcept
    {
        const uint32_t hash      = detail::get_hash(t);
        int            frequency = unsigned(-1) >> 1;

        for(auto i = 0; i < 4; ++i)
        {
            frequency = std::min(frequency, get_count(hash, i));
        }

        return frequency;
    }


    void record_access(const T& t) noexcept
    {
        const uint32_t hash      = detail::get_hash(t);
        bool           was_added = false;

        for(auto i = 0; i < 4; ++i)
        {
            was_added |= try_increment_at(hash, i);
        }

        if(was_added && (++m_size == get_sample_size()))
        {
            reset();
        }
    }

protected:

    int get_count(const uint32_t hash, const int counter_index) const noexcept
    {
        const int index  = get_table_index(hash, counter_index);
        const int offset = get_counter_offset(hash, counter_index);
        return (m_table[index] >> offset) & 0xfL;
    }

    /**
     * Returns the table index where the counter associated with $hash at
     * $counter_index resides (since each item is mapped to four different counters in
     * $m_table, an index is necessary to differentiate between each).
     */
    int get_table_index(const uint32_t hash, const int counter_index) const noexcept
    {
        static constexpr uint64_t SEEDS[] = {
            0xc3a5c85c97cb3127L,
            0xb492b66fbe98f273L,
            0x9ae16a3b2f90404fL,
            0xcbf29ce484222325L
        };

        uint64_t h = SEEDS[counter_index] * hash;
        h += h >> 32;
        return h & (m_table.size() - 1);
    }

    /**
     * Increments ${counter_index}th counter by 1 if it's below the maximum value (15).
     * Returns true if the counter was incremented.
     */
    bool try_increment_at(const uint32_t hash, const int counter_index)
    {
        const int      table_index = get_table_index(hash, counter_index);
        const int      offset      = get_counter_offset(hash, counter_index);
        const uint64_t mask        = 0xfL << offset;

        if(can_increment_counter(table_index, mask))
        {
            // increment TODO perhaps put in own fn
            m_table[table_index] += 1L << offset;
            return true;
        }
        return false;
    }

    /**
     * $m_table holds 64 bit blocks, while counters are 4 bit wide, i.e. there are 16
     * counters in a block.
     * This function determines the start offset of the ${counter_index}th counter
     * associated with $hash.
     * Offset may be [0, 60] and is a multiple of 4. $counter_index must be [0, 3].
     */
    int get_counter_offset(const uint32_t hash, const int counter_index) const noexcept
    {
        return (get_offset_multiplier(hash) + counter_index) << 2;
    }

    /**
     * $m_table holds 64 bit blocks, and each block is partitioned into four 16 bit
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
    int get_offset_multiplier(const uint32_t hash) const noexcept
    {
        return (hash & 3) << 2;
    }

    /* Returns true if the counter has not reached the limit of 15. */
    bool can_increment_counter(const int table_index, const uint64_t mask) const noexcept
    {
        return (m_table[table_index] & mask) != mask;
    }

    /* Halves every counter and adjusts $m_size. */
    void reset() noexcept // TODO verify noexcept claim
    {
        //int popcount = 0;
        for(auto& counter : m_table)
        {
            //popcount  += detail::get_popcount(counter & 0x1111111111111111L);
            //counter = (counter >> 1) & 0x77'77'77'77'77'77'77'77L;
            halve(counter);
        }
        //m_size = (m_size >> 1) - (popcount >> 2);
        //m_size = (m_size / 2) - (popcount / 4);
        m_size /= 2;
    }


    void halve(counter_t& counter) noexcept
    {
        // Do a 'bitwise_and' on each counter with 0111 (7) so as to eliminate the bit
        // that got shifted over to the leftmost position of a counter from the previous
        // one.
        counter = (counter >> 1) & 0x77'77'77'77'77'77'77'77L;
    }


    int get_sample_size() const noexcept
    {
        return m_table.size() * 10;
    }
};


#endif

