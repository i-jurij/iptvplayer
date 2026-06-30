#pragma once

#include "Application.h"
#include "Channel.h"
#include "PlayerController.h"
#include "VP_progressSlider.h"
#include "player/MpvGLCanvas.h"

#include <wx/event.h>
#include <wx/listctrl.h>
#include <wx/splitter.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>
#include <wx/wx.h>

#include <atomic>
#include <chrono>
#include <memory>

struct ProgressInfo;

class FocusManager {
public:
  FocusManager(wxWindow *videoArea, wxWindow *ownerPanel)
      : m_videoArea(videoArea), m_ownerPanel(ownerPanel) {}

  void EnsureFocus() {
    if (!m_videoArea || !m_ownerPanel)
      return;

    // Панель должна быть видима
    if (!m_ownerPanel->IsShownOnScreen())
      return;

    // Окно должно быть активным (только wxTopLevelWindow имеет IsActive)
    wxWindow *tlw = wxGetTopLevelParent(m_ownerPanel);
    wxTopLevelWindow *top = wxDynamicCast(tlw, wxTopLevelWindow);
    if (top && !top->IsActive())
      return;

    // Если фокус уже на видео — ничего не делаем
    if (m_videoArea->HasFocus())
      return;

    // Вернуть фокус
    m_videoArea->SetFocus();
  }

private:
  wxWindow *m_videoArea = nullptr;
  wxWindow *m_ownerPanel = nullptr;
};

wxDECLARE_EVENT(wxEVT_PLAYER_STATE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_PLAYER_INFO, wxCommandEvent);

class VideoPanel : public wxPanel {
public:
  VideoPanel(wxWindow *parent);
  ~VideoPanel();

  wxWindow *GetVideoArea() const { return m_videoArea; }

  std::unique_ptr<PlayerController> m_playerController;

  int GetLastVolume() const { return m_lastVolume; };

  void PlayChannel(const Channel &ch);

  // Drag & drop
  bool AcceptsFocus() const override { return true; }

  void HandleDroppedFiles(const wxArrayString &files);
  std::function<void(const wxString &)> m_onPlayerState;
  std::function<void(const wxString &)> m_onStreamInfo;
  std::function<void(const ProgressInfo &)> m_onProgress;

  void SetRecentFiles(const std::vector<wxString> &files);
  std::vector<wxString> GetRecentFiles() const;
  void AddToRecent(const wxString &path);
  std::vector<wxString> m_recentFiles;

  void SetUIElementsToHide(wxPanel *headerPanel, wxGauge *gaugeTop);

  std::function<void(int)> m_onRequestTabSwitch;

  void SetStreamInfoCallback(std::function<void(const wxString &)> cb) {
    m_onStreamInfo = std::move(cb);
  }

  void SetProgressCallback(std::function<void(const ProgressInfo &)> cb) {
    m_onProgress = std::move(cb);
  }

  void SetChannelSourceTab(int tab) { m_channelSourceTab = tab; }
  void SetIsChannelPlaying(bool is_channel) { m_isChannelPlaying = is_channel; }
  void SetIsFavoritePlaying(bool is_fav) { m_isFavoritePlaying = is_fav; }

  // Обработчик прогресса из PlayerController
  void OnProgressInfo(const ProgressInfo &info);

  // API для уведомления о видимости вкладки
  void SetTabActive(bool active);

  void StartTempPlayAsync(const wxString &path, int sel = -1,
                          bool isUrl = false, const char *source = "manual",
                          bool clearPlayNextInProgressOnFinish = false);

private:
  struct TempPlayRequest {
    uint64_t id;
    int index;
    wxString path;
    bool isUrl;
  };

  enum class TempPlayState {
    Idle,
    Requesting,
    Loading,
    FileLoaded,
    Starting,
    Playing,
    Paused,
    Stopped,
    Error
  };

  TempPlayState m_tempState = TempPlayState::Idle;
  TempPlayRequest m_currentTempRequest;
  uint64_t m_currentRequestId = 0;
  
  bool m_wasPlayingBeforeStop = false;
  std::chrono::steady_clock::time_point m_lastFileLoadedTime =
      std::chrono::steady_clock::time_point::min();
  const int kStoppedDebounceMs = 500;

  bool m_statusLockedUntilPlaying = false;
  
  wxPanel *m_loadingOverlay = nullptr;
  wxStaticText *m_loadingLabel = nullptr;

  enum class UiState { Idle, Loading, Playing, Paused };

  UiState m_uiState = UiState::Idle;
  bool m_autoPausedByTabSwitch = false;
  bool m_wasPlayingBeforeTabSwitch = false;
  bool m_tabIsActive = true;

  // Метод для централизованного обновления кнопок
  void UpdateUiButtons();

  wxSplitterWindow *m_splitter = nullptr;
  wxPanel *m_mainPanel = nullptr;

  wxStaticText *m_timeCurrentLabel = nullptr;   // "1:23:45"
  wxStaticText *m_timeDurationLabel = nullptr;  // "/ 2:30:00"
  wxStaticText *m_timeRemainingLabel = nullptr; // "-1:06:15"
  wxStaticText *m_statusLabel = nullptr;        // "Buffering...", etc.
  bool m_isDraggingProgress = false;

