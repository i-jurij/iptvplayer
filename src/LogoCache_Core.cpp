// LogoCache_Core.cpp
// Ядро: хранение, ключи, LRU, очистка

#include "LogControl.h"
#define __LOGOCACHE_CPP__
#include "LogoCache.h"
#include "Profiler.h"
#include "VP_SvgIcon.h"

#include <wx/log.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

std::atomic<size_t> LogoCache::s_maxMasters(300);
std::atomic<size_t> LogoCache::s_maxScaledTotal(2000);
std::atomic<bool> LogoCache::s_paused(false);

// --- STATIC FIELDS (определяем здесь, чтобы были доступны во всех .cpp) ---
std::unordered_map<std::string, std::weak_ptr<wxBitmap>>
    LogoCache::s_scaledIndex;
std::unordered_map<std::string, std::string> LogoCache::s_scaledToMaster;
std::mutex LogoCache::s_mutex;

std::function<void(const std::string &)> LogoCache::s_onScaledReady;

std::unordered_map<std::string, LogoCache::PendingOps>
    LogoCache::s_masterPending;
std::unordered_map<std::string, LogoCache::PendingOps>
    LogoCache::s_scaledPending;

// --- Master-LRU accessor (Meyers singleton + call_once для onEvict) ---
LruCache<std::string, LogoCache::LogoEntry> &LogoCache::Cache() {
  static LruCache<std::string, LogoEntry> cache(300);
  static std::once_flag initFlag;
  std::call_once(initFlag, []() {
    cache.onEvict = [](const std::string & /*mk*/, const LogoEntry &e) {
      for (const auto &sk : e.scaled) {
        s_scaledIndex.erase(sk.first);
        s_scaledToMaster.erase(sk.first);
      }
    };
  });
  return cache;
}

wxColour LogoCache::GetDefaultCardBgColor() {
  if (ShouldUseDarkColors()) {
    return wxColour(60, 63, 65); // #3C3F41 — мягкий графит
  }
  return wxColour(200, 200, 200); // #E6E6E6 — мягкий светло‑серый
}

// --- Keys ---
std::string LogoCache::MakeMasterKey(const std::string &p,
                                     const std::string &c) {
  return p + "|" + c;
}

std::string LogoCache::MakeScaledKey(const std::string &playlist,
                                     const std::string &channelOrUrl, int w,
                                     int h, int scale100) {
  std::string id =
      playlist.empty() ? channelOrUrl : playlist + "|" + channelOrUrl;
  return id + "|" + std::to_string(w) + "x" + std::to_string(h) + "|" +
         std::to_string(scale100);
}

// Парсит суффикс "...|{w}x{h}|{scale100}" из scaled-ключа.
// scale100 — целое: 100 (cs=1.0), 125 (cs=1.25), 200 (cs=2.0) и т.д.
static bool ParseScaledKeySize(const std::string &sk, int &outW, int &outH,
                               int &outScale100) {
  size_t lastPipe = sk.rfind('|');
  if (lastPipe == std::string::npos)
    return false;
  size_t prevPipe = sk.rfind('|', lastPipe - 1);
  if (prevPipe == std::string::npos)
    return false;
  std::string wh = sk.substr(prevPipe + 1, lastPipe - prevPipe - 1);
  std::string scaleStr = sk.substr(lastPipe + 1);
  int w = 0, h = 0, scale100 = 0;
  if (sscanf(wh.c_str(), "%dx%d", &w, &h) != 2)
    return false;
  scale100 = atoi(scaleStr.c_str());
  if (w <= 0 || h <= 0 || scale100 <= 0)
    return false;
  outW = w;
  outH = h;
  outScale100 = scale100;
  return true;
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
  // Очищаем кэш (onEvict почистит scaled-индексы)
  Cache().Clear();
  s_scaledIndex.clear();
  s_scaledToMaster.clear();

  ClearPosterMemory();
}

void LogoCache::ClearPlaylist(const std::string &p) {
  std::lock_guard<std::mutex> lock(s_mutex);

  std::vector<std::string> toErase;
  Cache().ForEach([&](const std::string &mk, const LogoEntry &) {
    if (mk.rfind(p + "|", 0) == 0)
      toErase.push_back(mk);
  });
  for (const auto &mk : toErase)
    Cache().Erase(mk); // onEvict чистит s_scaledIndex/s_scaledToMaster
}

void LogoCache::ClearScaled() {
  std::lock_guard<std::mutex> lock(s_mutex);

  LOG_DEBUG("ClearScaled: scaled=%zu masters=%zu", s_scaledIndex.size(),
            Cache().Size());

  // Вызываем все ожидающие scaled-колбэки с nullptr
  for (auto &kv : s_scaledPending) {
    for (auto &cb : kv.second.callbacks) {
      if (cb)
        cb(nullptr);
    }
  }
  s_scaledPending.clear();

  // Очищаем scaled-кэш у всех мастеров
  Cache().ForEach([](const std::string &, LogoEntry &e) {
    e.scaled.clear();
    e.scaledLastAccess.clear();
  });
  s_scaledIndex.clear();
  s_scaledToMaster.clear();
}

