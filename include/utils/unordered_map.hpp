#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2021
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob {
namespace utils {
//---------------------------------------------------------------------------
// Unordered Map implementation that uses a lock per bucket
//---------------------------------------------------------------------------
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class UnorderedMap {
    private:
    /// This bucket holds the hashtable values
    struct ValueBucket {
        /// The key value pair
        std::pair<Key, Value> keyValue;
        /// The chain of value buckets
        std::unique_ptr<ValueBucket> chain;

        /// The constructor
        template <class K, class V>
        constexpr ValueBucket(K&& key, V&& value) : keyValue(std::forward<Key>(key), std::forward<Value>(value)), chain(nullptr) {}
    };

    /// This bucket is used to build the base table and includes the chain locks
    struct TableBucket {
        /// The value chain
        std::unique_ptr<ValueBucket> chain;
        /// The next bucket in the hashtable
        TableBucket* next;
        /// The lock
        std::shared_mutex sharedMutex;

        /// The constructor
        constexpr TableBucket(std::unique_ptr<ValueBucket> chain = nullptr, TableBucket* next = nullptr) : chain(move(chain)), next(next), sharedMutex() {}
    };

    /// The base table of the map
    std::vector<std::unique_ptr<TableBucket>> _map;
    /// The size
    std::atomic<size_t> _size;

    public:
    /// The iterator
    class Iterator {
        public:
        /// Be friends with the original map
        friend UnorderedMap<Key, Value, Hash>;
        /// Standard definition as forward iterator
        using iterator_category = std::forward_iterator_tag;

        private:
        /// The current table bucket
        TableBucket* _tableBucket;
        /// The reader lock of the current table bucket
        std::shared_lock<std::shared_mutex> _lock;
        /// The current chained value bucket
        ValueBucket* _valueBucket;

        /// Releases buckets, lock and unlocks if hold
        constexpr inline void release() {
            _tableBucket = nullptr;
            _valueBucket = nullptr;
            if (_lock.owns_lock())
                _lock.release()->unlock();
        }

        public:
        /// The constructor
        constexpr Iterator(TableBucket* tableBucket = nullptr, ValueBucket* valueBucket = nullptr) : _tableBucket(tableBucket), _lock(), _valueBucket(valueBucket) {}
        /// The destructor
        ~Iterator() { release(); }
        /// Copy constructor
        constexpr Iterator(Iterator& rhs) {
            release();
            _tableBucket = rhs._tableBucket;
            _valueBucket = rhs._valueBucket;
            if (_tableBucket)
                _lock = std::shared_lock(_tableBucket->sharedMutex);
        }
        /// Copy assignment constructor
        constexpr Iterator& operator=(Iterator& rhs) {
            release();
            _tableBucket = rhs._tableBucket;
            _valueBucket = rhs._valueBucket;
            if (_tableBucket)
                _lock = std::shared_lock(_tableBucket->sharedMutex);
            return *this;
        }

        /// Equality operator
        [[nodiscard]] constexpr bool operator==(const Iterator& rhs) const {
            return (_tableBucket == rhs._tableBucket) && (_valueBucket == rhs._valueBucket);
        }
        /// Non equality operator
        [[nodiscard]] constexpr bool operator!=(const Iterator& rhs) const {
            return (_tableBucket != rhs._tableBucket) || (_valueBucket != rhs._valueBucket);
        }
        /// Get the value bucket reference
        [[nodiscard]] constexpr std::pair<Key, Value>& operator*() const {
            return _valueBucket->keyValue;
        }
        /// Get the value bucket pointer
        [[nodiscard]] constexpr std::pair<Key, Value>* operator->() const {
            return &_valueBucket->keyValue;
        }

        /// The advance function of the forward iterator
        constexpr Iterator& operator++() {
            throw std::runtime_error("advancing an iterator is not implemented");
        }
    };

    /// Be friends with the map's iterator
    friend UnorderedMap<Key, Value, Hash>::Iterator;

    /// The constructor
    constexpr UnorderedMap(size_t bucketCount) : _map(), _size() {
        /// No resizing afterwards
        _map.reserve(bucketCount);
        /// Default init the table buckets
        _map.emplace_back(std::make_unique<TableBucket>());
        /// Build the chain
        for (auto i = 0ul; i < bucketCount - 1; i++) {
            _map.emplace_back(std::make_unique<TableBucket>());
            _map[i]->next = _map[i + 1].get();
        }
    }

    /// The non-const begin iterator
    [[nodiscard]] constexpr Iterator begin() { return Iterator(_map[0].get()); }
    /// The non-const end iterator
    [[nodiscard]] constexpr Iterator end() { return Iterator(); }

    /// Returns the number of buckets
    [[nodiscard]] constexpr size_t buckets() const { return _map.size(); }
    /// Returns the size of the unordered map
    [[nodiscard]] constexpr size_t size() const { return _size; }

    /// Insert element, returns iterator
    template <class K, class V>
    [[nodiscard("Use push instead for better performance")]] constexpr Iterator insert(K&& key, V&& value) {
        {
            auto position = Hash{}(key) % buckets();
            std::unique_lock lock(_map[position]->sharedMutex);
            auto* chain = &_map[position]->chain;
            while (chain->get()) {
                if (chain->get()->keyValue.first == key)
                    return end();
                chain = &chain->get()->chain;
            }
            *chain = std::make_unique<ValueBucket>(std::forward<Key>(key), std::forward<Value>(value));
            _size++;
        }
        return find(key);
    }

    /// Insert element, returns true or false on matching key
    template <class K, class V>
    constexpr bool push(K&& key, V&& value) {
        {
            auto position = Hash{}(key) % buckets();
            std::unique_lock lock(_map[position]->sharedMutex);
            auto* chain = &_map[position]->chain;
            while (chain->get()) {
                if (chain->get()->keyValue.first == key)
                    return false;
                chain = &chain->get()->chain;
            }
            *chain = std::make_unique<ValueBucket>(std::forward<Key>(key), std::forward<Value>(value));
            _size++;
        }
        return true;
    }

    /// Erase element
    template <class K>
    [[nodiscard]] constexpr bool erase(K&& key) {
        auto position = Hash{}(key) % buckets();
        std::unique_lock lock(_map[position]->sharedMutex);
        auto* chain = &_map[position]->chain;
        while (chain->get()) {
            if (chain->get()->keyValue.first == key) {
                *chain = std::move((*chain)->chain);
                _size--;
                return true;
            }
            chain = &chain->get()->chain;
        }
        return false;
    }

    /// Erase element from iterator
    [[nodiscard]] constexpr bool erase(Iterator it) {
        auto key = it->first;
        it.release();
        return erase(std::move(key));
    }

    /// Find element, returns iterator
    template <class K>
    [[nodiscard]] constexpr Iterator find(K&& key) {
        auto position = Hash{}(key) % buckets();
        std::shared_lock lock(_map[position]->sharedMutex);
        auto* chain = &_map[position]->chain;
        while (chain->get()) {
            if (chain->get()->keyValue.first == key)
                return Iterator(_map[position].get(), chain->get());
            chain = &chain->get()->chain;
        }
        return end();
    }
};
//---------------------------------------------------------------------------
} // namespace utils
} // namespace anyblob
