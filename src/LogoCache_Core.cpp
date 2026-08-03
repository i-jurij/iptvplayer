// LogoCache_Core.cpp
// Ядро: хранение, ключи, LRU, очистка

#include "LogControl.h"
#define __LOGOCACHE_CPP__
#include "LogoCache.h"
#include "Profiler.h"
#include "VP_SvgIcon.h"

#include <wx/log.h>

#include <algorithm>
#include <set>


std::atomic<size_t> LogoCache::s_maxMasters(300);
std::atomic<size_t> LogoCache::s_maxScaledTotal(2000);
std::atomic<bool> LogoCache::s_paused(false);

// --- STATIC FIELDS (определяем здесь, чтобы были доступны во всех .cpp) ---
std::unordered_map<std::string, std::weak_ptr<wxBitmap>>
    LogoCache::s_scaledIndex;

std::unordered_map<std::string, LogoCache::LogoEntry> LogoCache::s_cache;
std::list<std::string> LogoCache::s_lru;
std::mutex LogoCache::s_mutex;

std::function<void(const std::string &)> LogoCache::s_onScaledReady;

std::unordered_map<std::string, LogoCache::PendingOps>
    LogoCache::s_masterPending;
std::unordered_map<std::string, LogoCache::PendingOps>
    LogoCache::s_scaledPending;

wxColour LogoCache::GetDefaultCardBgColor() {
  if (wxSystemSettings::GetAppearance().IsDark()) {
    return wxColour(60, 63, 65); // #3C3F41 — мягкий графит
  } else {
    return wxColour(200, 200, 200); // #E6E6E6 — мягкий светло‑серый
  }
}

// --- Keys ---
std::string LogoCache::MakeMasterKey(const std::string &p,
                                     const std::string &c) {
  return p + "|" + c;
}

std::string LogoCache::MakeScaledKey(const std::string &playlist,
                                     const std::string &channelOrUrl, int w,
                                     int h, int dpi) {
  std::string id =
      playlist.empty() ? channelOrUrl : playlist + "|" + channelOrUrl;
  return id + "|" + std::to_string(w) + "x" + std::to_string(h) + "|" +
         std::to_string(dpi);
}

// helper: parse suffix "...|{w}x{h}|{dpi}" from scaled key
static bool ParseScaledKeySize(const std::string &sk, int &outW, int &outH,
                               int &outDpi) {
  size_t lastPipe = sk.rfind('|');
  if (lastPipe == std::string::npos)
    return false;
  size_t prevPipe = sk.rfind('|', lastPipe - 1);
  if (prevPipe == std::string::npos)
    return false;
  std::string wh = sk.substr(prevPipe + 1, lastPipe - prevPipe - 1);
  std::string dpiStr = sk.substr(lastPipe + 1);
  int w = 0, h = 0, dpi = 0;
  if (sscanf(wh.c_str(), "%dx%d", &w, &h) != 2)
    return false;
  dpi = atoi(dpiStr.c_str());
  if (w <= 0 || h <= 0 || dpi <= 0)
    return false;
  outW = w;
  outH = h;
  outDpi = dpi;
  return true;
}

// --- LRU ---
void LogoCache::TouchEntry(const std::string &mk) {
  auto it = std::find(s_lru.begin(), s_lru.end(), mk);
  if (it != s_lru.end())
    s_lru.erase(it);
  s_lru.push_front(mk);
}

// --- Cache Ops ---
void LogoCache::ClearAll() {
  std::lock_guard<std::mutex> lock(s_mutex);
  // Очищаем master-pending
  for (auto &kv : s_masterPending) {
    for (auto &cb : kv.second.callbacks) {
      if (cb)
        cb(nullptr);
    }
  }
  s_masterPending.clear();
  // Очищаем scaled-pending
  for (auto &kv : s_scaledPending) {
    for (auto &cb : kv.second.callbacks) {
      if (cb)
        cb(nullptr);
    }
  }
  s_scaledPending.clear();
  // Очищаем кэш
  s_scaledIndex.clear();
  s_cache.clear();
  s_lru.clear();
}

void LogoCache::ClearPlaylist(const std::string &p) {
  std::lock_guard<std::mutex> lock(s_mutex);
  for (auto it = s_cache.begin(); it != s_cache.end();) {
    if (it->first.rfind(p + "|", 0) == 0) {
      for (auto &sk : it->second.scaled) {
        s_scaledIndex.erase(sk.first);
      }
      it = s_cache.erase(it);
    } else {
      ++it;
    }
  }
  s_lru.remove_if(
      [&](const std::string &mk) { return mk.rfind(p + "|", 0) == 0; });
}

