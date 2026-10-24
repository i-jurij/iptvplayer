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
  // i18n: описания переводятся, маски и '|' — константы кода.
  const wxString wildcard =
      _U("Video files") + " (*.mp4;*.mkv;*.avi;*.mov;*.flv;*.ts;*.m2ts;*.mts)"
                          "|*.mp4;*.mkv;*.avi;*.mov;*.flv;*.ts;*.m2ts;*.mts|"
      + _U("Playlists") + " (*.m3u;*.m3u8;*.pls;*.xspf)"
                          "|*.m3u;*.m3u8;*.pls;*.xspf|"
      + _U("Audio files") + " (*.mp3;*.flac;*.wav;*.aac;*.m4a;*.ogg)"
                            "|*.mp3;*.flac;*.wav;*.aac;*.m4a;*.ogg|"
      + _U("All files") + " (*.*)|*.*";

  // ======= 1. Получить ConfigManager =======
  Application *app = dynamic_cast<Application *>(wxTheApp);
  ConfigManager *cfg = app ? app->getConfigManager() : nullptr;

  // ======= 2. Загрузить сохранённый каталог =======
  wxString initialDir;
  if (cfg) {
    initialDir = wxString::FromUTF8(cfg->getSetting("last_open_dir", ""));
  }
  // Если каталог не сохранён или не существует — домашний каталог.
  if (initialDir.IsEmpty() || !wxDirExists(initialDir)) {
    initialDir = wxGetHomeDir();
  }

  wxFileDialog dlg(this, _U("Open file"), initialDir, "", wildcard,
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST);

  if (dlg.ShowModal() != wxID_OK)
    return;

  wxString path = dlg.GetPath();
  wxFileName fn(path);

  // ======= 3. Сохранить каталог выбранного файла =======
  if (cfg) {
    cfg->setSetting("last_open_dir", fn.GetPath().ToUTF8().data());
    cfg->saveSettings();
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
  m_currentName = NormalizeFileNameForDisk(
    std::string(fn.GetFullName().ToUTF8()), 128, Display);

  m_isChannelPlaying = false;
  m_isTempPlaylistPlaying = false;
  m_tempCurrentIndex = -1;

  m_tempState = TempPlayState::Loading;
  UpdateUiButtons();

  StartTempPlayAsync(path, -1, false, "open_file");
}

