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
static std::atomic<uint64_t> s_lastCleanupMs(0);
static std::atomic<size_t> s_adjustCounter(0);

static const size_t ADJUST_EVERY_N = 64;
static const uint64_t ADJUST_INTERVAL_MS = 3000;
static const uint64_t DEBUG_INTERVAL_MS = 2000;
static const uint64_t CLEANUP_INTERVAL_MS = 30000;

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

  uint64_t lastCleanup = s_lastCleanupMs.load(std::memory_order_relaxed);
  if (now > lastCleanup && now - lastCleanup > CLEANUP_INTERVAL_MS) {
    LogoCache::CleanupOldEntries();
    s_lastCleanupMs.store(now, std::memory_order_relaxed);
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
  const LogoEntry *e = Cache().Peek(mk);
  return e && e->master && e->master->IsOk();
}

void LogoCache::PutMaster(const std::string &p, const std::string &c,
                          const LogoBitmapPtr &bmpPtr) {
  if (!bmpPtr || !bmpPtr->IsOk() || bmpPtr->GetWidth() <= 0 ||
      bmpPtr->GetHeight() <= 0)
    return;

  {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto mk = MakeMasterKey(p, c);

    LogoEntry *e = Cache().Get(mk); // touch (MRU)
    if (!e) {
      Cache().Put(mk, LogoEntry{});     // Put уже ставит в MRU
      e = Cache().PeekMut(mk);          // без повторного touch
    }
    if (e) {
      e->master = bmpPtr;
      e->lastAccess = std::chrono::steady_clock::now();
    }
  }

  MaybeAdjustLimits();
  EnforceLimits();
}

void LogoCache::EnsureMasterAsync(const std::string &p, const std::string &c,
                                  const std::string &url, LogoCallback cb) {
  auto mk = MakeMasterKey(p, c);
  bool needLoad = false;

  {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_masterPending.find(mk);
    if (it == s_masterPending.end() || !it->second.isLoading) {
      s_masterPending[mk].isLoading = true;
      needLoad = true;
    }
    if (cb) {
      auto &ops = s_masterPending[mk];
      if (ops.callbacks.size() >= MAX_PENDING_PER_KEY) {
        auto oldCb = ops.callbacks.front();
        ops.callbacks.erase(ops.callbacks.begin());
        if (oldCb && wxTheApp) {
          auto cb_copy = oldCb;
          wxTheApp->CallAfter([cb_copy]() { cb_copy(nullptr); });
        }
      }
      ops.callbacks.push_back(cb);
    }
  }

  if (!needLoad) {
    return;
  }

  IconManager::EnsureIconAsync(p, c, url, [=](wxBitmap master) {
    LogoBitmapPtr masterPtr = nullptr;
    if (master.IsOk() && master.GetWidth() > 0 && master.GetHeight() > 0)
      masterPtr = std::make_shared<wxBitmap>(master);

    if (masterPtr)
      PutMaster(p, c, masterPtr);

    std::vector<LogoCallback> pendingCallbacks;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      auto it = s_masterPending.find(mk);
      if (it != s_masterPending.end()) {
        pendingCallbacks.swap(it->second.callbacks);
        s_masterPending.erase(it);
      }
    }

    if (!wxTheApp) {
      return;
    }
    for (auto &pcb : pendingCallbacks) {
      if (pcb) {
        auto cb_copy = pcb;
        auto master_copy = masterPtr;
        wxTheApp->CallAfter([cb_copy, master_copy]() { cb_copy(master_copy); });
      }
    }
  });
}

void LogoCache::EnsureMaster(const std::string &p, const std::string &c,
                             const std::string &url, LogoCallback cb) {
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto mk = MakeMasterKey(p, c);
    const LogoEntry *e = Cache().Peek(mk);
    if (e && e->master && e->master->IsOk()) {
      auto mptr = e->master;
      if (cb) {
        if (wxTheApp) {
          auto cb_copy = cb;
          auto mptr_copy = mptr;
          wxTheApp->CallAfter([cb_copy, mptr_copy]() { cb_copy(mptr_copy); });
        } else {
          cb(mptr);
        }
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

  // Применяем master-limit немедленно (evict через onEvict).
  std::lock_guard<std::mutex> lock(s_mutex);
  Cache().SetMaxSize(newMasters);
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

  auto &cache = Cache();

  // Master-eviction: применяем актуальный лимит (safety net для
  // внешнего SetCacheLimits и AdjustLimitsForMemory).
  if (cache.MaxSize() != maxMasters)
    cache.SetMaxSize(maxMasters);

  // Scaled-eviction: обход LRU→MRU, режем scaled пока totalScaled > limit.
  size_t totalScaled = s_scaledIndex.size();
  if (totalScaled <= maxScaledTotal)
    return;

  cache.ForEach([&](const std::string &, LogoEntry &e) {
    if (totalScaled <= maxScaledTotal)
      return;
    for (auto sit = e.scaled.begin();
         sit != e.scaled.end() && totalScaled > maxScaledTotal;) {
      s_scaledIndex.erase(sit->first);
      s_scaledToMaster.erase(sit->first);
      e.scaledLastAccess.erase(sit->first);
      sit = e.scaled.erase(sit);
      --totalScaled;
    }
  });
}

void LogoCache::DebugLogSizes() {
  LOG_DEBUG("LogoCache::Sizes masters=%zu scaledIndex=%zu", Cache().Size(),
            s_scaledIndex.size());
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
  // Применение лимита — на следующем EnforceLimits (safety net).
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

  constexpr auto kClearDebounceMs = 500LL;

  if (!force && ageMs >= 0 &&
      ageMs < static_cast<long long>(kClearDebounceMs)) {
    return;
  }

  s_last_clear_time = now;

  {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_scaledIndex.clear();
    s_scaledToMaster.clear();
    // Чистим только scaled; master-LRU и мастера сохраняем (D6 вариант A).
    Cache().ForEach([](const std::string &, LogoEntry &e) {
      e.scaled.clear();
      e.scaledLastAccess.clear();
    });
  }
}

void LogoCache::DumpUniqueKeys() {
  std::lock_guard<std::mutex> lk(s_mutex);
  std::unordered_set<std::string> masters;
  std::unordered_set<std::string> scaled;
  Cache().ForEach([&](const std::string &mk, const LogoEntry &e) {
    masters.insert(mk);
    for (const auto &sk : e.scaled)
      scaled.insert(sk.first);
  });
  fprintf(stderr, "ICON_DBG: unique masters=%zu unique scaled=%zu\n",
          masters.size(), scaled.size());
  fflush(stderr);
}