  static wxString FormatTime(double seconds);
  void UpdateProgressDisplay(const ProgressInfo &info);

  std::string m_currentName;
  int m_channelSourceTab = -1;
  bool m_isChannelPlaying = false;
  bool m_isFavoritePlaying = false;

  FocusManager *m_focusManager = nullptr;

  void OnFrameKey(wxKeyEvent &evt);

  bool m_isAttached = false;

  void OnWindowCreated(wxShowEvent &event);
  void OnVideoAreaPaint(wxPaintEvent &event);
  // Типы
  bool IsVideoFile(const wxString &path);
  bool IsPlaylist(const wxString &path);
  bool IsDvdFolder(const wxString &path);

  // Временный плейлист
  wxArrayString m_tempPlaylist;
  void AddToTempPlaylist(const wxArrayString &files);
  void RefreshTempPlaylistWithoutSorting();
  void ShowTempPlaylist();

  // UI
  wxPanel *m_headerPanel = nullptr;
  wxGauge *m_gaugeTop = nullptr;
  wxWindow *m_videoArea;
  wxPanel *m_controlsPanel = nullptr;
  wxTimer m_eofTimer;
  ProgressInfo m_lastProgress;

  wxButton *m_btnOpen = nullptr;
  wxButton *m_btnPlay = nullptr;
  wxButton *m_btnPause = nullptr;
  wxButton *m_btnStop = nullptr;
  wxToggleButton *m_btnMute = nullptr;
  wxSlider *m_volumeSlider = nullptr;
  wxButton *m_btnFullscreen = nullptr;

  // Боковая панель временного плейлиста
  wxPanel *m_tempPlaylistPanel = nullptr;
  wxListCtrl *m_tempPlaylistList = nullptr;

  // Меню для кнопки Open
  wxMenu *m_openMenu = nullptr;

  // State
  bool m_isMuted = false;
  bool m_isFullscreen = false;
  int m_lastVolume = 100;
  bool m_controlsVisible = true;
  bool m_isTempPlaylistPlaying = false;
  int m_tempCurrentIndex = -1;
  wxString m_lastPlayedFile;

  // -------------------------
  // Events (VP_events.cpp)
  // -------------------------
  void OnOpen(wxCommandEvent &);
  void OnPlay(wxCommandEvent &);
  void OnPause(wxCommandEvent &);
  void OnStop(wxCommandEvent &);
  void OnMute(wxCommandEvent &);
  void OnVolume(wxCommandEvent &);
  void OnFullscreen(wxCommandEvent &);
  void OnKey(wxKeyEvent &);
  void OnHideTimer(wxTimerEvent &);
  void OnTempPlaylistContextMenu(wxContextMenuEvent &evt);
  void OnTempPlaylistKeyDown(wxKeyEvent &evt);
  void OnTempPlaylistRemove();
  void OnTempPlaylistOpenFolder(wxCommandEvent &evt);
  void OnVideoAreaResize(wxSizeEvent &event);
  void OnPlayerState(wxCommandEvent &evt);
  void OnEofTimer(wxTimerEvent &evt);

  // -------------------------
  // Actions (VP_actions.cpp)
  // -------------------------
  void OpenFile();
  void OpenUrl();
  void LoadAndPlayPlaylist(const wxString &path);

  void Play();
  void Pause();
  void Stop();
  void Mute();
  void Unmute();
  void ToggleMute();
  void SetVolume(int vol);
  void ToggleFullscreen();
  void ShowControls();
  void HideControls();
  void ClearTempPlaylist();
  void OpenRecent(size_t index);

  void LoadTempPlaylistFromConfig();
  void SaveTempPlaylistToConfig();
  bool LoadPlaylistFile(const wxString &path, wxArrayString &out);
  void PlayNextTempItem();
  void OnTempPlaylistListActivate(wxListEvent &evt);
  void TempPlaylistPlay();
  void TempPlaylistMoveUp();
  void TempPlaylistMoveDown();
  void TempPlaylistRename();

  wxPanel *m_tempPlaylistButtonsPanel = nullptr;

  wxBitmapButton *m_btnPrev = nullptr;
  wxBitmapButton *m_btnNext = nullptr;
  wxBitmapButton *m_btnShuffle = nullptr;
  wxBitmapButton *m_btnRemove = nullptr;
  wxBitmapButton *m_btnClear = nullptr;

  bool m_shuffleActive = false;
  wxArrayString m_tempPlaylistOriginalOrder;
  void ToggleShuffleTempPlaylist(bool enable);
  void PlayPrevTempItem();

  // pending play (если запускаем из temp playlist — помечаем, ждём Playing)
  bool m_pendingTempPlay = false;
  int m_pendingTempIndex = -1;

  wxLongLong m_lastSeekTime = 0;
  // константа: как долго блокируем таймер после seek (мс)
  static constexpr long kSeekBlockMs = 400;
  int m_lastSeekSliderValue = 0; // позиция слайдера при последнем seek

  ProgressSlider *m_progress = nullptr;
  int m_pendingSeekPercent = -1;
  wxLongLong m_seekRequestTime = 0;
  static constexpr long kSeekConfirmTimeoutMs = 1200;
  bool m_isLiveStream = false;
};
