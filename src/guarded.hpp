#pragma once

#include <utility>

#include "thread_safety.hpp"

template <typename T> class Guarded {
  private:
    mutable StdMutex mtx_;
    T value_         GUARDED_BY(mtx_);

  public:
    Guarded()            = default;
    Guarded(Guarded&& v) = default;
    ~Guarded()           = default;

    explicit Guarded(T v) : value_{std::move(v)} {}

    T get() const {
        STDLOCK(mtx_);
        return value_;
    }

    operator T() const { return get(); }

    template <typename Fn> auto update(Fn&& fn) {
        STDLOCK(mtx_);
        return fn(value_);
    }
};