void VideoPanel::OpenCatalog() {
  Application *app = dynamic_cast<Application *>(wxTheApp);
  ConfigManager *cfg = app ? app->getConfigManager() : nullptr;
  
  wxString initialDir;
  if (cfg) {
    initialDir = wxString::FromUTF8(cfg->getSetting("last_catalog_dir", ""));
  }
  if (initialDir.IsEmpty() || !wxDirExists(initialDir)) {
    initialDir = wxGetHomeDir();
  }

  wxDirDialog dlg(this, _U("Select folder containing video files"),
                  initialDir, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
  if (dlg.ShowModal() != wxID_OK)
    return;

  wxString folderPath = dlg.GetPath();
  
  if (cfg) {
    cfg->setSetting("last_catalog_dir", folderPath.ToUTF8().data());
    cfg->saveSettings();
  }

  wxArrayString paths;
  paths.Add(folderPath);
  HandleDroppedFiles(paths, 0);
}

void VideoPanel::OpenUrl() {
  ClearTempPlaylist();

  wxTextEntryDialog dlg(this, _U("Enter URL:"), _U("Open URL"));
  if (dlg.ShowModal() != wxID_OK)
    return;

  wxString url = dlg.GetValue().Trim();
  if (url.IsEmpty())
    return;
  // Если схема не указана — считаем https. Проверяем "://", а не ":" —
  // иначе поймаем "localhost:8080" и IPv6-адрес.
  if (url.Find("://") == wxNOT_FOUND)
    url = "https://" + url;

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
  EnsurePlayerBackend();
  ClearTempPlaylist();
  std::string url = ch.getUrl();
  m_currentName = NormalizeFileNameForDisk(ch.getName(), 128, Display);
  m_isLoading = true; 
  m_loadAttempts = 0;
  m_tempState = TempPlayState::Loading;

  // ---- Сброс счётчиков EOF при старте канала ----
  m_eofFreezeCount = 0;
  m_eofLastPos = -1.0;

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
    canvas->ShowSpinner(true);
    LOG_DEBUG("PlayChannel: ShowSpinner(true) called");
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
        canvas->ShowSpinner(false);
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
    canvas->ShowSpinner(false);
    m_forceBlackActive = true;
    m_forceBlackTimer.Start(100, wxTIMER_CONTINUOUS);
  }

  // ---- Сброс счётчиков EOF ----
  m_eofFreezeCount = 0;
  m_eofLastPos = -1.0;

  if (m_isChannelPlaying || m_isFavoritePlaying) {
    m_onRequestTabSwitch(m_channelSourceTab);
  }
}

  // ============================================================================
  // Mute / Unmute logic
  // ============================================================================
  void VideoPanel::ToggleMute() {
    if (m_btnMute->GetValue()) {
      m_playerController->SetMuted(true);
      wxBitmapBundle icon = LoadSvgIcon("mute", this);
      if (icon.IsOk())
        m_btnMute->SetBitmap(icon);
    } else {
      m_playerController->SetMuted(false);
      wxBitmapBundle icon = LoadSvgIcon("unmute", this);
      if (icon.IsOk())
        m_btnMute->SetBitmap(icon);
    }
  }

  // ============================================================================
  // Volume
  // ============================================================================

  void VideoPanel::SetVolume(int vol) {
    m_lastVolume = vol;
    if (m_playerController)
      m_playerController->SetVolume(vol);
  }

  // ============================================================================
  // Fullscreen logic
  // ============================================================================
  void VideoPanel::SetUIElementsToHide(wxPanel *headerPanel, wxGauge *gaugeTop) {
    m_headerPanel = headerPanel;
    m_gaugeTop = gaugeTop;
  }

  void VideoPanel::SetControlsVisible(bool show, bool startTimer) {
    // === Управление видимостью всех элементов UI ===
    auto togglePanel = [show](wxWindow *panel) {
      if (panel) {
        show ? panel->Show() : panel->Hide();
        if (show)
          panel->Raise();
      }
    };

    togglePanel(m_headerPanel);
    togglePanel(m_controlsPanel);
    togglePanel(m_progressPanel);

    if (m_mainPanel && m_splitter) {
      wxColour bg = (m_isFullscreen && !show)
                        ? *wxBLACK
                        : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
      m_mainPanel->SetBackgroundColour(bg);
      m_splitter->SetBackgroundColour(bg);
      m_mainPanel->Refresh();
      m_splitter->Refresh();
    }
    
    // === Обновление Layout всей иерархии ===
    wxWindow *parent = GetParent();
    while (parent) {
      parent->Layout();
      parent->Refresh();
      parent = parent->GetParent();
    }
    this->Update();

    m_controlsVisible = show;

    // === Управление таймером автоскрытия ===
    if (m_isFullscreen && show && startTimer) {
      m_hideCursorTimer.Start(m_autoHideDelayMs, wxTIMER_ONE_SHOT);
    } else {
      m_hideCursorTimer.Stop();
    }
  }

  void VideoPanel::ToggleFullscreen() {
    m_isFullscreen = !m_isFullscreen;

    MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this));
    if (mf) {
      mf->ApplyFullscreen(m_isFullscreen);
    }

    if (m_isFullscreen) {
      // Вход в полноэкранный режим: скрыть UI, скрыть курсор, сменить иконку
      SetControlsVisible(false, false);
      m_videoArea->SetCursor(wxCURSOR_BLANK);
      m_cursorVisible = false;
      wxBitmapBundle icon = LoadSvgIcon("compress", this);
      if (icon.IsOk())
        m_btnFullscreen->SetBitmap(icon);
      else
        m_btnFullscreen->SetLabel(_U("Exit"));
      if (m_videoArea)
        m_videoArea->SetFocus();
      // Таймер не запускается – он запустится при движении мыши
    } else {
      // Выход из полноэкранного режима: показать UI, показать курсор, сменить
      // иконку
      SetControlsVisible(true, false);
      m_videoArea->SetCursor(wxCURSOR_DEFAULT);
      m_cursorVisible = true;
      wxBitmapBundle icon = LoadSvgIcon("expand", this);
      if (icon.IsOk())
        m_btnFullscreen->SetBitmap(icon);
      else
        m_btnFullscreen->SetLabel(_U("Full"));
      Layout();
      // Таймер останавливается (startTimer=false)
    }

    // Обновление фокуса
    if (m_focusManager)
      m_focusManager->EnsureFocus();
  }

  // ============================================================================
  // Autohide controls
  // ============================================================================
  void VideoPanel::ShowControls() { SetControlsVisible(true, true); }

  void VideoPanel::HideControls() { SetControlsVisible(false, false); }

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

  void VideoPanel::StartTempPlayAsync(const wxString &path, int sel, bool isUrl,
                                 const char *source,
                                 bool /*clearPlayNextInProgressOnFinish*/) {
    EnsurePlayerBackend();

    // помечаем, что началась загрузка — чтобы UI и логика были синхронизированы
    m_isLoading = true;
    m_loadAttempts = 0;
    m_tempState = TempPlayState::Loading;

    // ---- Сброс счётчиков EOF при старте загрузки ----
    m_eofFreezeCount = 0;
    m_eofLastPos = -1.0;

    // === Формируем запрос временного воспроизведения ===
    TempPlayRequest req;
    req.id = ++m_currentRequestId;
    req.index = sel;
    req.path = path;
    req.isUrl = isUrl;

    m_currentTempRequest = req;

    if (!isUrl) {
      wxFileName fn(path);
      m_currentName = NormalizeFileNameForDisk(
          std::string(fn.GetFullName().ToUTF8()), 128, Display);
    }

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
      canvas->ShowSpinner(true);
      LOG_DEBUG("StartTempPlayAsync: ShowSpinner(true) called");
      m_forceBlackActive = true;
      m_forceBlackTimer.Start(100, wxTIMER_CONTINUOUS);
    }

    // === Запуск воспроизведения ===
    bool ok = false;
    if (isUrl)
      ok = m_playerController->PlayUrl(std::string(path.ToUTF8()));
    else
      ok = m_playerController->PlayFile(std::string(path.ToUTF8()));

    if (!ok) {
      if (canvas) {
        canvas->SetForceBlack(false);
        canvas->ShowSpinner(false);
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

#include <map>

  void VideoPanel::CollectVideoFiles(const wxArrayString &paths,
                                     wxArrayString &outFiles, int depth) {
    if (depth > 10) {
      LOG_WARN("CollectVideoFiles: max depth exceeded");
      return;
    }

    // Карта: каталог -> список файлов в этом каталоге
    std::map<wxString, wxArrayString> dirMap;

    // Рекурсивный обход всех путей
    for (const auto &path : paths) {
      if (wxDirExists(path)) {
        wxDir dir(path);
        wxString filename;
        bool cont = dir.GetFirst(&filename);
        while (cont) {
          wxString full = path + "/" + filename;
          if (wxDirExists(full)) {
            // Рекурсивно обходим подкаталог
            wxArrayString subFiles;
            wxArrayString subPath;
            subPath.Add(full);
            CollectVideoFiles(subPath, subFiles, depth + 1);
            // Добавляем собранные файлы из подкаталога в карту по их каталогу
            for (const auto &f : subFiles) {
              wxFileName fn(f);
              wxString dirKey = fn.GetPath();
              dirMap[dirKey].Add(f);
            }
          } else if (IsVideoFile(full)) {
            dirMap[path].Add(full);
          }
          cont = dir.GetNext(&filename);
        }
      } else if (IsVideoFile(path)) {
        // Одиночный файл – помещаем в его каталог
        wxFileName fn(path);
        wxString dirKey = fn.GetPath();
        dirMap[dirKey].Add(path);
      }
    }

    // Сортируем каталоги по имени с учётом чисел
    std::vector<wxString> sortedDirs;
    for (const auto &entry : dirMap) {
      sortedDirs.push_back(entry.first);
    }
    std::sort(sortedDirs.begin(), sortedDirs.end(), CompareNamesWithNumbers);

    // Проходим по отсортированным каталогам
    for (const auto &dir : sortedDirs) {
      wxArrayString filesInDir = dirMap[dir];
      // Сортируем файлы внутри каталога по имени с учётом чисел
      std::sort(filesInDir.begin(), filesInDir.end(),
                [](const wxString &a, const wxString &b) {
                  wxFileName fa(a), fb(b);
                  return CompareNamesWithNumbers(fa.GetFullName(),
                                                 fb.GetFullName());
                });
      // Добавляем в итоговый массив
      for (const auto &f : filesInDir) {
        if (outFiles.size() >= 1000) {
          showInfo(this, _U("File limit exceeded (max 1000)"));
          return;
        }
        outFiles.Add(f);
      }
    }
  }

  void VideoPanel::HandleDroppedFiles(const wxArrayString &files,
                                      int recursionDepth) {
    wxArrayString videoFiles;
    CollectVideoFiles(files, videoFiles, recursionDepth);

    if (videoFiles.IsEmpty())
      return;

    // --- Обработка собранных файлов (без изменений) ---
    if (videoFiles.size() == 1) {
      wxString first = videoFiles[0];
      ClearTempPlaylist();
      wxFileName fn(first);
      m_currentName = NormalizeFileNameForDisk(
          std::string(fn.GetFullName().ToUTF8()), 128, Display);
      if (m_tempPlaylistPanel)
        m_tempPlaylistPanel->Hide();
      m_pendingTempPlay = true;
      m_pendingTempIndex = -1;
      m_tempState = TempPlayState::Loading;
      UpdateUiButtons();
      StartTempPlayAsync(first, -1, false, "dropped");
    } else {
      AddToTempPlaylist(videoFiles);
      ShowTempPlaylist();
      if (!m_tempPlaylist.IsEmpty()) {
        m_tempPlaylistList->SetItemState(0, wxLIST_STATE_SELECTED,
                                         wxLIST_STATE_SELECTED);
        wxString first = m_tempPlaylist[0];
        m_pendingTempPlay = true;
        m_pendingTempIndex = 0;
        m_tempState = TempPlayState::Loading;
        UpdateUiButtons();
        StartTempPlayAsync(first, 0, false, "dropped");
      }
    }
  }
