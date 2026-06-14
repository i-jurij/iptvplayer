#ifndef ICON_MANAGER_H
#define ICON_MANAGER_H

#include "ErrorCode.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <wx/bitmap.h>
#include <wx/image.h>

// ============================================================================
//  IconManager — асинхронный загрузчик master-логотипов
//  (SVG / PNG / WebP / raster), без рескейла и без sync-I/O.
//  Работает только в worker-потоках, возвращает результат через UI callback.
// ============================================================================

class IconManager {
public:
  using IconCallback = std::function<void(const wxBitmap &)>;

  // ------------------------------------------------------------------------
  //  ИНИЦИАЛИЗАЦИЯ / ЗАВЕРШЕНИЕ
  // ------------------------------------------------------------------------
  static void Shutdown();

  // ------------------------------------------------------------------------
  //  ОСНОВНОЙ API
  // ------------------------------------------------------------------------
  //
  //  EnsureIconAsync:
  //      - загружает master-логотип (оригинальный размер)
  //      - НЕ масштабирует
  //      - НЕ блокирует UI
  //      - НЕ делает sync-диск операций в UI
  //
  //  Поведение:
  //      1) Проверяет наличие master-файла на диске (SVG/PNG/WebP)
  //      2) Если есть — декодирует в worker-потоке → callback(master)
  //      3) Если нет — скачивает → ProcessDownloadedIconAsync →
  //      callback(master)
  //
  //  Гарантия:
  //      callback вызывается ТОЛЬКО в UI-потоке (через CallAfter)
  //
  static void EnsureIconAsync(const std::string &playlist,
                              const std::string &channel,
                              const std::string &url, IconCallback cb);

  // ------------------------------------------------------------------------
  //  ПУТИ К ФАЙЛАМ
  // ------------------------------------------------------------------------
  static std::string GetCacheDir();
  static std::string GetIconPath(const std::string &playlist,
                                 const std::string &channel);
  static std::string GetSvgPath(const std::string &playlist,
                                const std::string &channel);

  // ------------------------------------------------------------------------
  //  УТИЛИТЫ ДЛЯ ОЧИСТКИ
  // ------------------------------------------------------------------------
  // Удаляет иконки для конкретного плейлиста (playlist может быть UTF-8)
  // Возвращает ErrorCode::OK при успехе.
  static ErrorCode DeletePlaylistIcons(const std::string &playlist);

  // Удаляет все иконки (вся папка icons)
  static ErrorCode DeleteAllIcons();
  static void CleanupUnusedIcons(const std::string &playlist,
                                 const std::vector<std::string> &validNames);
  static void PauseLoading();
  static void ResumeLoading();
  static bool IsPausedLoaded();
  static void
  ClearMemory(); // очищает очереди и освобождает RAM-кэш и временные структуры

private:
  IconManager() = delete;
  // pause control for worker queue
  static std::atomic<bool> paused;

  // ------------------------------------------------------------------------
  //  ВНУТРЕННИЕ WORKER-ФУНКЦИИ (реализация в .cpp)
  // ------------------------------------------------------------------------
  static bool DownloadIconToBuffer(const std::string &url,
                                   std::vector<unsigned char> &buffer);

  // Чтение master-файла с диска (SVG/PNG/WebP) → bitmap
  static void LoadMasterFromDiskAsync(const std::string &playlist,
                                      const std::string &channel,
                                      IconCallback cb);

  // Скачивание → ProcessDownloadedIconAsync → decode → bitmap
  static void DownloadAndProcessAsync(const std::string &playlist,
                                      const std::string &channel,
                                      const std::string &url, IconCallback cb);

  // Обработка скачанного файла (SVG/PNG/WebP/raster)
  static void ProcessDownloadedIconAsync(const std::vector<unsigned char> &data,
                                         const std::string &finalPathWebP,
                                         IconCallback cb);

  // SVG → bitmap
  static void RenderSvgAsync(const std::string &svgText, IconCallback cb);

  // PNG → bitmap
  static void DecodePngAsync(const std::vector<unsigned char> &data,
                             IconCallback cb);

  // WebP → bitmap
  static void DecodeWebpAsync(const std::vector<unsigned char> &data,
                              IconCallback cb);

  // fallback (1×1 прозрачный PNG)
  static void SaveNoLogoMarkerAsync(const std::string &pathWebP,
                                    IconCallback cb);

  // ------------------------------------------------------------------------
  //  ВНУТРЕННЯЯ ИНФРАСТРУКТУРА
  // ------------------------------------------------------------------------
  static void EnqueueTask(std::function<void()> fn);
  static void ProcessQueue();
  static void SafeCallReady(IconCallback cb, const wxBitmap &bmp);

  static std::atomic<bool> shuttingDown;
  static std::atomic<bool> queueStarted;
};

#endif // ICON_MANAGER_H
