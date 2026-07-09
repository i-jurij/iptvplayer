#include "ConfigManager.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Utils.h"
#include "VP_SvgIcon.h"
#include "VideoPanel.h"

#include <wx/config.h>
#include <wx/dir.h>
#include <wx/event.h>
#include <wx/fileconf.h>
#include <wx/filedlg.h>
#include <wx/notebook.h>
#include <wx/textdlg.h>

#include <cassert>

void VideoPanel::OpenFile() {
  const wxString wildcard =
      "Video files (*.mp4;*.mkv;*.avi;*.mov;*.flv;*.ts;*.m2ts;*.mts)|"
      "*.mp4;*.mkv;*.avi;*.mov;*.flv;*.ts;*.m2ts;*.mts|"
      "Playlists (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|"
      "Audio files (*.mp3;*.flac;*.wav;*.aac;*.m4a;*.ogg)|"
      "*.mp3;*.flac;*.wav;*.aac;*.m4a;*.ogg|"
      "All files (*.*)|*.*";

  // ======= 1. Получить ConfigManager =======
  Application *app = dynamic_cast<Application *>(wxTheApp);
  ConfigManager *cfg = app ? app->getConfigManager() : nullptr;

  // ======= 2. Загрузить сохранённый каталог =======
  wxString initialDir;
  if (cfg) {
    initialDir = wxString::FromUTF8(cfg->getSetting("last_open_dir", ""));
  }
  // Если каталог не существует – сбросить (тогда будет использован системный)
  if (!wxDirExists(initialDir)) {
    initialDir = wxEmptyString;
  }

  wxFileDialog dlg(this, "Open file", initialDir, "", wildcard,
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST);

  if (dlg.ShowModal() != wxID_OK)
    return;

  wxString path = dlg.GetPath();
  wxFileName fn(path);

  // ======= 3. Сохранить каталог выбранного файла =======
  if (cfg) {
    cfg->setSetting("last_open_dir", fn.GetPath().ToUTF8().data());
  }

  bool isPlaylist = IsPlaylist(path);

  // === Плейлист-файл (m3u/m3u8/pls/xspf) ===
  if (isPlaylist) {
    wxArrayString list;
    bool ok = LoadPlaylistFile(path, list);
    if (ok) {
      LoadAndPlayPlaylist(path);
      return;
    }
  }

  // === Обычный файл ===
  ClearTempPlaylist();

  AddToRecent(path);
  m_currentName =
      NormalizeFileNameForDisk(fn.GetFullName().ToStdString(), 128, Display);

  m_isChannelPlaying = false;
  m_isTempPlaylistPlaying = false;
  m_tempCurrentIndex = -1;

  m_tempState = TempPlayState::Loading;
  UpdateUiButtons();

  StartTempPlayAsync(path, -1, false, "open_file");
}

void VideoPanel::OpenUrl() {
  ClearTempPlaylist();

  wxTextEntryDialog dlg(this, "Enter URL:", "Open URL", "https://");
  if (dlg.ShowModal() != wxID_OK)
    return;

  wxString url = dlg.GetValue();
  AddToRecent(url);

  // Проверяем: URL может быть плейлистом
  bool isPlaylist = IsPlaylist(url);
  if (isPlaylist) {
    LoadAndPlayPlaylist(url);
    return;
  }

  m_tempState = TempPlayState::Loading;
  UpdateUiButtons();

  // Используем StartTempPlayAsync с isUrl = true
  StartTempPlayAsync(url, -1, true, "open_url");
}

void VideoPanel::PlayChannel(const Channel &ch) {
  ClearTempPlaylist();
  std::string url = ch.getUrl();
  m_currentName = NormalizeFileNameForDisk(ch.getName(), 128, Display);
  m_isLoading = true; 
  m_loadAttempts = 0;
  m_tempState = TempPlayState::Loading;

  // ---- ОБНОВЛЯЕМ СТАТУС 1 ИМЕНЕМ КАНАЛА ----
  wxFrame *frame = dynamic_cast<wxFrame *>(wxGetTopLevelParent(this));
  if (frame && frame->GetStatusBar()) {
    frame->SetStatusText(wxString::FromUTF8(m_currentName), 1);
  }

  UpdateUiButtons();

  // === Устанавливаем чёрный экран СИНХРОННО ===
  MpvGLCanvas *canvas = dynamic_cast<MpvGLCanvas *>(m_videoArea);
  if (canvas) {
    canvas->SetForceBlack(true);
    m_forceBlackActive = true;
    m_forceBlackTimer.Start(100, wxTIMER_CONTINUOUS);
  }

  wxTheApp->CallAfter([this, url]() {
    bool ok = true;
    try {
      m_playerController->PlayUrl(url);
    } catch (...) {
      ok = false;
    }
    if (!ok) {
      LOG_ERROR("PlayChannel: PlayUrl failed");
      m_tempState = TempPlayState::Error;
      UpdateUiButtons();
      // Сбрасываем чёрный при ошибке
      MpvGLCanvas *canvas = dynamic_cast<MpvGLCanvas *>(m_videoArea);
      if (canvas) {
        canvas->SetForceBlack(false);
        m_forceBlackActive = false;
        m_forceBlackTimer.Stop();
      }
      return;
    }
    Play(); // вызовет Play, флаг останется true до первого кадра
  });
}

