// Загрузка мастеров, контроль, лимиты, дампы
#define __LOGOCACHE_CPP__

#include "IconManager.h"
#include "LogControl.h"
#include "LogoCache.h"
#include "Profiler.h"
#include "Utils.h"
#include <atomic>
#include <mutex>
#include <unordered_set>

// --- Rate limiting helpers ---
static std::atomic<uint64_t> s_lastAdjustMs(0);
static std::atomic<uint64_t> s_lastDebugMs(0);
static std::atomic<size_t> s_adjustCounter(0);

static const size_t ADJUST_EVERY_N = 64;
static const uint64_t ADJUST_INTERVAL_MS = 3000;
static const uint64_t DEBUG_INTERVAL_MS = 2000;

static inline uint64_t NowMs() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void MaybeAdjustLimits() {
  size_t cnt = ++s_adjustCounter;
  uint64_t now = NowMs();
  uint64_t last = s_lastAdjustMs.load(std::memory_order_relaxed);
  if ((cnt % ADJUST_EVERY_N) == 0 ||
      (now > last && now - last > ADJUST_INTERVAL_MS)) {
    LogoCache::AdjustLimitsForMemory();
    s_lastAdjustMs.store(now, std::memory_order_relaxed);
  }
}

static bool ShouldDebugLogSizes() {
  uint64_t now = NowMs();
  uint64_t last = s_lastDebugMs.load(std::memory_order_relaxed);
  if (now > last && now - last > DEBUG_INTERVAL_MS) {
    s_lastDebugMs.store(now, std::memory_order_relaxed);
    return true;
  }
  return false;
}

// --- Public API ---
bool LogoCache::HasMaster(const std::string &p, const std::string &c) {
  std::lock_guard<std::mutex> lock(s_mutex);
  auto mk = MakeMasterKey(p, c);
  auto it = s_cache.find(mk);
  return it != s_cache.end() && it->second.master && it->second.master->IsOk();
}

void LogoCache::PutMaster(const std::string &p, const std::string &c,
                          const LogoBitmapPtr &bmpPtr) {
  if (!bmpPtr || !bmpPtr->IsOk() || bmpPtr->GetWidth() <= 0 ||
      bmpPtr->GetHeight() <= 0)
    return;

  {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto mk = MakeMasterKey(p, c);
    auto &e = s_cache[mk];
    e.master = bmpPtr;
    e.lastAccess = std::chrono::steady_clock::now();
    TouchEntry(mk);
  }

  MaybeAdjustLimits();
  EnforceLimits();
}

void LogoCache::EnsureMasterAsync(const std::string &p, const std::string &c,
                                  const std::string &url, LogoCallback cb) {
  IconManager::EnsureIconAsync(p, c, url, [=](wxBitmap master) {
    LogoBitmapPtr masterPtr = nullptr;
    if (master.IsOk() && master.GetWidth() > 0 && master.GetHeight() > 0)
      masterPtr = std::make_shared<wxBitmap>(master);

    if (masterPtr)
      PutMaster(p, c, masterPtr);

    if (cb) {
      auto cb_copy = cb;
      auto master_copy = masterPtr;
      wxTheApp->CallAfter([cb_copy, master_copy]() { cb_copy(master_copy); });
    }
  });
}

void LogoCache::EnsureMaster(const std::string &p, const std::string &c,
                             const std::string &url, LogoCallback cb) {
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto mk = MakeMasterKey(p, c);
    auto it = s_cache.find(mk);
    if (it != s_cache.end() && it->second.master && it->second.master->IsOk()) {
      auto mptr = it->second.master;
      if (cb) {
        auto cb_copy = cb;
        auto mptr_copy = mptr;
        wxTheApp->CallAfter([cb_copy, mptr_copy]() { cb_copy(mptr_copy); });
      }
      return;
    }
  }
  EnsureMasterAsync(p, c, url, cb);
}

// --- Adaptive limits ---
void LogoCache::AdjustLimitsForMemory() {
  size_t availMB = GetAvailableRAM_MB();

  size_t newMasters;
  size_t newScaled;

  if (availMB < 512) {
    newMasters = 50;
    newScaled = 500;
  } else if (availMB < 2048) {
    newMasters = 100;
    newScaled = 1500;
  } else if (availMB < 4096) {
    newMasters = 150;
    newScaled = 2000;
  } else {
    newMasters = 200;
    newScaled = 3000;
  }

  s_maxMasters.store(newMasters);
  s_maxScaledTotal.store(newScaled);
}

