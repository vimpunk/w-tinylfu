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
 * Values are stored in shared_ptr<V> instances in order to ensure memory safety when
 * a cache entry is evicted while it is still being used by user.
 *
 * It is advised that trivially copiable, small keys be used as there persist two
 * copies of each within the cache.
 *
 * NOTE: it is NOT thread-safe!
 */
template<
    typename K,
    typename V
> class wtinylfu_cache
{
    enum class cache_slot
    {
        window,
        probationary,
        eden
    };

    struct page
    {
        K key;
        enum cache_slot cache_slot;
        std::shared_ptr<V> data;

        page(K key_, enum cache_slot cache_slot_, std::shared_ptr<V> data_)
            : key(std::move(key_))
            , cache_slot(cache_slot_)
            , data(data_)
        {}
    };

    class lru
    {
        std::list<page> lru_;
        int capacity_;

    public:
        using page_position = typename std::list<page>::iterator;
        using const_page_position = typename std::list<page>::const_iterator;

        explicit lru(int capacity) : capacity_(capacity) {}

        int size() const noexcept { return lru_.size(); }
        int capacity() const noexcept { return capacity_; }
        bool is_full() const noexcept { return size() >= capacity(); }

        /**
         * NOTE: doesn't actually remove any pages, it only sets the capacity.
         *
         * This is because otherwise there'd be no way to delete the corresponding
         * entries from the page map outside of this LRU instance, so this is handled
         * externally.
         */
        void set_capacity(const int n) noexcept { capacity_ = n; }

        /** Returns the position of the hottest (most recently used) page. */
        page_position mru_pos() noexcept { return lru_.begin(); }
        const_page_position mru_pos() const noexcept { return lru_.begin(); }

        /** Returns the position of the coldest (least recently used) page. */
        page_position lru_pos() noexcept { return --lru_.end(); }
        const_page_position lru_pos() const noexcept { return --lru_.end(); }

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
            lru_.erase(page);
        }

        /** Inserts new page at the MRU position of the cache. */
        template<typename... Args>
        page_position insert(Args&&... args)
        {
            return lru_.emplace(mru_pos(), std::forward<Args>(args)...);
        }

        /** Moves page to the MRU position. */
        void handle_hit(page_position page)
        {
            transfer_page_from(page, *this);
        }

        /** Moves page from $source to the MRU position of this cache. */
        void transfer_page_from(page_position page, lru& source)
        {
            lru_.splice(mru_pos(), source.lru_, page);
        }
    };

    /**
     * A cache which is divided into two segments, a probationary and an eden
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
     * reach their capacity, a new entry is replaced with the LRU victim of the
     * probationary segment.
     *
     * In this implementation 80% of the capacity is allocated to the eden (or "hot")
     * pages and 20% for pages under probation (the "cold" pages).
     */
    class slru
    {
        lru eden_;
        lru probationary_;

    public:
        using page_position = typename lru::page_position;
        using const_page_position = typename lru::const_page_position;

        explicit slru(int capacity) : slru(0.8f * capacity, capacity - 0.8f * capacity)
        {
            // correct truncation error
            if(this->capacity() < capacity)
            {
                eden_.set_capacity(eden_.capacity() + 1);
            }
        }

        slru(int eden_capacity, int probationary_capacity)
            : eden_(eden_capacity)
            , probationary_(probationary_capacity)
        {}

        const int size() const noexcept
        {
            return eden_.size() + probationary_.size();
        }

        const int capacity() const noexcept
        {
            return eden_.capacity() + probationary_.capacity();
        }

        const bool is_full() const noexcept
        {
            return size() >= capacity();
        }

        void set_capacity(const int n)
        {
            eden_.set_capacity(0.8f * n);
            probationary_.set_capacity(n - eden_.capacity());
        }

        page_position victim_pos() noexcept
        {
            return probationary_.lru_pos();
        }

        const_page_position victim_pos() const noexcept
        {
            return probationary_.lru_pos();
        }

        const K& victim_key() const noexcept
        {
            return victim_pos()->key;
        }

        void evict()
        {
            probationary_.evict();
        }

        void erase(page_position page)
        {
            if(page->cache_slot == cache_slot::eden)
                eden_.erase(page);
            else
                probationary_.erase(page);
        }

        /** Moves page to the MRU position of the probationary segment. */
        void transfer_page_from(page_position page, lru& source)
        {
            probationary_.transfer_page_from(page, source);
            page->cache_slot = cache_slot::probationary;
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
            if(page->cache_slot == cache_slot::probationary)
            {
                promote_to_eden(page);
                if(eden_.is_full()) { demote_to_probationary(eden_.lru_pos()); }
            }
            else
            {
                assert(page->cache_slot == cache_slot::eden); // this shouldn't happen
                eden_.handle_hit(page);
            }
        }

    private:
        void promote_to_eden(page_position page)
        {
            eden_.transfer_page_from(page, probationary_);
            page->cache_slot = cache_slot::eden;
        }

        void demote_to_probationary(page_position page)
        {
            probationary_.transfer_page_from(page, eden_);
            page->cache_slot = cache_slot::probationary;
        }
    };

    frequency_sketch<K> filter_;

    // Maps keys to page positions of the LRU caches pointing to a page.
    std::map<K, typename lru::page_position> page_map_;

    // Allocated 1% of the total capacity. Window victims are granted the chance to
    // reenter the cache (into $main_). This is to remediate the problem where sparse
    // bursts cause repeated misses in the regular TinyLfu architecture.
    lru window_;

    // Allocated 99% of the total capacity.
    slru main_;

    // Statistics.
    int num_cache_hits_ = 0;
    int num_cache_misses_ = 0;