void LogoCache::ClearScaledRemoveSizes(
    const std::vector<std::tuple<int, int, int>> &removeSizes) {
  PROFILE_SCOPE("LogoCache::ClearScaledRemoveSizes");
  if (removeSizes.empty()) {
    return;
  }

  // Каждый кортеж — (physW, physH, scale100).
  std::set<std::tuple<int, int, int>> removeSet;
  for (const auto &t : removeSizes) {
    int w, h, s;
    std::tie(w, h, s) = t;
    if (w > 0 && h > 0 && s > 0)
      removeSet.insert(std::make_tuple(w, h, s));
  }
  if (removeSet.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(s_mutex);

  std::vector<std::string> toClearPending;
  Cache().ForEach([&](const std::string &, LogoEntry &e) {
    for (auto it = e.scaled.begin(); it != e.scaled.end();) {
      int w = 0, h = 0, scale100 = 0;
      bool parsed = ParseScaledKeySize(it->first, w, h, scale100);
      bool toRemove =
          parsed &&
          (removeSet.find(std::make_tuple(w, h, scale100)) != removeSet.end());
      if (toRemove) {
        const std::string &sk = it->first;
        toClearPending.push_back(sk);
        s_scaledIndex.erase(sk);
        s_scaledToMaster.erase(sk);
        e.scaledLastAccess.erase(sk);
        it = e.scaled.erase(it);
      } else {
        ++it;
      }
    }
  });

  // Очищаем ожидающие колбэки для удалённых scaled-ключей
  for (const auto &sk : toClearPending) {
    auto pendingIt = s_scaledPending.find(sk);
    if (pendingIt != s_scaledPending.end()) {
      for (auto &pcb : pendingIt->second.callbacks) {
        if (pcb)
          pcb(nullptr);
      }
      s_scaledPending.erase(pendingIt);
    }
  }
  // NOTE: мастера и их LRU-позиции НЕ затрагиваются (fix by I11 11.1).
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
  // Удаляем мастер и все scaled-записи (onEvict почистит индексы)
  Cache().Erase(mk);
}

LogoCache::LogoBitmapPtr
LogoCache::GetCachedBitmapPtr(const std::string &key) {
  PROFILE_SCOPE("LogoCache::GetCachedBitmapPtr");

  std::lock_guard<std::mutex> lock(s_mutex);

  auto it = s_scaledIndex.find(key);
  if (it != s_scaledIndex.end()) {
    auto sp = it->second.lock();
    if (sp && sp->IsOk()) {
      // O(1) обновление timestamp через обратный индекс (без touch LRU).
      auto rit = s_scaledToMaster.find(key);
      if (rit != s_scaledToMaster.end()) {
        LogoEntry *e = Cache().PeekMut(rit->second);
        if (e) {
          auto now = std::chrono::steady_clock::now();
          e->scaledLastAccess[key] = now;
          e->lastAccess = now;
        }
      }
      return sp;
    }
    // weak_ptr истёк → удаляем запись
    s_scaledIndex.erase(it);
    s_scaledToMaster.erase(key);
  }

  return nullptr;
}

// backward-compatible wrapper (возвращает копию)
wxBitmap LogoCache::GetCachedBitmap(const std::string &key) {
  auto p = GetCachedBitmapPtr(key);
  return p ? *p : wxNullBitmap;
}

// NOTE: активируется в 11.2 (см. MaybeAdjustLimits).
void LogoCache::CleanupOldEntries() {
  using namespace std::chrono;
  auto now = steady_clock::now();

  const auto MASTER_TIMEOUT = seconds(120);
  const auto SCALED_TIMEOUT = seconds(60);

  std::lock_guard<std::mutex> lock(s_mutex);

  std::vector<std::string> toErase;
  Cache().ForEach([&](const std::string &mk, LogoEntry &e) {
    // --- 1. Чистим scaled по времени ---
    for (auto sit = e.scaled.begin(); sit != e.scaled.end();) {
      const std::string &sk = sit->first;
      auto tsIt = e.scaledLastAccess.find(sk);
      bool expired =
          (tsIt != e.scaledLastAccess.end()) &&
          (now - tsIt->second > SCALED_TIMEOUT);

      if (expired) {
        s_scaledIndex.erase(sk);
        s_scaledToMaster.erase(sk);
        e.scaledLastAccess.erase(sk);
        sit = e.scaled.erase(sit);
      } else {
        ++sit;
      }
    }

    // --- 2. Если master давно не использовался и scaled пуст —
    // удаляем master ---
    bool masterExpired = (now - e.lastAccess > MASTER_TIMEOUT);
    bool noScaled = e.scaled.empty();
    if (masterExpired && noScaled)
      toErase.push_back(mk);
  });
  for (const auto &mk : toErase)
    Cache().Erase(mk);
}

// Debug helpers implementation, only for development
void LogoCache::DumpStats() {
  std::lock_guard<std::mutex> lk(s_mutex);
  fprintf(stderr,
          "ICON_DBG: cache masters=%zu scaledIndex=%zu "
          "maxMasters=%zu maxScaled=%zu\n",
          Cache().Size(), s_scaledIndex.size(),
          s_maxMasters.load(std::memory_order_relaxed),
          s_maxScaledTotal.load(std::memory_order_relaxed));
  fflush(stderr);
}
