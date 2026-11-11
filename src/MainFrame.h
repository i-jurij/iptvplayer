#ifndef MAINFRAME_H
#define MAINFRAME_H

#include "ChannelCards.h"
#include "ChannelList.h"
#include "FavoritesCards.h"
#include "FavoritesList.h"
#include "VideoPanel.h"
#include "epg/EPGPanel.h"
#include "epg/EpgSourceManagerPanel.h"
#include "xtream/PosterCards.h"
#include "xtream/VodInfoPanel.h"

#include <wx/activityindicator.h>
#include <wx/aui/aui.h>
#include <wx/event.h>
#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/simplebook.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
#include <wx/toolbar.h>
#include <wx/wx.h>

#include <future>
#include <string>
#include <unordered_map>
#include <vector>

wxDECLARE_EVENT(EVT_UPDATE_PROGRESS, wxCommandEvent);
wxDECLARE_EVENT(EVT_UPDATE_ONE_DONE, wxCommandEvent);
wxDECLARE_EVENT(EVT_UPDATE_ALL_DONE, wxCommandEvent);
wxDECLARE_EVENT(EVT_CHANNEL_SELECTED, wxCommandEvent);

// Forward declarations
class Application;
class PlaylistManager;
class ConfigManager;
class Playlist;
class AddPlaylistFileDialog;
class AddPlaylistUrlDialog;
class EditPlaylistDialog;
class UpdateOneThread;
class UpdateAllThread;

enum class ChannelsViewState { Uninitialized, Initializing, Ready, Paused };

enum class SortMode {
  NameAsc,
  NameDesc,
  GroupAsc,
  GroupDesc,
  CountryAsc,
  CountryDesc,
  LangAsc,
  LangDesc
};

inline constexpr SortMode kSortOrder[] = {
    SortMode::NameAsc,   SortMode::NameDesc,   SortMode::GroupAsc,
    SortMode::GroupDesc, SortMode::CountryAsc, SortMode::CountryDesc,
    SortMode::LangAsc,   SortMode::LangDesc,
};

wxString SortModeLabel(SortMode m);

class MainFrame : public wxFrame {
public:
  explicit MainFrame(Application *app);
  ~MainFrame() override;

  Application *getApplication() const { return m_application; }

  bool isClosing() const { return m_closing; }
  void SetApplication(Application *app) { m_application = app; }
  // Применить начальный режим (вызывается в конструкторе, до heavy init)
  void ApplyInitialViewMode();

  void InitializeListResources();
  void InitializeGridResources();
  void TeardownGridResources();
  void TeardownListResources();

  // ----------------------------------------------------------------
  // Public helpers
  void RefreshPlaylistView();
  void adjustPlaylistColumns();
  void addPlaylistToView(size_t index, const Playlist *playlist);
  void editPlaylistAtIndex(int index);
  void updatePlaylistFromDialog(int index, const EditPlaylistDialog &dlg);
  void handlePlaylistExport(const wxString &exportPath, int playlistIndex);

  PlaylistManager *getPlaylistManager() const;
  Playlist *GetPlaylistByIndex(int idx) const;
  ConfigManager *getConfigManager() const;
  ChannelCards *GetChannelCards();

  bool validateApplication() const;
  bool validatePlaylistManager() const;
  bool validatePlaylistSelection(bool showMessage = true);
  void savePlaylistsToConfig();
  void startAutoUpdateFromSavedPlaylists();

  // Обновление при выборе плейлиста
  void loadPlaylistChannels(const std::vector<Channel> &channels,
                            const wxString &title,
                            const std::string &playlistId);

  void refreshFavorites();

  // -------------------------
  // Filters / Sorting API
  void FillFilterChoices(const std::vector<Channel> &channels);
  void ApplyFiltersAndSort(bool incremental = false);
  void ApplyFavoritesFiltersAndSort();

  // Apply show-logo flag coming from settings dialog (true = show logos)
  void SetShowLogoFromSettings(bool show);
  // в MainFrame.h / MainFrame.cpp
  bool AreLogosEnabled() const { return !m_channelsNoLogo; }

  // Флаги состояния
  ChannelsViewState m_gridState = ChannelsViewState::Uninitialized;
  ChannelsViewState m_listState = ChannelsViewState::Uninitialized;

  void PlayChannel(const Channel &ch, double startPosition = 0.0);

  void ApplyFullscreen(bool fs);
  bool m_isFullscreen = false;
#if defined(__WXMSW__)
  WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam,
                          WXLPARAM lParam) override;
#endif

  uint64_t InvalidateShowPanelToken();

  void ShowMainMenu(bool deferred = true);

  void RemoveChannelFromPlaylist(const Channel &ch);

  std::vector<Channel> GetCurrentChannels() const;
  std::string GetCurrentPlaylistId() const;

  void OpenFiles(const wxArrayString &paths);

