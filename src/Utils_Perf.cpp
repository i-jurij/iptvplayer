#include "Utils_Perf.h"
#include "Utils_Platform.h"

#include <algorithm>
#include <string>
#include <thread>

// ----------------------------------------------------------------------------
// Per-OS includes
// ----------------------------------------------------------------------------
#if defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <mach/host_info.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// ============================================================================
// Sleep helper
// ============================================================================
static inline void SleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ============================================================================
// CPU load
// ============================================================================

#if defined(__linux__)
static bool ReadProcStat(uint64_t &idle, uint64_t &total) {
  std::ifstream f("/proc/stat");
  if (!f.is_open())
    return false;
  std::string line;
  if (!std::getline(f, line))
    return false;
  const char *s = line.c_str();
  while (*s && !(*s >= '0' && *s <= '9'))
    ++s;
  uint64_t user = 0, nice = 0, system = 0, idlev = 0, iowait = 0, irq = 0,
           softirq = 0, steal = 0, guest = 0, guest_nice = 0;
  sscanf(s, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &system,
         &idlev, &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
  idle = idlev + iowait;
  total = user + nice + system + idlev + iowait + irq + softirq + steal +
          guest + guest_nice;
  return true;
}
#endif

#if defined(__APPLE__)
static bool ReadMachCPUTicks(uint64_t &idle, uint64_t &total) {
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  host_cpu_load_info_data_t cpuinfo;
  kern_return_t kr = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                                     (host_info_t)&cpuinfo, &count);
  if (kr != KERN_SUCCESS)
    return false;
  uint64_t user = cpuinfo.cpu_ticks[CPU_STATE_USER];
  uint64_t nice = cpuinfo.cpu_ticks[CPU_STATE_NICE];
  uint64_t system = cpuinfo.cpu_ticks[CPU_STATE_SYSTEM];
  uint64_t idlev = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
  idle = idlev;
  total = user + nice + system + idlev;
  return true;
}
#endif

#if defined(_WIN32)
static bool ReadWindowsCPUTimes(uint64_t &idle, uint64_t &total) {
  FILETIME idleTime, kernelTime, userTime;
  if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
    return false;
  auto FileTimeToUint64 = [](const FILETIME &ft) -> uint64_t {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
           static_cast<uint64_t>(ft.dwLowDateTime);
  };
  uint64_t idle64 = FileTimeToUint64(idleTime);
  uint64_t kernel64 = FileTimeToUint64(kernelTime);
  uint64_t user64 = FileTimeToUint64(userTime);
  uint64_t system64 = kernel64 - idle64;
  idle = idle64;
  total = idle64 + system64 + user64;
  return true;
}
#endif

double GetSystemCPULoadPercent() {
  if (IsLinuxPlatform()) {
#if defined(__linux__)
    uint64_t idle1 = 0, total1 = 0;
    if (!ReadProcStat(idle1, total1))
      return -1.0;
    SleepMs(120);
    uint64_t idle2 = 0, total2 = 0;
    if (!ReadProcStat(idle2, total2))
      return -1.0;
    uint64_t idleDelta = (idle2 > idle1) ? (idle2 - idle1) : 0;
    uint64_t totalDelta = (total2 > total1) ? (total2 - total1) : 0;
    if (totalDelta == 0)
      return -1.0;
    double usage = (1.0 - (double)idleDelta / (double)totalDelta) * 100.0;
    if (usage < 0.0)
      usage = 0.0;
    if (usage > 100.0)
      usage = 100.0;
    return usage;
#else
    return -1.0;
#endif
  } else if (IsMacPlatform()) {
#if defined(__APPLE__)
    uint64_t idle1 = 0, total1 = 0;
    if (!ReadMachCPUTicks(idle1, total1))
      return -1.0;
    SleepMs(120);
    uint64_t idle2 = 0, total2 = 0;
    if (!ReadMachCPUTicks(idle2, total2))
      return -1.0;
    uint64_t idleDelta = (idle2 > idle1) ? (idle2 - idle1) : 0;
    uint64_t totalDelta = (total2 > total1) ? (total2 - total1) : 0;
    if (totalDelta == 0)
      return -1.0;
    double usage = (1.0 - (double)idleDelta / (double)totalDelta) * 100.0;
    if (usage < 0.0)
      usage = 0.0;
    if (usage > 100.0)
      usage = 100.0;
    return usage;
#else
    return -1.0;
#endif
  } else if (IsWindowsPlatform()) {
#if defined(_WIN32)
    uint64_t idle1 = 0, total1 = 0;
    if (!ReadWindowsCPUTimes(idle1, total1))
      return -1.0;
    SleepMs(120);
    uint64_t idle2 = 0, total2 = 0;
    if (!ReadWindowsCPUTimes(idle2, total2))
      return -1.0;
    uint64_t idleDelta = (idle2 > idle1) ? (idle2 - idle1) : 0;
    uint64_t totalDelta = (total2 > total1) ? (total2 - total1) : 0;
    if (totalDelta == 0)
      return -1.0;
    double usage = (1.0 - (double)idleDelta / (double)totalDelta) * 100.0;
    if (usage < 0.0)
      usage = 0.0;
    if (usage > 100.0)
      usage = 100.0;
    return usage;
#else
    return -1.0;
#endif
  } else {
    return -1.0;
  }
}

