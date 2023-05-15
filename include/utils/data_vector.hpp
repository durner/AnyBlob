#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
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
/// Minimal version of a vector that only allwos to store raw data
template <typename T>
class DataVector {
    private:
    /// Current capacity
    uint64_t _capacity;
    /// Current size
    uint64_t _size;
    /// The data
    std::unique_ptr<T[]> _dataOwned;
    /// The data not owned
    T* _data;

    public:
    /// Constructor
    DataVector() : _capacity(0), _size(0) {}

    /// Constructor with size
    DataVector(uint64_t cap) : _capacity(0), _size(0) {
        resize(cap);
    }

    /// Copy constructor
    DataVector(DataVector& rhs) : _capacity(0), _size(0) {
        reserve(rhs._capacity);
        _size = rhs._size;
        std::memcpy(data(), rhs.data(), size() * sizeof(T));
    }

    /// Constructor from other pointers
    DataVector(T* start, T* end) : _capacity(0), _size(0) {
        resize(end - start);
        std::memcpy(data(), start, size() * sizeof(T));
    }

    /// Constructor unowned, map to other pointer, capacity given in number of T elements
    DataVector(T* ptr, uint64_t capacity) : _capacity(capacity * sizeof(T)), _size(0) {
        _data = ptr;
    }

    /// Get the data
    inline T* data() {
        return _data;
    }

    /// Get the data
    inline const T* cdata() const {
        return _data;
    }

    /// Get the size
    inline uint64_t size() const {
        return _size;
    }

    /// Get the capacity
    inline uint64_t capacity() const {
        return _capacity;
    }

    /// Clear the size
    inline void clear() {
        _size = 0;
    }

    /// Increase the capacity
    inline void reserve(uint64_t cap) {
        if (_capacity < cap) {
            if (!_dataOwned && _capacity)
                throw std::runtime_error("Pointer not owned, thus size is fixed!");
            auto swap = std::unique_ptr<T[]>(new T[cap]());
            if (_data && _size)
                std::memcpy(swap.get(), data(), _size * sizeof(T));
            _dataOwned.swap(swap);
            _data = _dataOwned.get();
            _capacity = cap;
        }
    }

    /// Change the number of elements
    inline void resize(uint64_t size) {
        if (size > _capacity) {
            reserve(size);
        }
        _size = size;
    }

    /// Transfer the ownership of the data
    inline std::unique_ptr<T[]> transferBuffer() {
        return move(_dataOwned);
    }
};
//---------------------------------------------------------------------------
}; // namespace utils
}; // namespace anyblob