private:
  void OnFavoritesMatchDone(wxCommandEvent& evt);

  void RestoreChannelSelection();
  void RestoreFavoriteSelection();
  
  Channel m_lastSelectedChannel;
  bool m_hasLastSelectedChannel = false;
  Channel m_lastSelectedFavorite;
  bool m_hasLastSelectedFavorite = false;

  wxTimer m_epgDebounceTimer;
  EPGPanel *m_epgPendingPanel = nullptr;
  Channel m_epgPendingChannel;
  void OnEpgDebounceTimer(wxTimerEvent &);
  
  wxSizerItem *m_mainSizerGaugeItem = nullptr;
  wxSizerItem *m_mainSizerHeaderItem = nullptr;
  wxSizerItem *m_mainSizerNotebookItem = nullptr;
  wxColour m_mainPanelBg = wxNullColour;   // wxNullColour → SYS_COLOUR_BTNFACE
  
  std::vector<std::future<void>> m_backgroundTasks;
  void CleanupFinishedTasks();
  
  wxToggleButton *m_btnEpg = nullptr;
  wxToggleButton *m_btnMovies = nullptr;
  int m_epgPageIdx = wxNOT_FOUND;
  void OnEpgToggle(wxCommandEvent &event);
  void OnMoviesToggle(wxCommandEvent &event);
  void HandleMoviesPageChanged(int sel);
  void UpdateMoviesTabVisibility();
  void ResetMoviesState();
  void OnMoviesRefreshCatalog(wxCommandEvent &event);
  void OnEpgProgress(const EpgProgressInfo &info);

  void CheckAndSuggestPlaylist();
  bool m_playlistSuggestionShown = false;

  void onAddIPTVPlaylist(wxCommandEvent &event);
  void onAddXtreamPlaylist(wxCommandEvent &event);
  void RegisterEpgSourceForPlaylist(const wxString &epgUrl,
                                    const wxString &playlistTitle);

  void OnGlobalCharHook(wxKeyEvent &evt);
  bool IsVideoPageActive() const;

  std::atomic<bool> m_ignoreNotebookEvents{false};
  
  wxToggleButton *m_btnPlaylists = nullptr;
  wxToggleButton *m_btnChannels = nullptr;
  wxToggleButton *m_btnFavorites = nullptr;
  wxToggleButton *m_btnVideo = nullptr;

  void ToggleHeaderGroup(wxToggleButton *active);
  void ToggleHeaderGroup(int index);
  void ToggleHeaderGroup(const wxString &name);

  bool m_playlistUpdated = false;

  enum { ID_VIEW_LIST = wxID_HIGHEST + 1, ID_VIEW_GRID };

  Application *m_application{nullptr};

  bool m_startInGrid = false;

  bool m_channelsNoLogo = true;

  // placeholder: later will apply flag to channel views
  void ApplyChannelsNoLogoToViews();

  // ----------------------------------------------------------------
  // UI widgets
  wxPanel *m_mainPanel{nullptr};
  wxAuiNotebook *m_notebook{nullptr};

  wxPanel *m_playlistPanel{nullptr};
  wxPanel *m_favoritesPanel{nullptr};
  VideoPanel *m_videoPanel = nullptr;
  int m_videoPageIdx = -1;

  wxStaticText *m_channelsHeader{nullptr};
  wxSimplebook *m_channelViewBook{nullptr};
  ChannelList *m_channelList{nullptr};
  ChannelCards *m_channelCards{nullptr};
  wxPanel *m_channelsPage{nullptr};
  int m_channelsPageIdx = wxNOT_FOUND;
  int m_favoritesPageIdx = -1;

#if defined(__WXMSW__)
  wxAuiToolBar *m_viewToolBar{nullptr};
#else
  wxToolBar *m_viewToolBar{nullptr};
#endif

  wxListCtrl *m_playlistList{nullptr};

  wxButton *m_infoBtn{nullptr};
  wxButton *m_openBtn{nullptr};
  wxButton *m_updateAllBtn{nullptr};
  wxButton *m_updateBtn{nullptr};
  wxButton *m_editBtn{nullptr};
  wxButton *m_removeBtn{nullptr};

  // Favorites
  wxSimplebook *m_favViewBook{nullptr};
  FavoritesList *m_favList{nullptr};
  FavoritesCards *m_favCards{nullptr};
#if defined(__WXMSW__)
  wxAuiToolBar *m_favToolBar{nullptr};
#else
  wxToolBar *m_favToolBar{nullptr};
