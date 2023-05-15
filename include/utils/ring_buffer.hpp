#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
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
/// An atomic RingBuffer
template <typename T>
class RingBuffer {
    public:
    /// A simple spin mutex
    class SpinMutex {
        std::atomic<bool> bit;

        public:
        /// The constructor
        SpinMutex() : bit() {}

        /// Lock
        void lock() {
            auto expected = false;
            while (!bit.compare_exchange_weak(expected, true, std::memory_order_acq_rel)) {
                expected = false;
            }
        }
        /// Unlock
        void unlock() { bit.store(false, std::memory_order_release); }
    };

    private:
    /// The buffer
    std::unique_ptr<T[]> _buffer;
    /// The buffer size
    uint64_t _size;
    /// The insert cache-aligned struct
    struct alignas(64) {
        /// The current insert head
        std::atomic<uint64_t> pending;
        /// The current insert counter such that we can safely insert before seen takes element
        std::atomic<uint64_t> commited;
        /// The insert flag for spinning
        SpinMutex mutex;
    } _insert;
    /// The seen cache-alignedstruct
    struct alignas(64) {
        /// The seen head
        std::atomic<uint64_t> pending;
        /// The seen counter
        std::atomic<uint64_t> commited;
        /// The seen flag for spinning
        SpinMutex mutex;
    } _seen;

    public:
    /// The constructor
    RingBuffer(uint64_t size) : _buffer(new T[size]()), _size(size) {}

    /// Insert a tuple into buffer
    template <bool wait = false>
    inline uint64_t insert(T tuple) {
        while (true) {
            std::unique_lock lock(_insert.mutex);
            auto seenHead = _seen.commited.load(std::memory_order_acquire);
            auto curInsert = _insert.pending.load(std::memory_order_acquire);
            if (curInsert - seenHead < _size) {
                _insert.pending.fetch_add(1, std::memory_order_release);
                _buffer[curInsert % _size] = tuple;
                _insert.commited.fetch_add(1, std::memory_order_release);
                return curInsert;
            } else if (!wait) {
                return ~0ull;
            }
        }
    }

    /// Insert a span into buffer
    template <bool wait = false>
    inline uint64_t insertAll(std::span<T> tuples) {
        while (true) {
            std::unique_lock lock(_insert.mutex);
            auto seenHead = _seen.commited.load();
            auto curInsert = _insert.pending.load();
            if (_size >= tuples.size() && curInsert - seenHead < _size + 1 - tuples.size()) {
                _insert.pending.fetch_add(tuples.size(), std::memory_order_release);
                auto off = 0u;
                for (auto& tuple : tuples) {
                    _buffer[(curInsert + off) % _size] = tuple;
                    off++;
                }
                _insert.commited.fetch_add(tuples.size(), std::memory_order_release);
                return curInsert;
            } else if (!wait) {
                return ~0ull;
            }
        }
    }

    /// Consume from buffer
    template <bool wait = false>
    inline std::optional<T> consume() {
        while (true) {
            std::unique_lock lock(_seen.mutex);
            auto curInsert = _insert.commited.load(std::memory_order_acquire);
            auto curSeen = _seen.pending.load(std::memory_order_acquire);
            if (curInsert > curSeen) {
                _seen.pending.fetch_add(1, std::memory_order_release);
                auto val = _buffer[curSeen % _size];
                _seen.commited.fetch_add(1, std::memory_order_release);
                return val;
            } else if (!wait) {
                return std::nullopt;
            }
        }
    }

    /// Check if empty
    inline bool empty() const {
        return !(_insert.commited.load(std::memory_order_acquire) - _seen.commited.load(std::memory_order_acquire));
    }
};
//---------------------------------------------------------------------------
} // namespace utils
} // namespace anyblob
