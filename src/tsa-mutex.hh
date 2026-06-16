// Annotated mutex for clang thread-safety analysis (-Wthread-safety,
// -Wthread-safety-negative).
//
// With -Wthread-safety-negative, a function that acquires a mutex must
// declare that the mutex is not already held (negative capability,
// `requires_capability(!m)`). The `!m` in the attribute is parsed as a
// regular C++ expression, so std::mutex cannot be used directly (it has no
// operator!); use tinyusdz::Mutex/MutexLockGuard instead and annotate the
// locking function with TUSDZ_REQUIRES_NOT(m).
#pragma once

#include <mutex>

// On Apple platforms, back Mutex with os_unfair_lock: a 4-byte
// kernel-assisted lock with priority donation, cheaper than std::mutex
// (pthread_mutex) for short critical sections. Available since macOS 10.12.
#if defined(__APPLE__)
#include <os/lock.h>
#endif

#if defined(__clang__)
#define TUSDZ_TSA(x) __attribute__((x))
#else
#define TUSDZ_TSA(x)
#endif

#define TUSDZ_TSA_CAPABILITY(x) TUSDZ_TSA(capability(x))
#define TUSDZ_TSA_SCOPED_CAPABILITY TUSDZ_TSA(scoped_lockable)
#define TUSDZ_TSA_ACQUIRE(...) TUSDZ_TSA(acquire_capability(__VA_ARGS__))
#define TUSDZ_TSA_RELEASE(...) TUSDZ_TSA(release_capability(__VA_ARGS__))
#define TUSDZ_TSA_GUARDED_BY(x) TUSDZ_TSA(guarded_by(x))
#define TUSDZ_REQUIRES_NOT(x) TUSDZ_TSA(requires_capability(!x))

namespace tinyusdz {

// std::mutex wrapper visible to clang thread-safety analysis.
class TUSDZ_TSA_CAPABILITY("mutex") Mutex {
#if defined(__APPLE__)
  os_unfair_lock m_ = OS_UNFAIR_LOCK_INIT;

 public:
  void lock() TUSDZ_TSA_ACQUIRE() { os_unfair_lock_lock(&m_); }
  void unlock() TUSDZ_TSA_RELEASE() { os_unfair_lock_unlock(&m_); }

  Mutex() = default;
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
#else
  std::mutex m_;

 public:
  void lock() TUSDZ_TSA_ACQUIRE() { m_.lock(); }
  void unlock() TUSDZ_TSA_RELEASE() { m_.unlock(); }
#endif
  // Required so `!m` is a valid expression in negative capability
  // annotations.
  const Mutex& operator!() const { return *this; }
};

// std::lock_guard equivalent for Mutex.
class TUSDZ_TSA_SCOPED_CAPABILITY MutexLockGuard {
  Mutex& m_;

 public:
  explicit MutexLockGuard(Mutex& m) TUSDZ_TSA_ACQUIRE(m) : m_(m) {
    m_.lock();
  }
  ~MutexLockGuard() TUSDZ_TSA_RELEASE() { m_.unlock(); }

  MutexLockGuard(const MutexLockGuard&) = delete;
  MutexLockGuard& operator=(const MutexLockGuard&) = delete;
};

}  // namespace tinyusdz