// ============================================================================
// Доступная RAM
// ============================================================================
size_t GetAvailableRAM_MB() {
  if (IsWindowsPlatform()) {
#if defined(_WIN32)
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex)) {
      return static_cast<size_t>(statex.ullAvailPhys / (1024 * 1024));
    }
    return 4096;
#else
    return 4096;
#endif
  } else if (IsMacPlatform()) {
#if defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, nullptr, 0) == 0) {
      return static_cast<size_t>(memsize / (1024 * 1024));
    }
    return 4096;
#else
    return 4096;
#endif
  } else if (IsLinuxPlatform()) {
#if defined(__linux__)
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo.is_open()) {
      std::string line;
      while (std::getline(meminfo, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
          size_t kb = 0;
          if (sscanf(line.c_str(), "MemAvailable: %zu kB", &kb) == 1) {
            return kb / 1024;
          }
        }
      }
    }
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
      return static_cast<size_t>((static_cast<long long>(pages) * page_size) /
                                 (1024 * 1024));
    }
    return 4096;
#else
    return 4096;
#endif
  } else {
    return 4096;
  }
}

// ============================================================================
// Performance tuning
// ============================================================================
PerformanceMode DetectPerformanceMode(size_t availMB, unsigned cores,
                                      size_t modelCount) {
  int score = 0;

  if (availMB < 4096)
    score += 0;
  else if (availMB < 8192)
    score += 1;
  else
    score += 2;

  if (cores <= 2)
    score += 0;
  else if (cores <= 6)
    score += 1;
  else
    score += 2;

  if (modelCount < 2000)
    score += 2;
  else if (modelCount < 10000)
    score += 1;
  else
    score += 0;

  if (score <= 2)
    return PerformanceMode::Eco;
  if (score <= 4)
    return PerformanceMode::Balanced;
  return PerformanceMode::Fast;
}

PerformanceTuning GetPerformanceTuning(size_t availMB, unsigned cores,
                                       size_t modelCount) {
  PerformanceMode mode = DetectPerformanceMode(availMB, cores, modelCount);
  PerformanceTuning t{};

  switch (mode) {
  case PerformanceMode::Eco:
    t.maxConcurrentLoads = 2;
    t.maxTotalPending = 600;
    t.basePrefetch = 200;
    t.lru = {80, 200};
    break;

  case PerformanceMode::Balanced:
    t.maxConcurrentLoads = 6;
    t.maxTotalPending = 2500;
    t.basePrefetch = 1000;
    t.lru = {100, 300};
    break;

  case PerformanceMode::Fast:
    t.maxConcurrentLoads = 10;
    t.maxTotalPending = 8000;
    t.basePrefetch = 3000;
    t.lru = {140, 400};
    break;
  }

  return t;
}

LRULimits GetRecommendedLRULimits() {
  const size_t availableMB = GetAvailableRAM_MB();
  unsigned cores = std::max<unsigned>(1, std::thread::hardware_concurrency());
  PerformanceTuning t = GetPerformanceTuning(availableMB, cores, 0);
  return t.lru;
}