// ============================================================================
// Playback logic
// ============================================================================
void VideoPanel::Play() {
  if (!m_playerController)
    return;

  // Сбрасываем вспомогательные поля, чтобы OnPlayerState корректно обработал
  // переход
  m_lastPlayedFile = wxEmptyString;

  m_playerController->Play();
}

void VideoPanel::Pause() {
  if (!m_playerController)
    return;

  // Запрос к backend; UI обновится через OnPlayerState(Paused)
  m_playerController->Pause();
}

void VideoPanel::Stop() {
  if (m_playerController)
    m_playerController->Stop();

  m_isLoading = false;
  m_loadAttempts = 0;

  m_pendingTempPlay = false;
  m_pendingTempIndex = -1;
  m_isTempPlaylistPlaying = false;
  m_tempCurrentIndex = -1;

  // сбрасываем имя при остановке
  m_currentName.clear();

  // ---- ОЧИЩАЕМ СТАТУС 1 ----
  wxFrame *frame = dynamic_cast<wxFrame *>(wxGetTopLevelParent(this));
  if (frame && frame->GetStatusBar()) {
    frame->SetStatusText("", 1);
  }

  m_tempState = TempPlayState::Stopped;
  m_currentRequestId = 0;

  m_autoPausedByTabSwitch = false;
  
  UpdateUiButtons();

  m_eofTimer.Stop();

  MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this));
  if (mf) {
    mf->InvalidateShowPanelToken();
  }

  MpvGLCanvas *canvas = dynamic_cast<MpvGLCanvas *>(m_videoArea);
  if (canvas) {
    canvas->SetForceBlack(true);
    m_forceBlackActive = true;
    m_forceBlackTimer.Start(100, wxTIMER_CONTINUOUS);
  }

  if (m_isChannelPlaying || m_isFavoritePlaying) {
    m_onRequestTabSwitch(m_channelSourceTab);
  }
}

  // ============================================================================
  // Mute / Unmute logic
  // ============================================================================

  void VideoPanel::Mute() {
    m_isMuted = true;
    m_btnMute->SetValue(true);

    {
      wxBitmapBundle icon = LoadSvgIcon("mute", this);
      if (icon.IsOk())
        m_btnMute->SetBitmap(icon);
      else
        m_btnMute->SetLabel("Mute");
    }

    m_lastVolume = m_volumeSlider->GetValue();
    m_volumeSlider->Disable();
    m_volumeSlider->SetValue(0);

    m_playerController->SetVolume(0);
  }

  void VideoPanel::Unmute() {
    m_isMuted = false;
    m_btnMute->SetValue(false);

    {
      wxBitmapBundle icon = LoadSvgIcon("unmute", this);
      if (icon.IsOk())
        m_btnMute->SetBitmap(icon);
      else
        m_btnMute->SetLabel("Vol");
    }

    m_volumeSlider->Enable();
    m_volumeSlider->SetValue(m_lastVolume);

    m_playerController->SetVolume(m_lastVolume);
  }

  void VideoPanel::ToggleMute() {
    if (m_btnMute->GetValue()) {
      m_playerController->SetMuted(true);
    } else {
      m_playerController->SetMuted(false);
    }
  }

  // ============================================================================
  // Volume
  // ============================================================================

  void VideoPanel::SetVolume(int vol) {
    if (!m_isMuted) {
      m_lastVolume = vol;
      m_playerController->SetVolume(vol);
    }
  }

  // ============================================================================
  // Fullscreen logic
  // ============================================================================
  void VideoPanel::SetUIElementsToHide(wxPanel * headerPanel,
                                       wxGauge * gaugeTop) {
    m_headerPanel = headerPanel;
    m_gaugeTop = gaugeTop;
  }

  void VideoPanel::ToggleFullscreen() {
    m_isFullscreen = !m_isFullscreen;

    wxFrame *frame = dynamic_cast<wxFrame *>(wxGetTopLevelParent(this));
    if (!frame)
      return;

    frame->ShowFullScreen(m_isFullscreen, wxFULLSCREEN_ALL);

    wxWindow *parent = GetParent();               // это m_notebook
    wxWindow *grandparent = parent->GetParent();  // это m_mainPanel
    wxSizer *mainSizer = grandparent->GetSizer(); // сизер MainFrame

    if (m_isFullscreen) {
      // Убираем отступы у notebook в fullscreen
      mainSizer->Detach(parent);
      mainSizer->Add(parent, 1,
                     wxEXPAND); // убираем wxLEFT | wxRIGHT | wxBOTTOM, 6
      grandparent->Layout();
      // Скрываем курсор
      // wxCursor blankCursor(wxCURSOR_BLANK);
      // m_videoArea->SetCursor(blankCursor);
      m_videoArea->SetCursor(wxCURSOR_BLANK);
      m_cursorVisible = false;

      // Скрываем header и gauge
      if (m_headerPanel)
        m_headerPanel->Hide();
      if (m_gaugeTop)
        m_gaugeTop->Hide();

      m_controlsPanel->Hide();

      // m_progress->Hide();
      if (m_progressPanel)
        m_progressPanel->Hide();

      wxBitmapBundle icon = LoadSvgIcon("compress", this);
      if (icon.IsOk())
        m_btnFullscreen->SetBitmap(icon);
      else
        m_btnFullscreen->SetLabel("Exit");

      if (m_videoArea)
        m_videoArea->SetFocus();

      m_isFullscreen = true;
    } else {
      // Восстанавливаем отступы при выходе из fullscreen
      mainSizer->Detach(parent);
      mainSizer->Add(parent, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
      grandparent->Layout();
      // Восстанавливаем обычный курсор
      // m_videoArea->SetCursor(wxCURSOR_DEFAULT);
      m_videoArea->SetCursor(wxCURSOR_DEFAULT);
      m_cursorVisible = true;

      // Показываем обратно
      if (m_headerPanel)
        m_headerPanel->Show();
      // if (m_gaugeTop)
      // m_gaugeTop->Show();

      m_controlsPanel->Show();
      // m_progress->Show();
      if (m_progressPanel)
        m_progressPanel->Show();

      Layout();
      m_controlsVisible = true;

      wxBitmapBundle icon = LoadSvgIcon("expand", this);
      if (icon.IsOk())
        m_btnFullscreen->SetBitmap(icon);
      else
        m_btnFullscreen->SetLabel("Full");

      m_isFullscreen = false;
    }

    // Перепроверь Layout на родителе
    if (parent)
      parent->Layout();

    // 🔥 Автоматический возврат фокуса на видео,
    // но только если панель активна и видима
    if (m_focusManager)
      m_focusManager->EnsureFocus();
  }

  // ============================================================================
  // Autohide controls
  // ============================================================================
  void VideoPanel::ShowControls() {
    if (!m_controlsVisible) {
      m_controlsPanel->Show();
      m_progress->Show();
      m_controlsPanel->Refresh();
      m_progress->Refresh();
      Layout();
      m_controlsVisible = true;

      // LOG_DEBUG("ShowControls: visible=true, fullscreen=%d", m_isFullscreen);
    }
  }

  void VideoPanel::HideControls() {
    if (m_controlsVisible && m_isFullscreen) {
      m_controlsPanel->Hide();
      m_progress->Hide();
      m_controlsPanel->Refresh();
      m_progress->Refresh();
      Layout();
      m_controlsVisible = false;

      // LOG_DEBUG("HideControls: visible=false");
    }
  }

  // ============================================================================
  // Helpers
  // ============================================================================

  bool VideoPanel::IsVideoFile(const wxString &path) {
    wxString ext = path.AfterLast('.').Lower();

    static const wxArrayString videoExt = {"mp4", "mkv",  "avi", "mov",
                                           "ts",  "mpeg", "mpg", "webm"};

    return videoExt.Index(ext) != wxNOT_FOUND;
  }

  bool VideoPanel::IsPlaylist(const wxString &path) {
    wxString ext = path.AfterLast('.').Lower();
    return ext == "m3u" || ext == "m3u8";
  }

  bool VideoPanel::IsDvdFolder(const wxString &path) {
    return wxDirExists(path + "/VIDEO_TS");
  }

  void VideoPanel::SetRecentFiles(const std::vector<wxString> &files) {
    m_recentFiles = files;
  }

  std::vector<wxString> VideoPanel::GetRecentFiles() const {
    return m_recentFiles;
  }

  void VideoPanel::AddToRecent(const wxString &path) {
    // LOG_DEBUG("VideoPanel: AddToRecent(%s)", path);

    // Удаляем дубликат
    auto it = std::find(m_recentFiles.begin(), m_recentFiles.end(), path);
    if (it != m_recentFiles.end()) {
      m_recentFiles.erase(it);
    }

    // Добавляем в начало
    m_recentFiles.insert(m_recentFiles.begin(), path);

    // Ограничиваем размер MRU
    const size_t MAX_RECENT = 10;
    if (m_recentFiles.size() > MAX_RECENT) {
      m_recentFiles.resize(MAX_RECENT);
    }
  }

  void VideoPanel::OpenRecent(size_t index) {
    if (index >= m_recentFiles.size())
      return;

    wxString item = m_recentFiles[index];

    bool isUrl = item.StartsWith("http://") || item.StartsWith("https://") ||
                 item.StartsWith("rtsp://");

    bool isPlaylist = IsPlaylist(item);

    // --- Плейлист ---
    if (isPlaylist) {
      LoadAndPlayPlaylist(item);
      return;
    }

    // --- URL ---
    if (isUrl) {
      bool isPlaylistUrl = IsPlaylist(item);
      if (isPlaylistUrl) {
        LoadAndPlayPlaylist(item);
        return;
      }

      // обычный URL
      ClearTempPlaylist();

      m_tempState = TempPlayState::Loading;
      UpdateUiButtons();

      // isUrl = true для URL
      StartTempPlayAsync(item, -1, true, "open_recent_url");

      return;
    }

    // --- Обычный файл ---
    ClearTempPlaylist();

    m_tempState = TempPlayState::Loading;
    UpdateUiButtons();

    StartTempPlayAsync(item, -1, false, "open_recent_file");
  }

  void VideoPanel::LoadAndPlayPlaylist(const wxString &path) {
    wxArrayString list;
    if (!LoadPlaylistFile(path, list) || list.IsEmpty())
      return;

    ClearTempPlaylist();
    AddToTempPlaylist(list);
    ShowTempPlaylist();

    // выделяем первый
    m_tempPlaylistList->SetItemState(0, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
    m_tempPlaylistList->EnsureVisible(0);

    m_isChannelPlaying = false;

    // Помечаем pending запуск из temp playlist и сохраняем индекс 0
    m_pendingTempPlay = true;
    m_pendingTempIndex = 0;

    m_tempState = TempPlayState::Loading;
    UpdateUiButtons();

    StartTempPlayAsync(list[0], 0, false, "load_playlist");
  }

  void
  VideoPanel::StartTempPlayAsync(const wxString &path, int sel, bool isUrl,
                                 const char *source,
                                 bool /*clearPlayNextInProgressOnFinish*/) {
    // помечаем, что началась загрузка — чтобы UI и логика были синхронизированы
    m_isLoading = true;
    m_loadAttempts = 0;
    m_tempState = TempPlayState::Loading;

    // === Формируем запрос временного воспроизведения ===
    TempPlayRequest req;
    req.id = ++m_currentRequestId;
    req.index = sel;
    req.path = path;
    req.isUrl = isUrl;

    m_currentTempRequest = req;

    // === Обновляем состояние state‑машины ===
    m_tempState = TempPlayState::Requesting;
    m_pendingTempPlay = true;
    m_pendingTempIndex = sel;

    m_isTempPlaylistPlaying = false;
    m_tempCurrentIndex = -1;

    // === Сбрасываем старые поля ===
    m_isTempPlaylistPlaying = false;
    m_isChannelPlaying = false;
    m_isFavoritePlaying = false;

    MpvGLCanvas *canvas = dynamic_cast<MpvGLCanvas *>(m_videoArea);
    if (canvas) {
      canvas->SetForceBlack(true);
      m_forceBlackActive = true;
      m_forceBlackTimer.Start(100, wxTIMER_CONTINUOUS);
    }

    // === Запуск воспроизведения ===
    bool ok = false;
    if (isUrl)
      ok = m_playerController->PlayUrl(path.ToStdString());
    else
      ok = m_playerController->PlayFile(path.ToStdString());

    if (!ok) {
      if (canvas) {
        canvas->SetForceBlack(false);
        m_forceBlackActive = false;
        m_forceBlackTimer.Stop();
      }

      LOG_ERROR("StartTempPlayAsync: Play failed (source=%s path=%s)", source,
                std::string(path.ToUTF8()).c_str());

      m_pendingTempPlay = false;
      m_pendingTempIndex = -1;
      m_tempState = TempPlayState::Error;
      UpdateUiButtons();
      return;
    }

    // === Обновляем выделение в списке ===
    if (sel >= 0 && m_tempPlaylistList) {
      m_tempPlaylistList->SetItemState(sel, wxLIST_STATE_SELECTED,
                                       wxLIST_STATE_SELECTED);

      this->CallAfter([this, sel]() {
        if (m_tempPlaylistList) {
          m_tempPlaylistList->SetFocus();
          m_tempPlaylistList->EnsureVisible(sel);
        }
      });
    }

    // === UI: Loading ===
    UpdateUiButtons();
  }
