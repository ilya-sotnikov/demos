#pragma once

#include "Common.hpp"

#include <initializer_list>

// WARN: Only use it in function arguments, see the comment for the
// std::initializer_list constructor.
template<typename T>
struct SliceArg {
    const T* data;
    int count;

    SliceArg(T* arr, int size) {
        data = arr;
        count = size;
    }

    template<int N>
    SliceArg(const T (&arr)[N]) {
        data = arr;
        count = N;
    }

    // WARN: This is very dangerous, since the lifetime of std::initializer_list
    // is short, never use it outside of function arguments.
    SliceArg(std::initializer_list<T> list) {
        data = list.begin();
        count = int(list.size());
    }

    int get_size_bytes() const {
        return count * int(sizeof(T));
    }

    T* begin() {
        return data;
    }

    const T* begin() const {
        return data;
    }

    T* end() {
        return data + count;
    }

    const T* end() const {
        return data + count;
    }

    T& operator[](int i) {
        return data[i];
    }

    const T& operator[](int i) const {
        return data[i];
    }
};
