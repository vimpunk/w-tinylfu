/* Copyright 2016 https://github.com/mandreyel
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

#ifndef WTINYLFU_HEADER
#define WTINYLFU_HEADER

#include "detail.h"
#include "frequency_sketch.h"

#include <map>
#include <list>
#include <memory>
#include <cmath>
#include <cassert>

/**
 * Window-TinyLFU Cache as per: https://arxiv.org/pdf/1512.00727.pdf
 *
 *
 *           Window Cache Victim .---------. Main Cache Victim    
 *          .------------------->| TinyLFU |<-----------------.
 *          |                    `---------'                  |
 * .-------------------.              |    .------------------.
 * | Window Cache (1%) |              |    | Main Cache (99%) |
 * |      (LRU)        |              |    |      (SLRU)      |
 * `-------------------'              |    `------------------'
 *          ^                         |               ^
 *          |                         `---------------'
 *       new item                        Winner
 *
 *
 * New entries are first placed in the window cache where they remain as long as they
 * have high temporal locality. An entry that's pushed out of the window cache gets a
 * chance to be admitted in the front of the main cache. If the main cache is full,
 * the TinyLFU admission policy determines whether this entry is to replace the main
 * cache's next victim based on TinyLFU's implementation defined historic frequency
 * filter. Currently a 4 bit frequency sketch is employed.
 *
 * TinyLFU's periodic reset operation ensures that lingering entries that are no longer
 * accessed are evicted.
 *
 * NOTE: it is advised that trivially copiable, small keys be used as there persist two
 * copies of each within the cache.
 * NOTE: it is NOT thread-safe!
 */
template<
    typename K,
    typename V
