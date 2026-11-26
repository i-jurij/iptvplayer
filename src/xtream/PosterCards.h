#pragma once

#include "CardsBase.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

struct PosterItem {
  std::string mediaKey; // "vod:<id>" / "series:<id>"
  std::string name;
  std::string posterUrl; // пусто → placeholder
  double rating = 0.0;   // 0.0 → не показывать
  int64_t added = 0;     // unix epoch; для сортировки по added
  bool isFavorite = false;
  bool isWatchlist = false;
};

class PosterCards : public CardsBase {
public:
  explicit PosterCards(wxWindow *parent);
  static constexpr int kIconSizeDip = 24;
  static constexpr int kIconMarginDip = 6;
  static constexpr int kIconGapDip = 6;

  void SetItems(const std::vector<PosterItem> &items);
  void SetItems(std::vector<PosterItem> &&items);
  void AppendItems(const std::vector<PosterItem> &items);
  using SortComparator =
      std::function<bool(const PosterItem &, const PosterItem &)>;
  void SortItems(SortComparator comp);
  // Текст, который рисуется по центру сетки, когда items пустой.
  // Пустая строка — placeholder не рисуется (базовая отрисовка).
  void SetEmptyMessage(const wxString &msg);
  // Позиция вертикального скролла (в пикселях). Используется для
  // восстановления при drill-down в SeriesPanel (Stage 5.4).
  int GetScrollY() const;
  void SetScrollY(int y);

  const std::string &GetMediaKey(size_t index) const;
  // Set keyboard focus to the first poster (used when the Movies/Series tab
  // becomes active). No-op when items are empty or widget is not shown.
  void FocusFirst();
  // Total item count in DB (for virtual grid size + ghost placeholders).
  // Pass items.size() for flat lists (Recently Added, Favorites, Watchlist,
  // Search); pass DB count for paginated category views.
  void SetTotalCount(size_t total);

  using LoadMoreCallback = std::function<void()>;
  void SetLoadMoreCallback(LoadMoreCallback cb) {
    m_loadMoreCallback = std::move(cb);
  }

  using SelectCallback =
      std::function<void(const std::string &mediaKey, size_t index)>;
  void SetSelectCallback(SelectCallback cb) {
    m_onPosterSelect = std::move(cb);
  }

  using ActivateCallback =
      std::function<void(const std::string &mediaKey, size_t index)>;
  void SetActivateCallback(ActivateCallback cb) {
    m_onActivate = std::move(cb);
  }

  using ContextCallback = std::function<void(const std::string &mediaKey,
                                             const wxPoint &screenPos)>;
  void SetContextCallback(ContextCallback cb) {
    m_onContext = std::move(cb);
  }
  using ToggleCallback = std::function<void(const std::string &mediaKey)>;
  void SetFavoriteToggleCallback(ToggleCallback cb) {
    m_onFavoriteToggle = std::move(cb);
  }
  void SetWatchlistToggleCallback(ToggleCallback cb) {
    m_onWatchlistToggle = std::move(cb);
  }

  // Пересчитать layout при смене display (переопределяем, чтобы не тащить
  // логику LogoCache из базового класса).
  void RefreshPosterLayout();

  void SetPlaylistName(const std::string &name) { m_playlistName = name; }

protected:
  void RenderTile(size_t index) override;
  void UpdateLayout() override;
  void OnPaint(wxPaintEvent &evt) override;
  wxBitmap GetStarBitmap(const Channel &ch) const override;
  void OnCardClick(size_t index, bool fav, const wxRect &rect) override;
  void OnContextMenu(wxContextMenuEvent &evt);
  void OnMouseDown(wxMouseEvent &evt) override;
  void OnKeyDownIntercept(wxKeyEvent &evt);
  void OnLeftDClick(wxMouseEvent &evt);
  void OnCardActivate(size_t index);
  void OnNearBottom() override;
  void EnsureScaledIcons();

private:
  std::vector<PosterItem> m_items;
  SelectCallback m_onPosterSelect;
  ActivateCallback m_onActivate;
  LoadMoreCallback m_loadMoreCallback;
  bool m_loadMoreInFlight = false;
  wxString m_emptyMessage;
  ContextCallback m_onContext;
  ToggleCallback m_onFavoriteToggle;
  ToggleCallback m_onWatchlistToggle;

  std::string m_playlistName;
  std::unordered_set<std::string> m_requestedPosters;

  // Reverse-index: mediaKey → index in m_items. Rebuilt whenever m_items
  // is mutated (SetItems/AppendItems/SortItems). Used by the poster-fetch
  // callback to locate the current row in O(1) without linear scan —
  // important for paginated categories with 1000+ items.
  std::unordered_map<std::string, size_t> m_mediaKeyToIndex;
  void RebuildMediaKeyIndex();

  // Hit-test/отрисовочная геометрия иконок fav/watch.
  // Возвращает {favZone, watchZone} относительно (0,0) карточки, в DIP.
  // Единственный источник формулы xIcon/yFav/yWatch — все три call-site
  // (RenderTile, OnMouseDown, OnLeftDClick) используют её.
  std::pair<wxRect, wxRect> IconZonesDip(int cardWidthDip) const;

  // Клиентский прямоугольник карточки с индексом `idx` (с учётом
  // вертикального скролла). Единственный источник истины для hit-зон
  // в mouse-обработчиках: rect из HitTestIndex не используем — на
  // прокрученных рядах он приходит в виртуальных координатах, из-за
  // чего зоны иконок уезжают.
  wxRect CardRectForIndex(int idx) const;

  // Иконные зоны fav/watch в клиентских координатах для карточки
  // `cardRect` (обычно — из CardRectForIndex). Внутри — IconZonesDip +
  // FromDIP на каждую координату.
  std::pair<wxRect, wxRect> IconZones(const wxRect &cardRect) const;
  LogoCache::LogoBitmapPtr m_ghostBmp;
  wxBitmap m_favFilled;
  wxBitmap m_watchFilled;
  double m_iconsScale = 0.0;
  wxBitmap m_favFilledScaled;
  wxBitmap m_watchFilledScaled;
  wxBitmap m_favOutlineScaled;
  wxBitmap m_watchOutlineScaled;
  int m_iconSizeDip = kIconSizeDip;
  int m_iconMarginDip = kIconMarginDip;
  int m_iconGapDip = kIconGapDip;
};