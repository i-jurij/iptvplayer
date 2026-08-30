#pragma once

#include "Channel.h"
#include "LogoCache.h"

#include <wx/dcbuffer.h>
#include <wx/scrolwin.h>
#include <wx/wx.h>

#include <atomic>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class MainFrame;

// Вспомогательная функция: унифицированный ключ кэша (совместим с
// IconManager) Формат: "playlist_channel_WxH"
inline std::string MakeLogoCacheKey(const std::string &playlist,
                                    const std::string &channelOrUrl, int w,
                                    int h, int dpi) {
  // playlist может быть пустым — тогда используем URL как идентификатор
  std::string id =
      playlist.empty() ? channelOrUrl : playlist + "|" + channelOrUrl;

  return id + "|" + std::to_string(w) + "x" + std::to_string(h) + "|" +
         std::to_string(dpi);
}

class CardsBase : public wxScrolledWindow {
public:
  MainFrame *GetMainFrame() const;
  // Горизонтальный отступ для центрирования сетки внутри клиентской области
  // (в пикселях)
  int m_gridOffsetX = 0;
  // header
  wxPoint m_lastMouseClientPos{0, 0};

  // Утилита: получить абсолютный rect карточки по колонке/строке
  wxRect GetCardRect(int col, int row) const;
  wxRect GetCardRect(size_t index) const;
  void InvalidateCardClientRect(size_t index, bool eraseBackground = false);
  void InvalidateCardClientRectByIndex(int cardIndex,
                                       bool eraseBackground = false);
  wxRect GetStarRect(int col, int row) const;
  wxPoint GetCardClientCenter(int col, int row) const;
  // Ensure that the given row is visible; scrolls if necessary.
  void EnsureRowVisible(int row);

  using SelectCallback =
      std::function<void(const Channel &, size_t, const wxRect &)>;

  CardsBase(wxWindow *parent);

  void SetSelectCallback(SelectCallback cb) { m_onSelect = std::move(cb); }
  void SetChannelsBase(const std::vector<Channel> &channels);
  void RefreshCards();
  static std::pair<int, int> ComputeLogoSize(int dpi);

  // Инициализация лимитов кэша (вызывать один раз при старте приложения)
  void InitLRULimits();

  // Публичный доступ к лимитам (для отладки/тестов)
  static int GetMaxTileLRU() { return MAX_TILE_LRU; }

  // Information about card layout — all sizes in pixels
  struct LayoutInfo {
    int cardW = 0, cardH = 0; // card width/height
    int pad = 0, logoGap = 0; // padding (in pixels)
    int starSize = 0;         // star size
    int logoZoneLeft = 0, logoZoneRight = 0, logoZoneW = 0; // logo zone
    int favZoneSize = 0;        // right square zone size
    int logoW = 0, logoH = 0;   // actual logo size
    int logoDx = 0, logoDy = 0; // absolute coordinates inside card (pixels)
    int starDx = 0, starDy = 0; // absolute coordinates inside card (pixels)
  };
  // layout
  int m_cardW = 0;
  int m_cardH = 0;
  int m_cols = 0;
  int m_gapX = 0;
  int m_gapY = 0;
  int m_pad = 0;
  int m_logoGap = 0;
  int m_starW = 0;
  int m_logoW = 0;
  int m_logoH = 0;
  int m_rowH = 0;
  int m_colW = 0;

  std::vector<Channel> m_channels;
  std::unordered_set<std::string> m_favorites;

  SelectCallback m_onSelect;

  void TrimTextCache();
  // Полная очистка всех RAM-кэшей (локальных и DPI-aware).
  // Если clearScaled==true — очищаются отмасштабированные битмапы
  // (LogoCache::ClearMemory вызывается). Если clearLRU==true — очищаются
  // LRU-кэши плиток/строк. Если clearTextLayout==true — очищаются текстовые
  // и layout-кэши.
  void ClearAllCaches(bool clearLRU = true, bool clearTextLayout = false);

  // Приостановить все фоновые загрузки/рескейлы логотипов (без удаления
  // очередей). Используется перед массовой очисткой диска/памяти.
  void PauseLogoLoading();
  // Возобновить фоновые загрузки/рескейлы логотипов.
  void ResumeLogoLoading();

  void IncrementCacheVersion() {
    m_cacheVersion.fetch_add(1, std::memory_order_relaxed);
  }
  uint64_t GetCacheVersion() const {
    return m_cacheVersion.load(std::memory_order_relaxed);
  }
  // Инвалидировать все видимые/виртуальные карточки (форсированная
  // перерисовка).
  void InvalidateAll();

  void WarmUpFavorites();
  void WarmUpTiles();
  void StopWarmUp();

  void SelectCard(int index); // установит фокус, прокрутит, перерисует
  const std::vector<Channel> &GetChannels() const { return m_channels; }

protected:
  void OnContextMenu(wxContextMenuEvent &evt);
  
  bool m_mouseInside = false;
  int m_focusIndex = -1; // клавиатурный фокус
  int m_hoverIndex = -1; // hover мыши

  std::unordered_map<std::string, std::vector<int>> m_scaledKeyToIndices;
  void OnLogoScaledReady(const std::string &scaledKey);

