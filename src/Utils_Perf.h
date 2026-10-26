#pragma once

#include <cstddef>

// ============================================================================
// Авто-определение лимитов LRU по доступной памяти
// ============================================================================
struct LRULimits {
  int rowLRU;
  int tileLRU;
};

size_t GetAvailableRAM_MB();
LRULimits GetRecommendedLRULimits();
inline size_t GetLowMemoryThreshold_MB() { return 4096; }

double GetSystemCPULoadPercent();

enum class PerformanceMode { Eco, Balanced, Fast };

struct PerformanceTuning {
  int maxConcurrentLoads;
  size_t maxTotalPending;
  size_t basePrefetch;
  LRULimits lru;
};

PerformanceMode DetectPerformanceMode(size_t availMB, unsigned cores,
                                      size_t modelCount);

PerformanceTuning GetPerformanceTuning(size_t availMB, unsigned cores,
                                       size_t modelCount);
                                       