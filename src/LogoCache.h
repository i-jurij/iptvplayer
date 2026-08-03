#ifndef LOGO_CACHE_H
#define LOGO_CACHE_H

#include <atomic>
#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <wx/bitmap.h>
#include <wx/image.h>

class LogoCache {
public:
  static wxColour GetDefaultCardBgColor();

  //for debug only -------------
  static void DebugMemoryUsage();
  //------------------------------

  static void DropMaster(const std::string &playlist,
                         const std::string &channel);

  static void SetCacheLimits(size_t maxMasters,
                                                           size_t maxScaled);

  using LogoBitmapPtr = std::shared_ptr<wxBitmap>;
  using LogoCallback = std::function<void(const LogoBitmapPtr &)>;

  static void PauseLoading();
  static void ResumeLoading();
  static bool IsPausedLoaded();

  static void ClearMemory(bool force = false);

  static void GetLogoAsync(const std::string &playlist,
                           const std::string &channel, const std::string &url,
                           int targetW, int targetH, int dpiY, LogoCallback cb);

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
  static void ClearScaledExceptSize(
      const std::vector<std::tuple<int, int, int>> &keepSizes);
  // Удалить из scaled-кэша только записи, соответствующие размерам в
  // removeSizes.
  // removeSizes — вектор кортежей (w, h, dpi). Если removeSizes.empty() —
  // ничего не делает.
  static void ClearScaledRemoveSizes(
      const std::vector<std::tuple<int, int, int>> &removeSizes);

  static void OnDPIChanged(int newDpiY);

  // Возвращает shared_ptr к cached scaled bitmap или nullptr
  static LogoBitmapPtr GetCachedBitmapPtr(const std::string &key);

  // backward-compatible wrapper (возвращает копию, если нужен старый API)
  static wxBitmap GetCachedBitmap(const std::string &key);

  // Адаптивная подстройка лимитов по памяти
  static void AdjustLimitsForMemory();
  // Отладочная утилита
  static void DebugLogSizes();
  static std::mutex s_mutex;

  // Debug helpers (only for development)
  static void DumpStats();      // prints masters/scaled/lru/max limits
  static void DumpUniqueKeys(); // prints unique masters/scaled counts

  static void
  RegisterScaledReadyCallback(std::function<void(const std::string &)> cb);

private:
  LogoCache() = delete;
  // pause control for rescale/serve operations
  static std::atomic<bool> s_paused;
  // --- Внутренние ключи ---
  static std::string MakeMasterKey(const std::string &playlist,
                                   const std::string &channel);

  static std::string MakeScaledKey(const std::string &playlist,
                                   const std::string &channel, int w, int h,
                                   int dpiBucket);

  // --- Worker‑функции ---
  static void RescaleAsync(const LogoBitmapPtr &master,
                           const std::string &playlist,
                           const std::string &channel, int w, int h,
                           int dpiBucket, LogoCallback cb);

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
  static std::unordered_map<std::string, LogoEntry> s_cache;
  static std::list<std::string> s_lru;

  // Лимиты (атомарные для безопасности при изменениях из разных потоков)
  static std::atomic<size_t> s_maxMasters;
  static std::atomic<size_t> s_maxScaledTotal;

  static void TouchEntry(const std::string &mk);
  static void EnforceLimits();

  // Быстрый индекс scaledKey -> shared_ptr<wxBitmap>
  static std::unordered_map<std::string, std::weak_ptr<wxBitmap>> s_scaledIndex;

  static std::function<void(const std::string &)> s_onScaledReady;

  static void CleanupOldEntries();

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