#ifndef LOGO_CACHE_H
#define LOGO_CACHE_H

#include "Utils_Cache.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <wx/bitmap.h>
#include <wx/image.h>

class LogoCache {
public:
  static wxColour GetDefaultCardBgColor();

  static void DropMaster(const std::string &playlist,
                         const std::string &channel);

  static void SetCacheLimits(size_t maxMasters, size_t maxScaled);

  using LogoBitmapPtr = std::shared_ptr<wxBitmap>;
  using LogoCallback = std::function<void(const LogoBitmapPtr &)>;

  static void PauseLoading();
  static void ResumeLoading();
  static bool IsPausedLoaded();

  static void ClearMemory(bool force = false);

  static void GetLogoAsync(const std::string &playlist,
                           const std::string &channel, const std::string &url,
                           int targetW, int targetH, int dpiY, LogoCallback cb,
                           double scaleFactor = 1.0);

  static bool HasMaster(const std::string &playlist,
                        const std::string &channel);

  static void PutMaster(const std::string &playlist, const std::string &channel,
                        const LogoBitmapPtr &masterBmp);

  static void EnsureMaster(const std::string &playlist,
                           const std::string &channel, const std::string &url,
                           LogoCallback cb = nullptr);

  static void ClearScaled();
  static void ClearAll();
  static void ClearPlaylist(const std::string &playlist);

  // ---- Posters ----
  // Отдельный кэш постеров. Ключ: playlist + mediaKey + hash(posterUrl)
  // + w + h + scale100. Хэш URL гарантирует инвалидацию при смене постера
  // сервером для того же mediaKey (RefreshVodCategory / FetchVodInfo).
  // mediaKey = "vod:<id>" / "series:<id>" — не имя канала.
  // Загрузка через IconManager::EnsureIconAsync (тот же мастер-кэш на
  // диске, псевдо-канал = mediaKey).
  static void GetPosterAsync(const std::string &playlist,
                             const std::string &mediaKey,
                             const std::string &url, int targetW, int targetH,
                             const wxColour &bgColor, LogoCallback cb,
                             double scaleFactor = 1.0);

  static LogoBitmapPtr GetCachedPosterPtr(const std::string &key);
  static std::string MakePosterKey(const std::string &playlist,
                                   const std::string &mediaKey,
                                   const std::string &posterUrl, int w, int h,
                                   int scale100);
  static void SetPosterCacheLimits(size_t maxPosters);
  static void ClearPosterMemory();

  // Удалить из scaled-кэша только записи, соответствующие размерам в
  // removeSizes.
  // Каждый кортеж — (physW, physH, scale100), где scale100 — целое
  // (100 = cs 1.0, 200 = cs 2.0). Если removeSizes.empty() — ничего не делает.
  // Мастера и их LRU-позиции НЕ затрагиваются.
  static void ClearScaledRemoveSizes(
      const std::vector<std::tuple<int, int, int>> &removeSizes);

  // Возвращает shared_ptr к cached scaled bitmap или nullptr
  static LogoBitmapPtr GetCachedBitmapPtr(const std::string &key);

  // backward-compatible wrapper (возвращает копию, если нужен старый API)
  static wxBitmap GetCachedBitmap(const std::string &key);

  // Адаптивная подстройка лимитов по памяти
  static void AdjustLimitsForMemory();

  // Time-based hygiene: чистит просроченные scaled и masters-сироты.
  // Вызывается из MaybeAdjustLimits раз в 30с (rate-limited).
  static void CleanupOldEntries();

  // Отладочная утилита
  static void DebugLogSizes();
  static std::mutex s_mutex;

  // Debug helpers (only for development)
  static void DumpStats();      // prints masters/scaled/lru/max limits
  static void DumpUniqueKeys(); // prints unique masters/scaled counts

  static void
  RegisterScaledReadyCallback(std::function<void(const std::string &)> cb);

  static std::string MakeScaledKey(const std::string &playlist,
                                   const std::string &channel, int w, int h,
                                   int scale100);

private:
  LogoCache() = delete;
  // pause control for rescale/serve operations
  static std::atomic<bool> s_paused;
  // --- Внутренние ключи ---
  static std::string MakeMasterKey(const std::string &playlist,
                                   const std::string &channel);

  // --- Worker‑функции ---
  static void RescaleAsync(const LogoBitmapPtr &master,
                           const std::string &playlist,
                           const std::string &channel, int w, int h,
                           int scale100, double scaleFactor, LogoCallback cb);

  static void EnsureMasterAsync(const std::string &playlist,
                                const std::string &channel,
                                const std::string &url, LogoCallback cb);

  // --- Структуры ---
  struct LogoEntry {
    LogoBitmapPtr master;
    std::unordered_map<std::string, LogoBitmapPtr> scaled;
    //  время последнего использования master
    std::chrono::steady_clock::time_point lastAccess;
    //  время последнего использования каждого scaled
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        scaledLastAccess;
  };

  // --- Хранилище ---
  // Master-LRU с onEvict, чистящим scaled-индексы.
  // Не thread-safe: caller держит s_mutex.
  static LruCache<std::string, LogoEntry> &Cache();

  // Быстрый индекс scaledKey -> shared_ptr<wxBitmap>
  static std::unordered_map<std::string, std::weak_ptr<wxBitmap>> s_scaledIndex;
  // Обратный индекс scaledKey -> masterKey (O(1) для GetCachedBitmapPtr)
  static std::unordered_map<std::string, std::string> s_scaledToMaster;

  // Лимиты (атомарные для безопасности при изменениях из разных потоков)
  static std::atomic<size_t> s_maxMasters;
  static std::atomic<size_t> s_maxScaledTotal;

  static void EnforceLimits();

  static std::function<void(const std::string &)> s_onScaledReady;

  struct PendingOps {
    std::vector<LogoCallback> callbacks;
    bool isLoading = false;
  };

  static std::unordered_map<std::string, PendingOps> s_masterPending;
  static std::unordered_map<std::string, PendingOps> s_scaledPending;
  static const size_t MAX_PENDING_PER_KEY = 32;
};

#endif

#ifdef __LOGOCACHE_CPP__
void MaybeAdjustLimits();
#endif
