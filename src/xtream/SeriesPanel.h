#pragma once

#include "xtream/PosterCards.h"
#include "xtream/XtreamDatabase.h"

#include <wx/activityindicator.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dataview.h>
#include <wx/panel.h>
#include <wx/simplebook.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>

#include <cstdint>
#include <vector>

class MainFrame;
class Playlist;
namespace xtream {
class XtreamApiClient;
class XtreamDatabase;
} // namespace xtream

enum class SeriesSortMode {
  Default = 0,
  NameAsc = 1,
  NameDesc = 2,
  RatingDesc = 3
};

enum class SeriesListMode { All = 0, Favorites = 1, Watchlist = 2 };

struct SeriesNavState {
  int level = 0; // static_cast<int>(SeriesPanel::Level)
  int64_t seriesId = 0;
  int seasonNumber = 0;
  int categoryId = 0;
  wxString seriesName;
  int scrollY = 0;
};

class SeriesPanel : public wxPanel {
public:
  enum class Level { SeriesList = 0, Seasons = 1, Episodes = 2 };

  static constexpr int kRecentlyAddedCategoryId = -1;

  SeriesPanel(wxWindow *parent, MainFrame *mainFrame);
  ~SeriesPanel() override;

  void OnActivated();
  void Reset();
  void ClearPlayingState();
  void FocusSearch();

  Level CurrentLevel() const { return m_level; }
  void GoToLevel(Level level);
  bool GoBack();

  wxSimplebook *GetBook() const { return m_book; }

private:
  void BuildUi();
  void BuildSeriesListPage();
  void BuildSeasonsPage();
  void BuildEpisodesPage();

  void PopulateCategories();
  void LoadSeriesPage(bool reset);
  void LoadRecentlyAdded();

  void OnCategoryChanged(wxCommandEvent &event);
  void OnSearchChanged(wxCommandEvent &event);
  void OnSearchDebounce(wxTimerEvent &event);
  void OnSortChanged(wxCommandEvent &event);
  void OnModeAll(wxCommandEvent &event);
  void OnModeFavorites(wxCommandEvent &event);
  void OnModeWatchlist(wxCommandEvent &event);
  void SetSeriesListMode(SeriesListMode mode);
  void LoadSeriesFavorites();
  void LoadSeriesWatchlist();
  void ReloadSeriesView();
  void OnSeriesShowContextMenu(const std::string &mediaKey,
                               const wxPoint &screenPos);
  void ToggleSeriesInList(int64_t seriesId, bool favorites);
  void ApplySearch(const std::string &queryLower);
  void ApplySort();
  void OnLoadMore(wxCommandEvent &event);
  void OnRefreshCatalog(wxCommandEvent &event);
  void OnSeriesSelected(const std::string &mediaKey, size_t index);
  void OnSeasonSelected(const std::string &mediaKey, size_t index);
  void OnEpisodeActivated(wxDataViewEvent &event);
  void OnPlaybackEnded();
  void PlayNextEpisode();
  void OnBackClicked(wxCommandEvent &event);

  void DrillIntoSeries(int64_t seriesId, const wxString &seriesName);
  void PopulateSeasonsCards();
  void PopulateEpisodesList(int seasonNumber);
  void PlayEpisode(const xtream::XtreamDatabase::Episode &episode);

  void PushCurrentState();
  bool PopAndRestore();
  void RebuildBreadcrumbs();
  void RebuildSeasonsBreadcrumb();
  void RebuildEpisodesBreadcrumb();
  void OnEscapeKey(wxKeyEvent &evt);
  void SetBusy(bool busy);
  void UpdateInfoLabel();

  Playlist *GetCurrentPlaylist() const;
  xtream::XtreamDatabase *GetDb() const;
  xtream::XtreamApiClient *GetClient() const;

  MainFrame *m_mainFrame = nullptr;
  wxSimplebook *m_book = nullptr;
  Level m_level = Level::SeriesList;

  // Page 0 — Series list
  wxPanel *m_seriesListPage = nullptr;
  wxStaticText *m_header = nullptr;
  wxStaticText *m_infoLabel = nullptr;
  wxChoice *m_categoryChoice = nullptr;
  wxToggleButton *m_modeAllBtn = nullptr;
  wxToggleButton *m_modeFavBtn = nullptr;
  wxToggleButton *m_modeWatchBtn = nullptr;
  SeriesListMode m_listMode = SeriesListMode::All;
  wxSearchCtrl *m_search = nullptr;
  wxChoice *m_sortChoice = nullptr;
  wxTimer m_searchDebounce;
  std::string m_searchQuery;
  SeriesSortMode m_sortMode = SeriesSortMode::Default;
  wxButton *m_refreshBtn = nullptr;
  wxActivityIndicator *m_activity = nullptr;
  wxButton *m_loadMoreBtn = nullptr;
  PosterCards *m_cards = nullptr;

  std::vector<int> m_categoryIds;
  int m_currentCategoryId = 0;
  int64_t m_lastSeriesId = 0;
  bool m_loading = false;
  bool m_activatedOnce = false;
  bool m_refreshing = false;

  // Page 1 — Seasons
  wxPanel *m_seasonsPage = nullptr;
  wxButton *m_seasonsBackBtn = nullptr;
  wxBoxSizer *m_seasonsBreadcrumbSizer = nullptr;
  wxActivityIndicator *m_seasonsActivity = nullptr;
  PosterCards *m_seasonsCards = nullptr;

  // Page 2 — Episodes
  wxPanel *m_episodesPage = nullptr;
  wxButton *m_episodesBackBtn = nullptr;
  wxBoxSizer *m_episodesBreadcrumbSizer = nullptr;
  wxDataViewListCtrl *m_episodesList = nullptr;

  // Drill-down state
  int64_t m_currentSeriesId = 0;
  int64_t m_playingSeriesId = 0;
  int m_playingSeason = 0;
  std::string m_playingEpisodeId;
  wxString m_currentSeriesName;
  int m_currentSeasonNumber = 0;
  int64_t m_pendingSeriesId = 0;

  std::vector<xtream::XtreamDatabase::Season> m_currentSeasons;
  std::vector<xtream::XtreamDatabase::Episode> m_currentEpisodes;
  std::vector<SeriesNavState> m_navStack;
};
