#pragma once

#include "ConfigManager.h"
#include <mpv/client.h>
#include <wx/app.h>
#include <wx/string.h>
#include <wx/window.h>

#include <cstddef>
#include <ctime>
#include <functional>
#include <string>

wxString FindExecutableInPath(const wxString &name);
bool IsFileExecutable(const wxString &path);
bool IsSafeSubpath(const wxString &base, const wxString &candidate);
bool SafeRemoveDirectory(const wxString &dir, std::error_code &ec);
using RemoveDirCallback = std::function<void(bool, const std::error_code &)>;
void SafeRemoveDirectoryAsync(const wxString &dir, RemoveDirCallback cb);
bool RemoveMarkerFilesRecursive(const wxString &baseDir, size_t &removed,
                                size_t &skippedUnsafe, size_t &failed,
                                bool followSymlinks);

std::string NormalizeFileNameForDisk(const std::string &input,
                                     size_t maxLen = 200);
void showError(wxWindow *parent, const wxString &message,
               const wxString &title = "Error");
std::vector<wxString> extractAllUrls(const wxString &s);
void showInfo(wxWindow *parent, const wxString &message,
              const wxString &caption = wxT("Info"));

// ============================================================================
// Пути к иконкам и DPI-утилиты
// ============================================================================

/// Возвращает путь к иконке, проверяя локальную папку, системный каталог и имя
/// файла
wxString getIconPath(const wxString &iconName);

// DPI и масштабирование
int GetRawDPI(wxWindow *ctx);
int NormalizeDpi(int dpiY);
int GetNormDPI(wxWindow *ctx);
/// Возвращает DPI-адаптивный размер логотипа для списков (24–64 px)
int GetDpiLogoSizeList(wxWindow *ctx);
std::pair<int, int> GetCardSizeForDPI(int dpi);
int GetScaledCardSize(int dipValue, int dpi);
std::pair<int, int> ComputeLogoSizeForDPI(int dpi);

// ============================================================================
// Авто-определение лимитов LRU по доступной памяти
// ============================================================================

/// Структура с рекомендуемыми лимитами для кэшей
struct LRULimits {
  int rowLRU;  ///< Лимит строк в LRU-кэше
  int tileLRU; ///< Лимит плиток в LRU-кэше
};

/// Возвращает доступную оперативную память в МБ (кросс-платформенно)
/// На Windows — реально свободная, на Linux — MemAvailable, на macOS — общий
/// объём
size_t GetAvailableRAM_MB();

/// Возвращает рекомендуемые лимиты кэша в зависимости от доступной памяти
/// @return {80, 200} если <4 ГБ, иначе {100, 300}
LRULimits GetRecommendedLRULimits();

/// Порог "мало памяти" в МБ (по умолчанию 4096)
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

wxString formatTimestamp(std::time_t timestamp);

inline void CallAfterSafeById(int winId, std::function<void(wxWindow *)> fn) {
  wxTheApp->CallAfter([winId, fn = std::move(fn)]() mutable {
    wxWindow *w = wxWindow::FindWindowById(winId);
    if (!w)
      return;
    fn(w);
  });
}

template <typename F, typename = std::enable_if_t<!std::is_convertible<
                          F, std::function<void(wxWindow *)>>::value>>
inline void CallAfterSafeById(int winId, F &&fn) {
  auto wrapper = [fn = std::forward<F>(fn)](wxWindow * /*w*/) mutable { fn(); };
  CallAfterSafeById(winId, std::function<void(wxWindow *)>(std::move(wrapper)));
}

bool NeedsEmbeddedVideoBackend(ConfigManager *cfg);
bool EnsureXWaylandForEmbeddedVideo(bool needsEmbeddedVideo);