public:
    explicit wtinylfu_cache(int capacity)
        : filter_(capacity)
        , window_(window_capacity(capacity))
        , main_(capacity - window_.capacity())
    {}

    int size() const noexcept
    {
        return window_.size() + main_.size();
    }

    int capacity() const noexcept
    {
        return window_.capacity() + main_.capacity();
    }

    int num_cache_hits() const noexcept { return num_cache_hits_; }
    int num_cache_misses() const noexcept { return num_cache_misses_; }

    bool contains(const K& key) const noexcept
    {
        return page_map_.find(key) != page_map_.cend();
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

        filter_.change_capacity(n);
        window_.set_capacity(window_capacity(n));
        main_.set_capacity(n - window_.capacity());

        while(window_.is_full()) { evict_from_window(); }
        while(main_.is_full()) { evict_from_main(); }
    }

    std::shared_ptr<V> get(const K& key)
    {
        filter_.record_access(key);
        auto it = page_map_.find(key);
        if(it != page_map_.end())
        {
            auto& page = it->second;
            handle_hit(page);
            return page->data;
        }
        ++num_cache_misses_;
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
        auto it = page_map_.find(key);
        if(it != page_map_.end())
        {
            auto& page = it->second;
            if(page->cache_slot == cache_slot::window)
                window_.erase(page);
            else
                main_.erase(page);
            page_map_.erase(it);
        }
    }

private:
    static int window_capacity(const int total_capacity) noexcept
    {
        return std::max(1, int(std::ceil(0.01f * total_capacity)));
    }

    void insert(const K& key, std::shared_ptr<V> data)
    {
        if(window_.is_full()) { evict(); }

        auto it = page_map_.find(key);
        if(it != page_map_.end())
            it->second->data = data;
        else
            page_map_.emplace(key, window_.insert(key, cache_slot::window, data));
    }

    void handle_hit(typename lru::page_position page)
    {
        if(page->cache_slot == cache_slot::window)
            window_.handle_hit(page);
        else
            main_.handle_hit(page);
        ++num_cache_hits_;
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
            evict_from_window_or_main();
        else
            main_.transfer_page_from(window_.lru_pos(), window_);
    }

    void evict_from_window_or_main()
    {
        const int window_victim_freq = filter_.frequency(window_.victim_key());
        const int main_victim_freq = filter_.frequency(main_.victim_key());
        if(window_victim_freq > main_victim_freq)
        {
            evict_from_main();
            main_.transfer_page_from(window_.lru_pos(), window_);
        }
        else
        {
            evict_from_window();
        }
    }

    void evict_from_main()
    {
        page_map_.erase(main_.victim_key());
        main_.evict();
    }

    void evict_from_window()
    {
        page_map_.erase(window_.victim_key());
        window_.evict();
    }
};

#endif
