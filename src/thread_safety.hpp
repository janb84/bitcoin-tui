#pragma once

// Based on Bitcoin Core's threadsafety.h and stdmutex.h
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license.

#include <mutex>

#ifdef __clang__
#define LOCKABLE                          __attribute__((capability("")))
#define SCOPED_LOCKABLE                   __attribute__((scoped_lockable))
#define GUARDED_BY(x)                     __attribute__((guarded_by(x)))
#define PT_GUARDED_BY(x)                  __attribute__((pt_guarded_by(x)))
#define ACQUIRED_AFTER(...)               __attribute__((acquired_after(__VA_ARGS__)))
#define ACQUIRED_BEFORE(...)              __attribute__((acquired_before(__VA_ARGS__)))
#define EXCLUSIVE_LOCK_FUNCTION(...)      __attribute__((acquire_capability(__VA_ARGS__)))
#define SHARED_LOCK_FUNCTION(...)         __attribute__((acquire_shared_capability(__VA_ARGS__)))
#define EXCLUSIVE_TRYLOCK_FUNCTION(...)   __attribute__((try_acquire_capability(__VA_ARGS__)))
#define SHARED_TRYLOCK_FUNCTION(...)      __attribute__((try_acquire_shared_capability(__VA_ARGS__)))
#define UNLOCK_FUNCTION(...)              __attribute__((release_capability(__VA_ARGS__)))
#define SHARED_UNLOCK_FUNCTION(...)       __attribute__((release_shared_capability(__VA_ARGS__)))
#define LOCK_RETURNED(x)                  __attribute__((lock_returned(x)))
#define LOCKS_EXCLUDED(...)               __attribute__((locks_excluded(__VA_ARGS__)))
#define EXCLUSIVE_LOCKS_REQUIRED(...)     __attribute__((requires_capability(__VA_ARGS__)))
#define SHARED_LOCKS_REQUIRED(...)        __attribute__((requires_shared_capability(__VA_ARGS__)))
#define NO_THREAD_SAFETY_ANALYSIS         __attribute__((no_thread_safety_analysis))
#define ASSERT_EXCLUSIVE_LOCK(...)        __attribute__((assert_capability(__VA_ARGS__)))
#else
#define LOCKABLE
#define SCOPED_LOCKABLE
#define GUARDED_BY(x)
#define PT_GUARDED_BY(x)
#define ACQUIRED_AFTER(...)
#define ACQUIRED_BEFORE(...)
#define EXCLUSIVE_LOCK_FUNCTION(...)
#define SHARED_LOCK_FUNCTION(...)
#define EXCLUSIVE_TRYLOCK_FUNCTION(...)
#define SHARED_TRYLOCK_FUNCTION(...)
#define UNLOCK_FUNCTION(...)
#define SHARED_UNLOCK_FUNCTION(...)
#define LOCK_RETURNED(x)
#define LOCKS_EXCLUDED(...)
#define EXCLUSIVE_LOCKS_REQUIRED(...)
#define SHARED_LOCKS_REQUIRED(...)
#define NO_THREAD_SAFETY_ANALYSIS
#define ASSERT_EXCLUSIVE_LOCK(...)
#endif

// Token-paste helpers for STDLOCK.
#define PASTE(x, y)    x##y
#define PASTE2(x, y)   PASTE(x, y)
#define UNIQUE_NAME(x) PASTE2(x, __COUNTER__)

// StdMutex: std::mutex annotated for Clang Thread Safety Analysis.
// Use GUARDED_BY(mtx) on member variables, STDLOCK(mtx) to lock.
class LOCKABLE StdMutex : public std::mutex {
public:
#ifdef __clang__
    //! Negative capability: EXCLUSIVE_LOCKS_REQUIRED(!cs) means cs must NOT be held.
    const StdMutex& operator!() const { return *this; }
#endif

    // RAII guard — annotated equivalent of std::lock_guard<StdMutex>.
    class SCOPED_LOCKABLE Guard : public std::lock_guard<StdMutex> {
    public:
        explicit Guard(StdMutex& cs) EXCLUSIVE_LOCK_FUNCTION(cs)
            : std::lock_guard<StdMutex>(cs) {}
        ~Guard() UNLOCK_FUNCTION() = default;
    };

    // Returns cs after asserting it is not already held.
    // Enables compile-time re-entrant locking detection via STDLOCK.
    static StdMutex& CheckNotHeld(StdMutex& cs) EXCLUSIVE_LOCKS_REQUIRED(!cs) LOCK_RETURNED(cs) {
        return cs;
    }
};

// Locks cs after asserting it is not already held.
#define STDLOCK(cs)                                                                                \
    StdMutex::Guard UNIQUE_NAME(criticalblock) { StdMutex::CheckNotHeld(cs) }
