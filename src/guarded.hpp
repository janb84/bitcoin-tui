#pragma once

#include <concepts>
#include <utility>

#include "thread_safety.hpp"

template <typename T> class Guarded {
  public:
    ~Guarded() = default;

    Guarded(const Guarded&)            = delete;
    Guarded& operator=(const Guarded&) = delete;
    Guarded(Guarded&&)                 = delete;
    Guarded& operator=(Guarded&&)      = delete;

    Guarded()
        requires std::default_initializable<T>
        : value_{} {}

    explicit Guarded(T v) : value_{std::move(v)} {}

    T get() const EXCLUSIVE_LOCKS_REQUIRED(!mtx_) {
        STDLOCK(mtx_);
        return value_;
    }

    operator T() const EXCLUSIVE_LOCKS_REQUIRED(!mtx_) { return get(); }

    Guarded& operator=(const T& value) EXCLUSIVE_LOCKS_REQUIRED(!mtx_) {
        STDLOCK(mtx_);
        value_ = value;
        return *this;
    }

    Guarded& operator=(T&& value) EXCLUSIVE_LOCKS_REQUIRED(!mtx_) {
        STDLOCK(mtx_);
        value_ = std::move(value);
        return *this;
    }

    template <typename Fn>
        requires(std::is_reference_v<std::invoke_result_t<Fn, const T&>>)
    auto access(Fn&& fn) const = delete;

    template <typename Fn> auto access(Fn&& fn) const EXCLUSIVE_LOCKS_REQUIRED(!mtx_) {
        STDLOCK(mtx_);
        return fn(value_);
    }

    template <typename Fn>
        requires(std::is_reference_v<std::invoke_result_t<Fn, T&>>)
    auto update(Fn&& fn) = delete;

    template <typename Fn> auto update(Fn&& fn) EXCLUSIVE_LOCKS_REQUIRED(!mtx_) {
        STDLOCK(mtx_);
        return fn(value_);
    }

  private:
    mutable StdMutex mtx_;
    T value_         GUARDED_BY(mtx_);
};
