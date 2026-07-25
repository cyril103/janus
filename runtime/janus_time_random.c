#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#endif

static _Atomic uint64_t janus_last_monotonic_nanoseconds;
static _Atomic uint64_t janus_seed_counter = UINT64_C(0x9e3779b97f4a7c15);

static uint64_t janus_platform_monotonic_nanoseconds(void) {
#if defined(_WIN32)
  LARGE_INTEGER counter;
  LARGE_INTEGER frequency;
  if (!QueryPerformanceCounter(&counter) ||
      !QueryPerformanceFrequency(&frequency) || counter.QuadPart < 0 ||
      frequency.QuadPart <= 0)
    abort();
  const uint64_t ticks = (uint64_t)counter.QuadPart;
  const uint64_t ticks_per_second = (uint64_t)frequency.QuadPart;
  const uint64_t whole_seconds = ticks / ticks_per_second;
  const uint64_t remainder = ticks % ticks_per_second;
  if (whole_seconds > UINT64_MAX / UINT64_C(1000000000))
    return UINT64_MAX;
  return whole_seconds * UINT64_C(1000000000) +
         (uint64_t)(((long double)remainder * 1000000000.0L) /
                    (long double)ticks_per_second);
#elif defined(__APPLE__)
  mach_timebase_info_data_t timebase;
  if (mach_timebase_info(&timebase) != KERN_SUCCESS ||
      timebase.denom == 0)
    abort();
  const uint64_t ticks = mach_absolute_time();
  const uint64_t whole = ticks / timebase.denom;
  const uint64_t remainder = ticks % timebase.denom;
  if (whole > UINT64_MAX / timebase.numer)
    return UINT64_MAX;
  return whole * timebase.numer +
         (remainder * timebase.numer) / timebase.denom;
#else
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0)
    abort();
  const uint64_t seconds = (uint64_t)value.tv_sec;
  if (seconds > UINT64_MAX / UINT64_C(1000000000))
    return UINT64_MAX;
  return seconds * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
#endif
}

uint64_t janus_monotonic_nanoseconds(void) {
  const uint64_t candidate = janus_platform_monotonic_nanoseconds();
  uint64_t previous =
      atomic_load_explicit(&janus_last_monotonic_nanoseconds,
                           memory_order_relaxed);
  while (candidate > previous) {
    if (atomic_compare_exchange_weak_explicit(
            &janus_last_monotonic_nanoseconds, &previous, candidate,
            memory_order_relaxed, memory_order_relaxed))
      return candidate;
  }
  return previous;
}

uint64_t janus_wall_nanoseconds(void) {
  struct timespec value;
  if (timespec_get(&value, TIME_UTC) != TIME_UTC || value.tv_sec < 0)
    abort();
  const uint64_t seconds = (uint64_t)value.tv_sec;
  if (seconds > UINT64_MAX / UINT64_C(1000000000))
    return UINT64_MAX;
  return seconds * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

uint64_t janus_random_mix(uint64_t value) {
  value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31);
}

uint64_t janus_random_advance(uint64_t state) {
  return state + UINT64_C(0x9e3779b97f4a7c15);
}

uint64_t janus_random_seed(void) {
  const uint64_t counter =
      atomic_fetch_add_explicit(&janus_seed_counter,
                                UINT64_C(0x9e3779b97f4a7c15),
                                memory_order_relaxed);
  const uint64_t address = (uint64_t)(uintptr_t)&janus_seed_counter;
  return janus_random_mix(counter ^ address ^
                          janus_platform_monotonic_nanoseconds() ^
                          janus_wall_nanoseconds());
}