void LogoCache::ClearScaled() {
  std::lock_guard<std::mutex> lock(s_mutex);
  // Вызываем все ожидающие scaled-колбэки с nullptr
  for (auto &kv : s_scaledPending) {
    for (auto &cb : kv.second.callbacks) {
      if (cb)
        cb(nullptr);
    }
  }
  s_scaledPending.clear();
  // Очищаем scaled-кэш
  for (auto &kv : s_cache) {
    kv.second.scaled.clear();
  }
  s_scaledIndex.clear();
}

void LogoCache::ClearScaledRemoveSizes(
    const std::vector<std::tuple<int, int, int>> &removeSizes) {
  PROFILE_SCOPE("LogoCache::ClearScaledRemoveSizes");
  if (removeSizes.empty()) {
    return;
  }

  std::set<std::tuple<int, int, int>> removeSet;
  for (const auto &t : removeSizes) {
    int w, h, d;
    std::tie(w, h, d) = t;
    if (w > 0 && h > 0 && d > 0)
      removeSet.insert(std::make_tuple(w, h, d));
  }
  if (removeSet.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(s_mutex);
  for (auto &cachePair : s_cache) {
    auto &scaledMap = cachePair.second.scaled;
    for (auto it = scaledMap.begin(); it != scaledMap.end();) {
      int w = 0, h = 0, dpi = 0;
      bool parsed = ParseScaledKeySize(it->first, w, h, dpi);
      bool toRemove = parsed && (removeSet.find(std::make_tuple(w, h, dpi)) !=
                                 removeSet.end());
      if (toRemove) {
        const std::string &sk = it->first;
        // Очищаем ожидающие колбэки для этого scaled-ключа
        auto pendingIt = s_scaledPending.find(sk);
        if (pendingIt != s_scaledPending.end()) {
          for (auto &pcb : pendingIt->second.callbacks) {
            if (pcb)
              pcb(nullptr);
          }
          s_scaledPending.erase(pendingIt);
        }
        s_scaledIndex.erase(sk);
        it = scaledMap.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Очищаем LRU от записей, у которых больше нет scaled
  for (auto it = s_lru.begin(); it != s_lru.end();) {
    auto cacheIt = s_cache.find(*it);
    if (cacheIt == s_cache.end() || cacheIt->second.scaled.empty()) {
      it = s_lru.erase(it);
    } else {
      ++it;
    }
  }
}

void LogoCache::ClearScaledExceptSize(
    const std::vector<std::tuple<int, int, int>> &keepSizes) {
  PROFILE_SCOPE("LogoCache::ClearScaledExceptSize");
  std::lock_guard<std::mutex> lock(s_mutex);

  if (keepSizes.empty()) {
    // Очищаем всё: вызываем все ожидающие scaled-колбэки с nullptr
    for (auto &kv : s_scaledPending) {
      for (auto &cb : kv.second.callbacks) {
        if (cb)
          cb(nullptr);
      }
    }
    s_scaledPending.clear();
    for (auto &kv : s_cache) {
      for (auto &sk : kv.second.scaled) {
        s_scaledIndex.erase(sk.first);
      }
      kv.second.scaled.clear();
    }
    s_lru.clear();
    return;
  }

  std::set<std::tuple<int, int, int>> keepSet;
  for (const auto &t : keepSizes) {
    int w, h, d;
    std::tie(w, h, d) = t;
    if (w > 0 && h > 0 && d > 0)
      keepSet.insert(std::make_tuple(w, h, d));
  }

  for (auto &cachePair : s_cache) {
    auto &scaledMap = cachePair.second.scaled;
    for (auto it = scaledMap.begin(); it != scaledMap.end();) {
      const std::string &sk = it->first;
      int w = 0, h = 0, dpi = 0;
      bool parsed = ParseScaledKeySize(sk, w, h, dpi);
      bool keep =
          parsed && (keepSet.find(std::make_tuple(w, h, dpi)) != keepSet.end());
      if (!keep) {
        // Очищаем ожидающие колбэки для этого scaled-ключа
        auto pendingIt = s_scaledPending.find(sk);
        if (pendingIt != s_scaledPending.end()) {
          for (auto &pcb : pendingIt->second.callbacks) {
            if (pcb)
              pcb(nullptr);
          }
          s_scaledPending.erase(pendingIt);
        }
        s_scaledIndex.erase(sk);
        it = scaledMap.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Очищаем LRU от записей без scaled
  for (auto it = s_lru.begin(); it != s_lru.end();) {
    auto cacheIt = s_cache.find(*it);
    if (cacheIt == s_cache.end() || cacheIt->second.scaled.empty()) {
      it = s_lru.erase(it);
    } else {
      ++it;
    }
  }
}

void LogoCache::DropMaster(const std::string &p, const std::string &c) {
  std::lock_guard<std::mutex> lock(s_mutex);
  auto mk = MakeMasterKey(p, c);
  // Очищаем master-pending для этого ключа
  auto mpIt = s_masterPending.find(mk);
  if (mpIt != s_masterPending.end()) {
    for (auto &cb : mpIt->second.callbacks) {
      if (cb)
        cb(nullptr);
    }
    s_masterPending.erase(mpIt);
  }
  // Удаляем мастер и все scaled-записи (scaled-pending не трогаем)
  auto cacheIt = s_cache.find(mk);
  if (cacheIt != s_cache.end()) {
    for (auto &sk : cacheIt->second.scaled) {
      s_scaledIndex.erase(sk.first);
    }
    s_cache.erase(cacheIt);
    s_lru.remove(mk);
  }
}

void LogoCache::OnDPIChanged(int) { ClearScaled(); }

LogoCache::LogoBitmapPtr LogoCache::GetCachedBitmapPtr(const std::string &key) {
  PROFILE_SCOPE("LogoCache::GetCachedBitmapPtr");

  std::lock_guard<std::mutex> lock(s_mutex);

  auto it = s_scaledIndex.find(key);
  if (it != s_scaledIndex.end()) {
    auto sp = it->second.lock();
    if (sp && sp->IsOk()) {

      // --- NEW: обновляем timestamp scaled ---
      for (auto &kv : s_cache) {
        auto &entry = kv.second;
        auto it2 = entry.scaled.find(key);
        if (it2 != entry.scaled.end()) {
          auto now = std::chrono::steady_clock::now();
          entry.scaledLastAccess[key] = now;
          entry.lastAccess = now;
          break;
        }
      }

      return sp;
    }

    // weak_ptr истёк → удаляем запись
    s_scaledIndex.erase(it);
  }

  return nullptr;
}

// backward-compatible wrapper (возвращает копию)
wxBitmap LogoCache::GetCachedBitmap(const std::string &key) {
  auto p = GetCachedBitmapPtr(key);
  return p ? *p : wxNullBitmap;
}

void LogoCache::CleanupOldEntries() {
  using namespace std::chrono;
  auto now = steady_clock::now();

  const auto MASTER_TIMEOUT = seconds(120);
  const auto SCALED_TIMEOUT = seconds(60);

  for (auto it = s_cache.begin(); it != s_cache.end();) {
    auto &entry = it->second;

    // --- 1. Чистим scaled по времени ---
    for (auto sit = entry.scaled.begin(); sit != entry.scaled.end();) {
      const std::string &sk = sit->first;

      auto tsIt = entry.scaledLastAccess.find(sk);
      bool expired = false;

      if (tsIt != entry.scaledLastAccess.end()) {
        expired = (now - tsIt->second > SCALED_TIMEOUT);
      }

      if (expired) {
        s_scaledIndex.erase(sk);
        entry.scaledLastAccess.erase(sk);
        sit = entry.scaled.erase(sit);
      } else {
        ++sit;
      }
    }

    // --- 2. Если master давно не использовался и scaled пуст — удаляем master
    // ---
    bool masterExpired = (now - entry.lastAccess > MASTER_TIMEOUT);
    bool noScaled = entry.scaled.empty();

    if (masterExpired && noScaled) {
      s_lru.remove(it->first);
      it = s_cache.erase(it);
      continue;
    }

    ++it;
  }
}

// Debug helpers implementation, only for development
void LogoCache::DumpStats() {
  std::lock_guard<std::mutex> lk(s_mutex);
  fprintf(stderr,
          "ICON_DBG: cache masters=%zu scaledIndex=%zu lru=%zu maxMasters=%zu "
          "maxScaled=%zu\n",
          s_cache.size(), s_scaledIndex.size(), s_lru.size(),
          s_maxMasters.load(std::memory_order_relaxed),
          s_maxScaledTotal.load(std::memory_order_relaxed));
  fflush(stderr);
}

static size_t EstimateBitmapBytes(const wxBitmap &bmp) {
  if (!bmp.IsOk())
    return 0;
  return (size_t)bmp.GetWidth() * (size_t)bmp.GetHeight() * 4;
}

void LogoCache::DebugMemoryUsage() {
  std::lock_guard<std::mutex> lock(s_mutex);

  size_t mastersCount = 0;
  size_t mastersBytes = 0;

  size_t scaledCount = 0;
  size_t scaledBytes = 0;

  for (auto &kv : s_cache) {
    const auto &entry = kv.second;

    if (entry.master && entry.master->IsOk()) {
      mastersCount++;
      mastersBytes += EstimateBitmapBytes(*entry.master);
    }

    for (auto &sk : entry.scaled) {
      auto sp = sk.second;
      if (sp && sp->IsOk()) {
        scaledCount++;
        scaledBytes += EstimateBitmapBytes(*sp);
      }
    }
  }
  /*
    LOG_DEBUG("LogoCache Memory: masters=%zu (%zu KB), scaled=%zu (%zu KB), "
              "total=%zu KB",
              mastersCount, mastersBytes / 1024, scaledCount, scaledBytes /
  1024, (mastersBytes + scaledBytes) / 1024);
  */
}