void LogoCache::EnforceLimits() {
  PROFILE_SCOPE("LogoCache::EnforceLimits");
  std::lock_guard<std::mutex> lock(s_mutex);

  if (ShouldDebugLogSizes()) {
    DebugLogSizes();
  }

#ifdef __WXMSW__
  LOG_DEBUG("GDI Objects: %u",
            GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
#endif

  size_t maxMasters = s_maxMasters.load(std::memory_order_relaxed);
  size_t maxScaledTotal = s_maxScaledTotal.load(std::memory_order_relaxed);

  while (s_lru.size() > maxMasters) {
    auto old = s_lru.back();
    s_lru.pop_back();
    auto it = s_cache.find(old);
    if (it != s_cache.end()) {
      for (auto &kv : it->second.scaled) {
        s_scaledIndex.erase(kv.first);
      }
      s_cache.erase(it);
    }
  }

  size_t totalScaled = s_scaledIndex.size();
  if (totalScaled <= maxScaledTotal)
    return;

  for (auto rit = s_lru.rbegin();
       rit != s_lru.rend() && totalScaled > maxScaledTotal; ++rit) {

    const std::string &mk = *rit;
    auto cacheIt = s_cache.find(mk);
    if (cacheIt == s_cache.end())
      continue;

    auto &entry = cacheIt->second;

    for (auto sit = entry.scaled.begin();
         sit != entry.scaled.end() && totalScaled > maxScaledTotal;) {

      s_scaledIndex.erase(sit->first);
      entry.scaledLastAccess.erase(sit->first);
      sit = entry.scaled.erase(sit);
      --totalScaled;
      
      if (totalScaled <= maxScaledTotal)
        break;
    }
  }
}

void LogoCache::DebugLogSizes() {
  size_t masters = s_cache.size();
  size_t totalScaled = s_scaledIndex.size();
  LOG_DEBUG("LogoCache::Sizes masters=%zu scaledIndex=%zu s_lru=%zu", masters,
            totalScaled, s_lru.size());
}

// --- Control ---
void LogoCache::PauseLoading() {
  bool expected = false;
  if (s_paused.compare_exchange_strong(expected, true)) {
    LOG_DEBUG("LogoCache::PauseLoading - paused");
  }
}

void LogoCache::ResumeLoading() {
  bool expected = true;
  if (s_paused.compare_exchange_strong(expected, false)) {
    LOG_DEBUG("LogoCache::ResumeLoading - resumed");
  }
}

bool LogoCache::IsPausedLoaded() {
  return s_paused.load(std::memory_order_relaxed);
}

void LogoCache::SetCacheLimits(size_t maxMasters, size_t maxScaled) {
  s_maxMasters.store(maxMasters, std::memory_order_relaxed);
  s_maxScaledTotal.store(maxScaled, std::memory_order_relaxed);
}

void LogoCache::RegisterScaledReadyCallback(
    std::function<void(const std::string &)> cb) {
  std::lock_guard<std::mutex> lk(s_mutex);
  LogoCache::s_onScaledReady = std::move(cb);
}

// --- ClearMemory ---
static std::chrono::steady_clock::time_point s_last_clear_time =
    std::chrono::steady_clock::time_point::min();
static std::mutex s_clear_mutex;

void LogoCache::ClearMemory(bool force) {
  PROFILE_SCOPE("LogoCache::ClearMemory");

  std::lock_guard<std::mutex> guard(s_clear_mutex);
  using namespace std::chrono;
  auto now = steady_clock::now();

  long long ageMs = 0;
  if (s_last_clear_time == steady_clock::time_point::min()) {
    ageMs = LLONG_MAX;
  } else {
    ageMs = duration_cast<milliseconds>(now - s_last_clear_time).count();
  }

  //LOG_DEBUG("LogoCache::ClearMemory ENTER force=%d last_clear_age_ms=%lld "
    //        "scaledIndex=%zu masters=%zu",
      //      force ? 1 : 0, ageMs, s_scaledIndex.size(), s_cache.size());

  constexpr auto kClearDebounceMs = 500LL;

  if (!force && ageMs >= 0 &&
      ageMs < static_cast<long long>(kClearDebounceMs)) {
    //LOG_DEBUG(
      //  "LogoCache::ClearMemory skipped (debounced) last_clear_age_ms=%lld",
        //ageMs);
    return;
  }

  s_last_clear_time = now;

  //LOG_DEBUG("LogoCache::ClearMemory performing clear now");

  {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_scaledIndex.clear();
    for (auto &p : s_cache) {
      p.second.scaled.clear();
    }
    s_lru.clear();
  }

  //LOG_DEBUG("LogoCache::ClearMemory cleared scaled in-memory caches force=%d",
    //        force ? 1 : 0);
}

void LogoCache::DumpUniqueKeys() {
  std::lock_guard<std::mutex> lk(s_mutex);
  std::unordered_set<std::string> masters;
  std::unordered_set<std::string> scaled;
  for (auto &kv : s_cache) {
    masters.insert(kv.first);
    for (auto &sk : kv.second.scaled)
      scaled.insert(sk.first);
  }
  fprintf(stderr, "ICON_DBG: unique masters=%zu unique scaled=%zu\n",
          masters.size(), scaled.size());
  fflush(stderr);
}