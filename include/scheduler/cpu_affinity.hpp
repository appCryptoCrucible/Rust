#pragma once

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

inline void PinCurrentThreadToCore(unsigned int core_index) {
#if defined(_WIN32)
	if (core_index >= (sizeof(DWORD_PTR) * 8)) return;
	DWORD_PTR mask = static_cast<DWORD_PTR>(1ULL) << core_index;
	SetThreadAffinityMask(GetCurrentThread(), mask);
#else
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(core_index, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}


// Cross-platform light wrapper to optionally pin current thread to a CPU core.
// On non-Windows, this is a no-op for now.

inline void PinCurrentThreadToCore(int core_index) {
#if defined(_WIN32)
  if (core_index < 0) return;
  // Windows thread affinity mask uses bitmask for cores
  DWORD_PTR mask = 1ULL << (core_index % (8 * sizeof(DWORD_PTR)));
  HANDLE thread = GetCurrentThread();
  SetThreadAffinityMask(thread, mask);
#else
  (void)core_index;
#endif
}


