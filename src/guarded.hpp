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

    Guarded& operator=(const T& value) {
        STDLOCK(mtx_);
        value_ = value;
        return *this;
    }

    Guarded& operator=(T&& value) {
        STDLOCK(mtx_);
        value_ = std::move(value);
        return *this;
    }

    template <typename Fn> auto access(Fn&& fn) const {
        STDLOCK(mtx_);
        return fn(value_);
    }

    template <typename Fn> auto update(Fn&& fn) {
        STDLOCK(mtx_);
        return fn(value_);
    }
};
