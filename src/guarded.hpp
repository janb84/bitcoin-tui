#pragma once

#include <chrono>
#include <concepts>
#include <condition_variable>
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

  protected:
    mutable StdMutex mtx_;
    T value_         GUARDED_BY(mtx_);
};

template <typename T> class WaitableGuarded : public Guarded<T> {
  public:
    using Guarded<T>::Guarded;

    template <typename Fn>
        requires(std::is_reference_v<std::invoke_result_t<Fn, T&>>)
    auto update_and_notify(Fn&& fn) = delete;

    template <typename Fn>
        requires std::is_void_v<std::invoke_result_t<Fn, T&>>
    void update_and_notify(Fn&& fn) EXCLUSIVE_LOCKS_REQUIRED(!this->mtx_) {
        STDLOCK(this->mtx_);
        fn(this->value_);
        cv_.notify_one();
    }

    template <typename Fn>
        requires(!std::is_void_v<std::invoke_result_t<Fn, T&>>)
    auto update_and_notify(Fn&& fn) EXCLUSIVE_LOCKS_REQUIRED(!this->mtx_) {
        STDLOCK(this->mtx_);
        auto result = fn(this->value_);
        cv_.notify_one();
        return result;
    }

    void notify() { cv_.notify_one(); }

    template <typename Clock, typename Duration>
    void wait(std::chrono::time_point<Clock, Duration> deadline)
        EXCLUSIVE_LOCKS_REQUIRED(!this->mtx_) {
        StdUniqueLock lock(this->mtx_);
        cv_.wait_until(lock, deadline);
    }

    template <typename Clock, typename Duration, typename Pred>
    void wait_until(std::chrono::time_point<Clock, Duration> deadline, Pred&& pred)
        EXCLUSIVE_LOCKS_REQUIRED(!this->mtx_) {
        StdUniqueLock lock(this->mtx_);
        cv_.wait_until(lock, deadline,
                       [&]() NO_THREAD_SAFETY_ANALYSIS { return pred(this->value_); });
    }

    template <typename Fn>
        requires(std::is_reference_v<std::invoke_result_t<Fn, T&>>)
    auto access_when(auto&&, Fn&& fn) = delete;

    template <typename Pred, typename Fn>
    auto access_when(Pred&& pred, Fn&& fn) EXCLUSIVE_LOCKS_REQUIRED(!this->mtx_) {
        StdUniqueLock lock(this->mtx_);
        cv_.wait(lock, [&]() NO_THREAD_SAFETY_ANALYSIS { return pred(this->value_); });
        return fn(this->value_);
    }

  private:
    std::condition_variable_any cv_;
};