> class WTinyLFUCache
{
    using ValuePtr = std::shared_ptr<V>;


    enum class CacheType
    {
        WINDOW,
        PROBATIONARY,
        PROTECTED
    };


    struct Page
    {
        K         key;
        CacheType cache_type;
        ValuePtr  data;

        Page(K key_, CacheType cache_type_, ValuePtr data_)
            : key(key_)
            , cache_type(cache_type_)
            , data(data_)
        {}
    };


    class LRU
    {
        std::list<Page> m_lru;
        int             m_capacity;

    public:

        using page_position       = typename std::list<Page>::iterator;
        using const_page_position = typename std::list<Page>::const_iterator;


        explicit LRU(int capacity) : m_capacity(capacity) {}


        int size() const noexcept
        {
            return m_lru.size();
        }


        int capacity() const noexcept
        {
            return m_capacity;
        }


        bool has_reached_capacity() const noexcept
        {
            return size() >= capacity();
        }

        // NOTE: doesn't actually remove any pages, it only sets the capacity!
        //
        // This is because otherwise there'd be no way to delete the corresponding
        // entries from the page map outside of this LRU instance.
        void set_capacity(const int n) noexcept
        {
            m_capacity = n;
        }

        /** Returns the position of the hottest (most recently used) page. */
        page_position get_mru_pos() noexcept
        {
            return m_lru.begin();
        }

        const_page_position get_mru_pos() const noexcept
        {
            return m_lru.begin();
        }

        /** Returns the position of the coldest (least recently used) page. */
        page_position get_lru_pos() noexcept
        {
            return --m_lru.end();
        }

        const_page_position get_lru_pos() const noexcept
        {
            return --m_lru.end();
        }


        const K& get_victim_key() const noexcept
        {
            return get_lru_pos()->key;
        }

        
        void evict()
        {
            m_lru.erase(get_lru_pos());
        }

        /** Inserts new page at the MRU position of the cache. */
        template<typename... Args>
        page_position insert(Args&&... args)
        {
            return m_lru.emplace(get_mru_pos(), std::forward<Args>(args)...);
        }

        /** Moves page to the MRU position. */
        void handle_hit(page_position page)
        {
            transfer_page_from(page, *this);
        }


        void transfer_page_from(page_position page, LRU& source)
        {
            m_lru.splice(get_mru_pos(), source.m_lru, page);
        }
    };

    /**
     * A cache which is divided into two segments, a probationary and a protected
     * segment. Both are LRU caches.
     *
     * Pages that are cache hits are promoted to the top (MRU position) of the protected
     * segment, regardless of the segment in which they currently reside. Thus, pages
     * within the protected segment have been accessed at least twice.
     *
     * Pages that are cache MISSES are added to the cache at the MRU position of the
     * probationary segment.
     *
     * Each segment is finite in size, so the migration of a page from the probationary
     * segment may force the LRU page of the protected segment into the MRU position of
     * the probationary segment, giving it another chance. Likewise, if both segments
     * reached their capacity, a new entry is replaced with the LRU victim of the
     * probationary segment.
     *
     * In this implementation 80% of the capacity is allocated to the protected (the
     * "hot" pages) and 20% for pages under probation (the "cold" pages).
     */
    class SLRU
    {
        LRU m_protected;
        LRU m_probationary;

    public:

        using page_position       = typename LRU::page_position;
        using const_page_position = typename LRU::const_page_position;


        explicit SLRU(int capacity)
            : SLRU(0.8f * capacity, 0.2f * capacity)
        {
            while(this->capacity() < capacity)
            {
                m_protected.set_capacity(m_protected.capacity() + 1);
            }
        }

        explicit SLRU(int protected_capacity, int probationary_capacity)
            : m_protected(protected_capacity)
            , m_probationary(probationary_capacity)
        {}


        const int size() const noexcept
        {
            return m_protected.size() + m_probationary.size();
        }


        const int capacity() const noexcept
        {
            return m_protected.capacity() + m_probationary.capacity();
        }


        const bool has_reached_capacity() const noexcept
        {
            return size() >= capacity();
        }


        void set_capacity(const int n)
        {
            int       protected_capacity    = 0.8f * n;
            const int probationary_capacity = 0.2f * n;

            while(protected_capacity + probationary_capacity < n)
            {
                ++protected_capacity;
            }

            m_protected.set_capacity(protected_capacity);
            m_probationary.set_capacity(probationary_capacity);
        }


        page_position get_victim_pos() noexcept
        {
            return m_probationary.get_lru_pos();
        }

        const_page_position get_victim_pos() const noexcept
        {
            return m_probationary.get_lru_pos();
        }


        const K& get_victim_key() const noexcept
        {
            return get_victim_pos()->key;
        }


        void evict()
        {
            m_probationary.evict();
        }

        /** Moves page to the MRU position of the probationary segment. */
        void transfer_page_from(page_position page, LRU& source)
        {
            m_probationary.transfer_page_from(page, source);
            page->cache_type = CacheType::PROBATIONARY;
        }

        /**
         * If page is in the probationary segment:
         * promotes page to the MRU position of the protected segment, and if protected
         * segment capacity is reached, moves the LRU page of the protected segment to
         * the MRU position of the probationary segment.
         *
         * Otherwise, page is in protected:
         * promotes page to the MRU position of protected.
         */
        void handle_hit(page_position page)
        {
            if(page->cache_type == CacheType::PROBATIONARY)
            {
                promote_to_protected(page);
                if(m_protected.has_reached_capacity())
                {
                    demote_to_probationary(m_protected.get_lru_pos());
                }
            }
            else
            {
                assert(page->cache_type == CacheType::PROTECTED); // this shouldn't happen
                m_protected.handle_hit(page);
            }
        }

    private:

        void promote_to_protected(page_position page)
        {
            m_protected.transfer_page_from(page, m_probationary);
            page->cache_type = CacheType::PROTECTED;
        }


        void demote_to_probationary(page_position page)
        {
            m_probationary.transfer_page_from(page, m_protected);
            page->cache_type = CacheType::PROBATIONARY;
        }
    };


    using PageMap = std::map<K, typename LRU::page_position>;


    FrequencySketch<K> m_filter;
    // Maps keys to page positions of the LRU caches pointing to a page.
    PageMap            m_page_map;
    // Allocated 1% of the total capacity. Window victims are granted to chance to
    // reenter the cache (into $m_main). This is to remediate the problem where sparse
    // bursts cause repeated misses in the regular TinyLfu architecture.
    LRU                m_window;
    // Allocated 99% of the total capacity.
    SLRU               m_main;

public:

    explicit WTinyLFUCache(int capacity)
        : m_filter(capacity)
        , m_window(get_window_capacity(capacity))
        , m_main(capacity - m_window.capacity())
    {
        correct_capacity_truncation_error(capacity);
    }


    int size() const noexcept
    {
        return m_window.size() + m_main.size();
    }


    int capacity() const noexcept
    {
        return m_window.capacity() + m_main.capacity();
    }


    bool contains(const K& key) const noexcept
    {
        return m_page_map.find(key) != m_page_map.cend();
    }

    /**
     * NOTE: after this operation the accuracy of the cache will suffer until enough
     * historic data is gathered.
     */
    void change_capacity(const int n)
    {
        if(n <= 0)
        {
            throw std::invalid_argument("cache capacity must be greater than zero!");
        }

        m_filter.change_capacity(n);
        m_window.set_capacity(get_window_capacity(n));
        m_main.set_capacity(n - m_window.capacity());
        correct_capacity_truncation_error(n);

        while(m_window.has_reached_capacity())
        {
            evict_from_window();
        }
        while(m_main.has_reached_capacity())
        {
            evict_from_main();
        }
    }


    std::shared_ptr<V> get(const K& key)
    {
        m_filter.record_access(key);
        auto it = m_page_map.find(key);
        if(it != m_page_map.end())
        {
            auto page = it->second;
            handle_hit(page);
            return page->data;
        }
        return nullptr;
    }


    template<typename ValueLoader>
    std::shared_ptr<V> get_and_insert_if_missing(const K& key, ValueLoader value_loader)
    {
        ValuePtr value = get(key);
        if(value == nullptr)
        {
            value = std::make_shared<V>(value_loader(key));
            insert(key, value);
        }
        return value;
    }


    void insert(const K& key, V value)
    {
        insert(key, std::make_shared<V>(std::move(value)));
    }

private:

    static int get_window_capacity(const int total_capacity) noexcept
    {
        return std::max(1, int(std::ceil(0.01f * total_capacity)));
    }
    

    void correct_capacity_truncation_error(const int desired_capacity)
    {
        while(capacity() < desired_capacity)
        {
            m_main.set_capacity(m_main.capacity() + 1);
        }
    }


    void insert(const K& key, ValuePtr data)
    {
        if(m_window.has_reached_capacity())
        {
            evict();
        }
        if(contains(key))
        {
            // TODO think about whether this is the appropriate reaction. maybe data
            // should just be overwritten? or just return without overwriting or throwing?
            throw std::invalid_argument("key already in cache");
        }
        m_page_map.emplace(key, m_window.insert(key, CacheType::WINDOW, data));
    }


    void handle_hit(typename LRU::page_position page)
    {
        if(page->cache_type == CacheType::WINDOW)
        {
            m_window.handle_hit(page);
        }
        else
        {
            m_main.handle_hit(page);
        }
    }

    /**
     * Evicts from the window cache to the main cache's probationary space.
     * It is called once the window cache is full. If the cache's total size exceeds its
     * capacity, the window cache's victim and the main cache's eviction candidate are
     * evaluated and one is evicted. Otherwise, the window cache's victim is transferred
     * to the main cache.
     */
    void evict()
    {
        if(size() >= capacity())
        {
            evict_from_window_or_main();
        }
        else
        {
            m_main.transfer_page_from(m_window.get_lru_pos(), m_window);
        }
    }


    void evict_from_window_or_main()
    {
        if(is_window_victim_better_than_main_victim())
        {
            evict_from_main();
            m_main.transfer_page_from(m_window.get_lru_pos(), m_window);
        }
        else
        {
            evict_from_window();
        }
    }


    bool is_window_victim_better_than_main_victim() const noexcept
    {
        return m_filter.get_frequency(m_window.get_victim_key())
             > m_filter.get_frequency(m_main.get_victim_key());
    }


    void evict_from_main()
    {
        m_page_map.erase(m_main.get_victim_key());
        m_main.evict();
    }


    void evict_from_window()
    {
        m_page_map.erase(m_window.get_victim_key());
        m_window.evict();
    }
};


#endif