#endif
  wxStaticText *m_favHeader{nullptr};

  // ----------------------------------------------------------------
  // Filters UI (Channels page)
  wxPanel *m_filterPanel{nullptr};
  wxChoice *m_groupChoice{nullptr};
  wxChoice *m_countryChoice{nullptr};
  wxChoice *m_langChoice{nullptr};
  wxChoice *m_sortChoice{nullptr};
  wxCheckBox *m_favFirst{nullptr};
  wxCheckBox *m_radioOnlyCheck{nullptr};
  wxButton *m_resetBtn{nullptr};
  // Favorites filters
  wxPanel *m_favFilterPanel{nullptr};
  wxChoice *m_favGroupChoice{nullptr};
  wxChoice *m_favCountryChoice{nullptr};
  wxChoice *m_favLangChoice{nullptr};
  wxChoice *m_favSortChoice{nullptr};
  wxButton *m_favFilterResetBtn{nullptr};

  // ----------------------------------------------------------------
  // Data
  std::vector<Channel> m_allChannels; // original channels for current playlist

  // ----------------------------------------------------------------
  // UI creation helpers
  void createMainPanel();
  void createStatusBar();
  void createPlaylistPanel();
  void createPlaylistList();
  wxSizer *createPlaylistButtons();
  wxSizer *createPlaylistTopButtons();
  void createChannelsView();
  void createChannelsFilterPanel();
  void createFavoritesUI();
  void showPanel(wxWindow *panel);
  void UpdateFilterPanelVisibility();
  void RestoreLastOpenedPlaylist();
  void HighlightLoadedPlaylistInList();
  void HandleChannelPageChanged(int sel);
  void HandleFavPageChanged(int sel);
  void HandlePlaylistPageChanged(int sel);
  void HandleVideoPageChanged(int sel);

  // ----------------------------------------------------------------
  // Thread‑related UI callbacks
  void onUpdateAllDone(wxCommandEvent &ev);
  void onUpdateOneDone(wxCommandEvent &ev);
  void onUpdateProgress(wxCommandEvent &ev);

  // ----------------------------------------------------------------
  // Helpers
  void updateStatusBar(size_t playlistCount);
  void resetPlaylistSelection();
  void enablePlaylistButtons(bool enable);

  // Progress gauges
  wxGauge *m_gaugeTop{nullptr};

  enum class GaugeOwner { None, Playlist, Epg };
  GaugeOwner m_gaugeOwner = GaugeOwner::None;

  void BeginPlaylistGauge(int total);
  void EndPlaylistGauge();

  // Thread‑related UI callbacks
  void onAddFromUrlSuccess(wxCommandEvent &event);
  void onAddFromUrlError(wxCommandEvent &event);
  void onProgressTimeout(wxTimerEvent &event);
  // Общая UI-подготовка перед запуском асинхронной загрузки плейлиста
  void BeginPlaylistLoading(const wxString &sourceLabel);

  int m_selectedPlaylistIndex = -1;
  int m_loadedPlaylistIndex = -1;
  std::string m_loadedPlaylistName;
  bool m_autoUpdateStarted = false;

  std::unordered_map<std::string, wxString> m_pendingEpgBySource;
  std::unordered_map<std::string, std::pair<std::string, std::string>>
      m_pendingXtreamMetaBySource;

  bool m_closing = false;

  wxTimer m_progressTimer;

  wxDECLARE_NO_COPY_CLASS(MainFrame);

  // ----------------------------------------------------------------
  // Event handlers
  void onQuit(wxCommandEvent &event);
  void OnCloseWindow(wxCloseEvent &evt);
  void onAbout(wxCommandEvent &event);
  void onSettings(wxCommandEvent &event);
  void onAddPlaylistFile(wxCommandEvent &event);
  void onAddPlaylistUrl(wxCommandEvent &event);
  void onPlaylistSelected(wxListEvent &event);
  void onPlaylistActivated(wxListEvent &event);
  void onPlaylistRightClick(wxListEvent &event);
  void onOpenPlaylist(wxCommandEvent &event);
  void onEditPlaylist(wxCommandEvent &event);
  void onRemovePlaylist(wxCommandEvent &event);
  void onPlaylistInfo(wxCommandEvent &event);
  void onUpdatePlaylist(wxCommandEvent &event);
  void onUpdateAllPlaylists(wxCommandEvent &event);
  void onPlDelKeyDown(wxKeyEvent &event);
  void onChannelSelected(const Channel &ch, size_t index, const wxRect &rect);
  void onFavoriteSelected(const Channel &ch, size_t index, const wxRect &rect);

  std::atomic<uint64_t> m_showPanelToken{0};

  void OpenPlaylistInternal(int playlistIndex);
  void OpenPlaylistByModelIndex(int playlistIndex);

  // Восстановить UI после обновления (кнопки, гейдж, список плейлистов, тайтл)
  void ResetUIAfterUpdate();

  // Очистка логотипов и .marker файлов для одного плейлиста
  void CleanupLogosForPlaylist(Playlist *pl);

  // Очистка логотипов и .marker файлов для всех плейлистов
  void CleanupLogosForAllPlaylists();

  // Получить имя плейлиста в виде wxString
  wxString GetPlaylistName(Playlist *pl) const;

  // Перезагрузка каналов текущего плейлиста (если это он)
  bool ReloadPlaylistIfCurrent(Playlist *pl);

  wxPanel *m_moviesPage = nullptr;
  PosterCards *m_moviesCards = nullptr;
  VodInfoPanel *m_moviesInfo = nullptr;
  wxChoice *m_moviesCategoryChoice = nullptr;
  wxSearchCtrl *m_moviesSearch = nullptr;
  wxChoice *m_moviesSortChoice = nullptr;
  wxTimer m_moviesSearchDebounce;
  std::string m_moviesSearchQuery;
  wxToggleButton *m_moviesModeAllBtn = nullptr;
  wxToggleButton *m_moviesModeFavBtn = nullptr;
  wxToggleButton *m_moviesModeWatchBtn = nullptr;
  wxStaticText *m_moviesHeader = nullptr;
  wxButton *m_moviesLoadMoreBtn = nullptr;
  wxActivityIndicator *m_moviesActivity = nullptr;
  int m_moviesPageIdx = wxNOT_FOUND;
  bool m_moviesPanelCreated = false;
  wxButton *m_moviesRefreshBtn = nullptr;
  bool m_moviesRefreshing = false;
  enum class MoviesListMode { All = 0, Favorites = 1, Watchlist = 2 };
  MoviesListMode m_moviesListMode = MoviesListMode::All;
  enum class MoviesSortMode {
    Default = 0,
    NameAsc = 1,
    NameDesc = 2,
    RatingDesc = 3
  };
  MoviesSortMode m_moviesSortMode = MoviesSortMode::Default;
  static constexpr int kRecentlyAddedCategoryId = -1;
  std::vector<int> m_moviesCategoryIds;
  int m_currentVodCategoryId = 0;
  int64_t m_lastVodStreamId = 0;
  int64_t m_currentVodStreamId = 0;
  bool m_vodLoading = false;

  void EnsureMoviesPanel();
  void PopulateMoviesCategories();
  void LoadVodPage(bool reset);
  void OnMoviesCategoryChanged(wxCommandEvent &event);
  void OnMoviesSearchChanged(wxCommandEvent &event);
  void OnMoviesSearchDebounce(wxTimerEvent &event);
  void ApplyMoviesSearch(const std::string &queryLower);
  void ApplyMoviesSort();
  void OnMoviesSortChanged(wxCommandEvent &event);
  void OnMoviesLoadMore(wxCommandEvent &event);
  void OnMovieSelected(const std::string &mediaKey, size_t index);
  void OnPlayVod(int64_t streamId, bool fromBeginning = false);
  void SetMoviesListMode(MoviesListMode mode);
  void LoadMoviesFavorites();
  void LoadMoviesWatchlist();
  void LoadMoviesRecentlyAdded();
  void OnMoviesShowContextMenu(const std::string &mediaKey,
                               const wxPoint &screenPos);
  void ToggleMovieInList(int64_t streamId, bool favorites);
  void ReloadMoviesView();
  // hot position + запись в playback_positions.
  struct HotPlaybackPos {
    std::string mediaKey;
    double position = 0.0;
    double duration = 0.0;
    wxLongLong lastWriteMs = 0;
    bool hasData = false;
  };
  HotPlaybackPos m_hotPos;

  void OnPlaybackProgress(const std::string &mediaKey, double pos, double dur,
                          bool flush);
  void FlushPlaybackPosition(bool force);
  void ClearPlaybackPosition();

  EPGPanel *m_epgChannels = nullptr;
  EPGPanel *m_epgFavorites = nullptr;
  EpgSourceManagerPanel *m_epgAdminPanel = nullptr;

  std::string m_lastMappingLoadedPlaylistId;

  // EpgSourceManagerPanel создаётся лениво при первом переключении на
  // вкладку Program (см. MainFrame::EnsureEpgSourcesPanel).
  bool m_epgSourcesPanelCreated = false;
  void EnsureEpgSourcesPanel();

  // Токен подписки на прогресс EPG. Отписываемся в ~MainFrame —
  // иначе колбэк с висячим this остаётся в EPGManager и даёт UAF.
  size_t m_epgProgressToken = 0;

  // Глобальный мастер-флаг автообновления плейлистов. Если OFF (по умолчанию) —
  // обновляются только плейлисты с per-playlist autoUpdate == true;
  // если ON — обновляются все, per-playlist игнорируется.
  wxCheckBox *m_playlistsAutoUpdateAllCheck = nullptr;
  void onPlaylistsAutoUpdateAll(wxCommandEvent &event);

  wxStaticLine *statusLine = nullptr;
};

#endif // MAINFRAME_H
