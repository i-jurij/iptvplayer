#ifndef MAINFRAME_H
#define MAINFRAME_H

#include "ChannelCards.h"
#include "ChannelList.h"
#include "FavoritesCards.h"
#include "FavoritesList.h"
#include "VideoPanel.h"

#include <wx/aui/aui.h>
#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/simplebook.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
#include <wx/toolbar.h>
#include <wx/wx.h>

#include <string>
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

class MainFrame : public wxFrame {
public:
  explicit MainFrame(Application *app, PlayerController *player);
  ~MainFrame() override;

  Application *getApplication() const { return m_application; }
  PlayerController *m_playerController = nullptr;

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
  void adjustTitleColumnWidth();
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
                            const wxString &title);

  void refreshFavorites();

  // -------------------------
  // Filters / Sorting API
  void FillFilterChoices(const std::vector<Channel> &channels);
  void ApplyFiltersAndSort();

  // Apply show-logo flag coming from settings dialog (true = show logos)
  void SetShowLogoFromSettings(bool show);
  // в MainFrame.h / MainFrame.cpp
  bool AreLogosEnabled() const { return !m_channelsNoLogo; }

  // Флаги состояния
  ChannelsViewState m_gridState = ChannelsViewState::Uninitialized;
  ChannelsViewState m_listState = ChannelsViewState::Uninitialized;

  void PlayChannel(const Channel &ch);

  void ApplyFullscreen(bool fs);

private:
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
  wxCheckBox *m_showLogoToggle = nullptr;

  // placeholder: later will apply flag to channel views
  void ApplyChannelsNoLogoToViews();

  // ----------------------------------------------------------------
  // UI widgets
  wxPanel *m_mainPanel{nullptr};
  wxAuiNotebook *m_notebook{nullptr};

  wxPanel *m_playlistPanel{nullptr};
  wxPanel *m_favoritesPanel{nullptr};
  wxToggleButton *m_favViewToggle{nullptr};
  VideoPanel *m_videoPanel = nullptr;
  int m_videoPageIdx = -1;

  wxStaticText *m_channelsHeader{nullptr};
  wxSimplebook *m_channelViewBook{nullptr};
  ChannelList *m_channelList{nullptr};
  ChannelCards *m_channelCards{nullptr};
  wxPanel *m_channelsPage{nullptr};
  int m_channelsPageIdx = wxNOT_FOUND;

  wxToolBar *m_viewToolBar{nullptr};

  wxStaticText *m_infoText{nullptr};

  wxListCtrl *m_playlistList{nullptr};
  wxStaticText *m_placeholderText{nullptr};

  wxBitmapButton *m_menuBtn{nullptr};
  wxButton *m_openBtn{nullptr};
  wxButton *m_updateAllBtn{nullptr};
  wxButton *m_updateBtn{nullptr};
  wxButton *m_editBtn{nullptr};
  wxButton *m_removeBtn{nullptr};

  // Favorites
  wxSimplebook *m_favViewBook{nullptr};
  FavoritesList *m_favList{nullptr};
  FavoritesCards *m_favCards{nullptr};
  wxToolBar *m_favToolBar{nullptr};
  wxStaticText *m_favHeader{nullptr};

  // ----------------------------------------------------------------
  // Filters UI (Channels page)
  wxPanel *m_filterPanel{nullptr};
  wxChoice *m_groupChoice{nullptr};
  wxChoice *m_countryChoice{nullptr};
  wxChoice *m_langChoice{nullptr};
  wxChoice *m_sortChoice{nullptr};
  wxCheckBox *m_favFirst{nullptr};
  wxButton *m_resetBtn{nullptr};

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
  wxGauge *m_progressGauge{nullptr};

  // Thread‑related UI callbacks
  void onAddFromUrlSuccess(wxCommandEvent &event);
  void onAddFromUrlError(wxCommandEvent &event);
  void onProgressTimeout(wxTimerEvent &event);

  bool m_playlistPanelVisible{false};
  int m_selectedPlaylistIndex = -1;
  int m_loadedPlaylistIndex = -1;   // индекс последнего реально открытого
  std::string m_loadedPlaylistName; // имя последнего реально открытого
  bool m_autoUpdateStarted = false;

  bool m_closing = false;

  wxTimer m_progressTimer;

  wxDECLARE_NO_COPY_CLASS(MainFrame);

  // ----------------------------------------------------------------
  // Event handlers
  void onQuit(wxCommandEvent &event);
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
  void onUpdatePlaylist(wxCommandEvent &event);
  void onUpdateAllPlaylists(wxCommandEvent &event);
  void onPlDelKeyDown(wxKeyEvent &event);
  void onToggleFavoritesView(wxCommandEvent &evt);
  void onChannelSelected(const Channel &ch, size_t index, const wxRect &rect);
  void onFavoriteSelected(const Channel &ch, size_t index, const wxRect &rect);
};

#endif // MAINFRAME_H
