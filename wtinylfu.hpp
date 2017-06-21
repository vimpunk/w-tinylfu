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

#ifndef WTINYLFU_HEADER
#define WTINYLFU_HEADER

#include "frequency_sketch.hpp"
#include "detail.hpp"

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
> class wtinylfu_cache
{
    enum class cache_t
    {
        window,
        probationary,
        eden
    };

    struct page
    {
        K key;
        cache_t cache_type;
        std::shared_ptr<V> data;

        page(K key_, cache_t cache_type_, std::shared_ptr<V> data_)
            : key(key_)
            , cache_type(cache_type_)
            , data(data_)
        {}
    };

    class lru
    {
        std::list<page> m_lru;
        int m_capacity;

    public:

        using page_position = typename std::list<page>::iterator;
        using const_page_position = typename std::list<page>::const_iterator;

        explicit lru(int capacity) : m_capacity(capacity) {}

        int size() const noexcept { return m_lru.size(); }
        int capacity() const noexcept { return m_capacity; }
        bool is_full() const noexcept { return size() >= capacity(); }

        /**
         * NOTE: doesn't actually remove any pages, it only sets the capacity.
         *
         * This is because otherwise there'd be no way to delete the corresponding
         * entries from the page map outside of this LRU instance, so this is handled
         * externally.
         */
        void set_capacity(const int n) noexcept { m_capacity = n; }

        /** Returns the position of the hottest (most recently used) page. */
        page_position mru_pos() noexcept { return m_lru.begin(); }
        const_page_position mru_pos() const noexcept { return m_lru.begin(); }

        /** Returns the position of the coldest (least recently used) page. */
        page_position lru_pos() noexcept { return --m_lru.end(); }
        const_page_position lru_pos() const noexcept { return --m_lru.end(); }

        const K& victim_key() const noexcept
        {
            return lru_pos()->key;
        }

        void evict()
        {
            erase(lru_pos());
        }

        void erase(page_position page)
        {
            m_lru.erase(page);
        }

        /** Inserts new page at the MRU position of the cache. */
        template<typename... Args>
        page_position insert(Args&&... args)
        {
            return m_lru.emplace(mru_pos(), std::forward<Args>(args)...);
        }

        /** Moves page to the MRU position. */
        void handle_hit(page_position page)
        {
            transfer_page_from(page, *this);
        }

        /** Moves page from $source to the MRU position of this cache. */
        void transfer_page_from(page_position page, lru& source)
        {
            m_lru.splice(mru_pos(), source.m_lru, page);
        }
    };

    /**
     * A cache which is divided into two segments, a probationary and a eden
     * segment. Both are LRU caches.
     *
     * Pages that are cache hits are promoted to the top (MRU position) of the eden
     * segment, regardless of the segment in which they currently reside. Thus, pages
     * within the eden segment have been accessed at least twice.
     *
     * Pages that are cache misses are added to the cache at the MRU position of the
     * probationary segment.
     *
     * Each segment is finite in size, so the migration of a page from the probationary
     * segment may force the LRU page of the eden segment into the MRU position of
     * the probationary segment, giving it another chance. Likewise, if both segments
     * reached their capacity, a new entry is replaced with the LRU victim of the
     * probationary segment.
     *
     * In this implementation 80% of the capacity is allocated to the eden (the
     * "hot" pages) and 20% for pages under probation (the "cold" pages).
     */
    class slru
    {
        lru m_eden;
        lru m_probationary;

    public:

        using page_position = typename lru::page_position;
        using const_page_position = typename lru::const_page_position;

        explicit slru(int capacity) : slru(0.8f * capacity, capacity - 0.8f * capacity)
        {
            // correct truncation error
            if(this->capacity() < capacity)
            {
                m_eden.set_capacity(m_eden.capacity() + 1);
            }
        }

        explicit slru(int eden_capacity, int probationary_capacity)
            : m_eden(eden_capacity)
            , m_probationary(probationary_capacity)
        {}

        const int size() const noexcept
        {
            return m_eden.size() + m_probationary.size();
        }

        const int capacity() const noexcept
        {
            return m_eden.capacity() + m_probationary.capacity();
        }

        const bool is_full() const noexcept
        {
            return size() >= capacity();
        }

        void set_capacity(const int n)
        {
            m_eden.set_capacity(0.8f * n);
            m_probationary.set_capacity(n - m_eden.capacity());
        }

        page_position victim_pos() noexcept
        {
            return m_probationary.lru_pos();
        }

        const_page_position victim_pos() const noexcept
        {
            return m_probationary.lru_pos();
        }

        const K& victim_key() const noexcept
        {
            return victim_pos()->key;
        }

        void evict()
        {
            m_probationary.evict();
        }

        void erase(page_position page)
        {
            if(page->cache_type == cache_t::eden)
            {
                m_eden.erase(page);
            }
            else
            {
                m_probationary.erase(page);
            }
        }

        /** Moves page to the MRU position of the probationary segment. */
        void transfer_page_from(page_position page, lru& source)
        {
            m_probationary.transfer_page_from(page, source);
            page->cache_type = cache_t::probationary;
        }

        /**
         * If page is in the probationary segment:
         * promotes page to the MRU position of the eden segment, and if eden segment
         * capacity is reached, moves the LRU page of the eden segment to the MRU
         * position of the probationary segment.
         *
         * Otherwise, page is in eden:
         * promotes page to the MRU position of eden.
         */
        void handle_hit(page_position page)
        {
            if(page->cache_type == cache_t::probationary)
            {
                promote_to_eden(page);
                if(m_eden.is_full())
                {
                    demote_to_probationary(m_eden.lru_pos());
                }
            }
            else
            {
                assert(page->cache_type == cache_t::eden); // this shouldn't happen
                m_eden.handle_hit(page);
            }
        }

    private:

        // Both of the below functions promote to the MRU position.

        void promote_to_eden(page_position page)
        {
            m_eden.transfer_page_from(page, m_probationary);
            page->cache_type = cache_t::eden;
        }

        void demote_to_probationary(page_position page)
        {
            m_probationary.transfer_page_from(page, m_eden);
            page->cache_type = cache_t::probationary;
        }
    };

    frequency_sketch<K> m_filter;

    // Maps keys to page positions of the LRU caches pointing to a page.
    std::map<K, typename lru::page_position> m_page_map;

    // Allocated 1% of the total capacity. Window victims are granted the chance to
    // reenter the cache (into $m_main). This is to remediate the problem where sparse
    // bursts cause repeated misses in the regular TinyLfu architecture.
    lru m_window;

    // Allocated 99% of the total capacity.
    slru m_main;

    int m_num_cache_hits = 0;
    int m_num_cache_misses = 0;

public:

    explicit wtinylfu_cache(int capacity)
        : m_filter(capacity)
        , m_window(window_capacity(capacity))
        , m_main(capacity - m_window.capacity())
    {}

    int size() const noexcept
    {
        return m_window.size() + m_main.size();
    }

    int capacity() const noexcept
    {
        return m_window.capacity() + m_main.capacity();
    }

    int num_cache_hits() const noexcept { return m_num_cache_hits; }
    int num_cache_misses() const noexcept { return m_num_cache_misses; }

    bool contains(const K& key) const noexcept
    {
        return m_page_map.find(key) != m_page_map.cend();
    }

    /**
     * NOTE: after this operation the accuracy of the cache will suffer until enough
     * historic data is gathered (because the frequency sketch is cleared).
     */
    void change_capacity(const int n)
    {
        if(n <= 0)
        {
            throw std::invalid_argument("cache capacity must be greater than zero");
        }

        m_filter.change_capacity(n);
        m_window.set_capacity(window_capacity(n));
        m_main.set_capacity(n - m_window.capacity());

        while(m_window.is_full())
        {
            evict_from_window();
        }
        while(m_main.is_full())
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
            auto& page = it->second;
            handle_hit(page);
            return page->data;
        }
        ++m_num_cache_misses;
        return nullptr;
    }

    std::shared_ptr<V> operator[](const K& key)
    {
        return get(key);
    }

    template<typename ValueLoader>
    std::shared_ptr<V> get_and_insert_if_missing(const K& key, ValueLoader value_loader)
    {
        std::shared_ptr<V> value = get(key);
        if(value == nullptr)
        {
            value = std::make_shared<V>(value_loader(key));
            insert(key, value);
        }
        return value;
    }

    void insert(K key, V value)
    {
        insert(std::move(key), std::make_shared<V>(std::move(value)));
    }

    void erase(const K& key)
    {
        auto it = m_page_map.find(key);
        if(it != m_page_map.end())
        {
            auto& page = it->second;
            if(page->cache_type == cache_t::window)
            {
                m_window.erase(page);
            }
            else
            {
                m_main.erase(page);
            }
            m_page_map.erase(it);
        }
    }

