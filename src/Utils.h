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

struct UrlAvailabilityResult {
  bool available;          // true, если код 200
  long httpCode;           // HTTP-статус или 0 при ошибке curl
  long long contentLength; // размер в байтах, -1 если неизвестен
  std::string errorText;   // описание ошибки
};

/// Проверяет доступность URL (HEAD-запрос с таймаутом).
/// Возвращает структуру с результатами.
UrlAvailabilityResult CheckUrlAvailability(
    const std::string &url, const std::string &userAgent = "",
    int timeoutSeconds = 5,
    long long maxFileSize = 250 * 1024 * 1024 // 250 МБ по умолчанию
);

bool IsNetworkUrl(const wxString &url);
inline bool IsNetworkUrl(const std::string &url) {
  return IsNetworkUrl(wxString::FromUTF8(url));
}

bool IsWindowsPlatform();
bool IsMacPlatform();
bool IsLinuxPlatform();
bool IsWaylandSession();
bool IsX11Session();

wxString FindExecutableInPath(const wxString &name);
bool IsFileExecutable(const wxString &path);
bool IsSafeSubpath(const wxString &base, const wxString &candidate);
bool SafeRemoveDirectory(const wxString &dir, std::error_code &ec);
using RemoveDirCallback = std::function<void(bool, const std::error_code &)>;
void SafeRemoveDirectoryAsync(const wxString &dir, RemoveDirCallback cb);
bool RemoveMarkerFilesRecursive(const wxString &baseDir, size_t &removed,
                                size_t &skippedUnsafe, size_t &failed,
                                bool followSymlinks);
enum NormalizeFileNameMode {
  Disk,    // файловая система: замена запрещённых символов, reserved-имена
  Display, // UI: убрать управляющие символы, сохранить читаемые пробелы
  SafeUrl  // URL/параметры: только безопасные символы
};
std::string NormalizeFileNameForDisk(const std::string &input,
                                     size_t maxLen = 200,
                                     NormalizeFileNameMode mode = Disk);
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
// Возвращает смещение локального часового пояса относительно UTC в секундах
int GetLocalTimezoneOffset();
/// Преобразует time_t (UTC) в wxDateTime с локальным временем.
/// Если t == 0, возвращает невалидный объект wxDateTime.
wxDateTime GetLocalDateTime(time_t t);
/// Форматирует локальное время time_t в строку по указанному формату.
/// Возвращает пустую строку, если t == 0 или время невалидно.
std::string FormatLocalTime(time_t t, const wxString &format);

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
