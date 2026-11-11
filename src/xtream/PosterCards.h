#pragma once

#include "CardsBase.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
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

  using SelectCallback =
      std::function<void(const std::string &mediaKey, size_t index)>;
  void SetSelectCallback(SelectCallback cb) {
    m_onPosterSelect = std::move(cb);
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
  void EnsureScaledIcons();

private:
  std::vector<PosterItem> m_items;
  SelectCallback m_onPosterSelect;
  wxString m_emptyMessage;
  ContextCallback m_onContext;
  ToggleCallback m_onFavoriteToggle;
  ToggleCallback m_onWatchlistToggle;

  std::string m_playlistName;
  std::unordered_set<std::string> m_requestedPosters;
  wxBitmap m_favFilled;
  wxBitmap m_watchFilled;
  double m_iconsScale = 0.0;
  wxBitmap m_favFilledScaled;
  wxBitmap m_watchFilledScaled;
};