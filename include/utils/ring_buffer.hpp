#pragma once
#include <atomic>
#include <memory>
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
/// Implements a simple RingBuffer
template <typename T>
class RingBuffer {
    private:
    /// The buffer
    std::unique_ptr<std::atomic<T>[]> _buffer;
    /// The buffer size
    uint64_t _size;
    /// The current insert head
    std::atomic<uint64_t> _insertCommited;
    /// The current insert counter such that we can safely insert before seen takes element
    std::atomic<uint64_t> _insertPending;
    /// The seen head
    std::atomic<uint64_t> _seenCommited;
    /// The seen counter
    std::atomic<uint64_t> _seenPending;

    public:
    /// The constructor
    RingBuffer(uint64_t size) : _buffer(new std::atomic<T>[size]()), _size(size), _insertCommited(0), _insertPending(0), _seenCommited(0), _seenPending(0) {}

    /// Insert a tuple into buffer
    template <bool wait = true>
    inline uint64_t insert(T tuple) {
        while (true) {
            auto seenHead = _seenCommited.load();
            auto curInsert = _insertPending.load();
            if (curInsert - seenHead < _size) {
                if (_insertPending.compare_exchange_weak(curInsert, curInsert + 1)) {
                    _buffer[curInsert % _size] = tuple;
                    while (_insertCommited.load() != curInsert)
                        ;
                    _insertCommited.fetch_add(1);
                    return curInsert;
                }
            } else if (!wait) {
                return ~0ull;
            }
        }
    }

    /// Insert a span into buffer
    template <bool wait = true>
    inline uint64_t insert(std::span<T> tuples) {
        while (true) {
            auto seenHead = _seenCommited.load();
            auto curInsert = _insertPending.load();
            if (curInsert - seenHead < _size) {
                if (_insertPending.compare_exchange_weak(curInsert, curInsert + tuples.size())) {
                    auto off = 0u;
                    for (auto& tuple : tuples) {
                        _buffer[(curInsert + off) % _size] = tuple;
                        off++;
                    }
                    while (_insertCommited.load() != curInsert)
                        ;
                    _insertCommited.fetch_add(tuples.size());
                    return curInsert;
                }
            } else if (!wait) {
                return ~0ull;
            }
        }
    }

    /// Consume from buffer
    template <bool wait = true>
    inline std::optional<T> consume() {
        while (true) {
            auto curInsert = _insertCommited.load();
            auto curSeen = _seenPending.load();
            if (curInsert > curSeen) {
                if (_seenPending.compare_exchange_weak(curSeen, curSeen + 1)) {
                    auto val = _buffer[curSeen % _size].load();
                    while (_seenCommited.load() != curSeen)
                        ;
                    _seenCommited.fetch_add(1);
                    return val;
                }
            } else if (!wait) {
                return std::nullopt;
            }
        }
    }

    /// Check if empty
    inline bool empty() const {
        return !(_insertCommited - _seenCommited);
    }
};
//---------------------------------------------------------------------------
} // namespace utils
} // namespace anyblob