  int m_lastTooltipIndex = -1;
  void UpdateTooltip(int index);

  void UpdateHoverAtPoint(const wxPoint &clientPos);
  void OnScroll(wxScrollWinEvent &evt);
  void OnMouseWheel(wxMouseEvent &evt);
  void OnMouseEnter(wxMouseEvent &evt);

  void OnDPIChanged(wxDPIChangedEvent &evt);
  int GetCurrentDPI() const;

  void EnqueueLogoPriority(size_t index, int priority);

  // LRU helpers now accept shared_ptr
  void AddRowToLRU(int row, const LogoCache::LogoBitmapPtr &bmpPtr);
  void AddTileToLRU(size_t index, const LogoCache::LogoBitmapPtr &bmpPtr);

  static int GetStarSizeForCardH(int cardH);
  virtual wxBitmap GetStarBitmap(const Channel &ch) const = 0;
  virtual void OnCardClick(size_t index, bool fav, const wxRect &rect) = 0;

  // общая отрисовка
  void DrawCardBase(wxDC &dc, size_t index, const wxRect &rect, bool hovered);

  // общая логика
  void UpdateLayout();
  int HitTestIndex(const wxPoint &pos, bool &fav, wxRect *outRect) const;

  void RequestLogo(size_t index);
  void EnqueueLogo(size_t index);
  void ProcessLogoQueue(wxTimerEvent &);

  void MarkCardDirty(int index);
  void OnRedrawTimer(wxTimerEvent &);

  // обработчики
  void OnPaint(wxPaintEvent &event);
  void OnResize(wxSizeEvent &event);
  void OnMouseMove(wxMouseEvent &event);
  void OnMouseLeave(wxMouseEvent &event);
  void OnMouseDown(wxMouseEvent &event);
  void OnDestroy(wxWindowDestroyEvent &event);
  void OnKeyDown(wxKeyEvent &evt);

  // LRU-кэш плиток (index → shared_ptr<wxBitmap>)
  std::list<size_t> m_tileLRU;
  std::unordered_map<size_t, LogoCache::LogoBitmapPtr> m_tileLRUCache;

  static int MAX_TILE_LRU;

  int m_dynamicParallel = 2; // стартовое значение
  static constexpr int MIN_PARALLEL_LOADS = 1;

  int m_lastLoadTimeMs = 0;
  wxLongLong m_lastLoadStart;

  struct LogoTask {
    size_t index;
    int priority;
  };

  std::deque<LogoTask> m_logoQueuePQ;

  static constexpr int MAX_LOGO_QUEUE = 2000;

  int m_lastScrollY = 0;
  int m_scrollDirection = 0; // -1 вверх, +1 вниз

  // DPI-aware caches
  std::unordered_map<int, std::unordered_map<size_t, LogoCache::LogoBitmapPtr>>
      m_tileCacheDPI;

  // текущий DPI
  int m_currentDPI = 0;

  void RenderTile(size_t index);

  bool RemoveChannel(const std::string &name, const std::string &playlistName);

private:
  wxBitmap CreateTileBackground(int w, int h);
  // --- DPI & Layout ---
  LayoutInfo ComputeLayout(int normDPI) const;
  LayoutInfo GetLayoutInfoForDPI(int rawDPI) const;
  bool IsBitmapNonEmpty(const wxBitmap &bmp);

  // Cache: normalized DPI -> LayoutInfo (thread-safe)
  mutable std::map<int, LayoutInfo> m_layoutCache;
  mutable std::mutex m_layoutCacheMutex;

  wxBitmap GetScaledStar(const wxBitmap &star, int size);
  // text cache
  std::unordered_map<std::string, wxString> m_textCache;

  wxString GetTruncatedText(const Channel &ch);
  // text size cache
  std::unordered_map<std::string, std::pair<int, int>> m_textSizeCache;

  // hover
  bool m_hoverFav = false;

  bool m_closing = false;

  // batched redraw
  wxTimer m_redrawTimer;
  std::vector<int> m_dirtyCards;

  // throttling
  wxTimer m_logoTimer;
  int m_activeLoads = 0;
  static constexpr int MAX_PARALLEL_LOADS = 4;

  int FromDIP(int v) const { return wxWindow::FromDIP(v, this); }

  wxDECLARE_EVENT_TABLE();

  std::atomic<uint64_t> m_channelsVersion{0};

  // Мьютекс для защиты локальных кэшей (m_scaledCache, DPI-кэши и
  // т.д.)
  std::mutex m_cacheMutex;

  // Версия кэша: bump'ается при ClearAllCaches, используется для игнорирования
  // устаревших колбэков.
  std::atomic<uint64_t> m_cacheVersion{0};

  // Флаг паузы загрузок (локальный для CardsBase). При true — не стартуем новые
  // загрузки.
  std::atomic<bool> m_loadingPaused{false};

  wxTimer m_scrollStopTimer;
  wxLongLong m_skipWarmupUntil = 0;
  wxLongLong m_lastScrollEvent = 0;
  void OnScrollStop(wxTimerEvent &);
};
