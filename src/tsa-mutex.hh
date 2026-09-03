// Annotated mutex for clang thread-safety analysis (-Wthread-safety,
// -Wthread-safety-negative).
//
// With -Wthread-safety-negative, a function that acquires a mutex must
// declare that the mutex is not already held (negative capability,
// `requires_capability(!m)`). The `!m` in the attribute is parsed as a
// regular C++ expression, so std::mutex cannot be used directly (it has no
// operator!); use lightusd::Mutex/MutexLockGuard instead and annotate the
// locking function with LIGHTUSD_REQUIRES_NOT(m).
#pragma once

#include <mutex>

#include "compiler-features.hh"

// On Apple platforms, back Mutex with os_unfair_lock: a 4-byte
// kernel-assisted lock with priority donation, cheaper than std::mutex
// (pthread_mutex) for short critical sections. Available since macOS 10.12.
#if defined(__APPLE__)
#include <os/lock.h>
#endif

#if defined(__clang__)
#define LIGHTUSD_TSA(x) __attribute__((x))
#else
#define LIGHTUSD_TSA(x)
#endif

#define LIGHTUSD_TSA_CAPABILITY(x) LIGHTUSD_TSA(capability(x))
#define LIGHTUSD_TSA_SCOPED_CAPABILITY LIGHTUSD_TSA(scoped_lockable)
#define LIGHTUSD_TSA_ACQUIRE(...) LIGHTUSD_TSA(acquire_capability(__VA_ARGS__))
#define LIGHTUSD_TSA_RELEASE(...) LIGHTUSD_TSA(release_capability(__VA_ARGS__))
#define LIGHTUSD_TSA_GUARDED_BY(x) LIGHTUSD_TSA(guarded_by(x))
#define LIGHTUSD_REQUIRES_NOT(x) LIGHTUSD_TSA(requires_capability(!x))

namespace lightusd {

// std::mutex wrapper visible to clang thread-safety analysis.
class LIGHTUSD_TSA_CAPABILITY("mutex") Mutex {
#if defined(__APPLE__)
  os_unfair_lock m_ = OS_UNFAIR_LOCK_INIT;

 public:
  void lock() LIGHTUSD_TSA_ACQUIRE() { os_unfair_lock_lock(&m_); }
  void unlock() LIGHTUSD_TSA_RELEASE() { os_unfair_lock_unlock(&m_); }

  Mutex() = default;
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
#else
  std::mutex m_;

 public:
  void lock() LIGHTUSD_TSA_ACQUIRE() { m_.lock(); }
  void unlock() LIGHTUSD_TSA_RELEASE() { m_.unlock(); }
#endif
  // Required so `!m` is a valid expression in negative capability
  // annotations.
  const Mutex& operator!() const LIGHTUSD_LIFETIMEBOUND { return *this; }
};

// std::lock_guard equivalent for Mutex.
class LIGHTUSD_TSA_SCOPED_CAPABILITY MutexLockGuard {
  Mutex& m_;

 public:
  explicit MutexLockGuard(Mutex& m) LIGHTUSD_TSA_ACQUIRE(m) : m_(m) {
    m_.lock();
  }
  ~MutexLockGuard() LIGHTUSD_TSA_RELEASE() { m_.unlock(); }

  MutexLockGuard(const MutexLockGuard&) = delete;
  MutexLockGuard& operator=(const MutexLockGuard&) = delete;
};

}  // namespace lightusd