private:

    static int window_capacity(const int total_capacity) noexcept
    {
        return std::max(1, int(std::ceil(0.01f * total_capacity)));
    }

    void insert(const K& key, std::shared_ptr<V> data)
    {
        if(m_window.is_full())
        {
            evict();
        }

        auto it = m_page_map.find(key);
        if(it != m_page_map.end())
        {
            it->second->data = data;
        }
        else
        {
            m_page_map.emplace(key, m_window.insert(key, cache_t::window, data));
        }
    }

    void handle_hit(typename lru::page_position page)
    {
        if(page->cache_type == cache_t::window)
        {
            m_window.handle_hit(page);
        }
        else
        {
            m_main.handle_hit(page);
        }
        ++m_num_cache_hits;
    }

    /**
     * Evicts from the window cache to the main cache's probationary space.
     * Called when the window cache is full.
     * If the cache's total size exceeds its capacity, the window cache's victim and
     * the main cache's eviction candidate are evaluated and the one with the worse
     * (estimated) access frequency is evicted. Otherwise, the window cache's victim is
     * just transferred to the main cache.
     */
    void evict()
    {
        if(size() >= capacity())
        {
            evict_from_window_or_main();
        }
        else
        {
            m_main.transfer_page_from(m_window.lru_pos(), m_window);
        }
    }

    void evict_from_window_or_main()
    {
        const int window_victim_freq = m_filter.frequency(m_window.victim_key());
        const int main_victim_freq = m_filter.frequency(m_main.victim_key());
        if(window_victim_freq > main_victim_freq)
        {
            evict_from_main();
            m_main.transfer_page_from(m_window.lru_pos(), m_window);
        }
        else
        {
            evict_from_window();
        }
    }

    void evict_from_main()
    {
        m_page_map.erase(m_main.victim_key());
        m_main.evict();
    }

    void evict_from_window()
    {
        m_page_map.erase(m_window.victim_key());
        m_window.evict();
    }
};

#endif
