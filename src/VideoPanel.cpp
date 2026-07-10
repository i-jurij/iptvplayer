#include "VideoPanel.h"
#include "BackendFactory.h"
#include "ConfigManager.h"
#include "EventIDs.h"
#include "LogControl.h"
#include "MpvBackend.h"
#include "VP_SvgIcon.h"

#include <wx/dnd.h>
#include <wx/event.h>
#include <wx/listctrl.h>
#include <wx/splitter.h>
#include <wx/statbmp.h>

#include <memory>

wxDEFINE_EVENT(wxEVT_PLAYER_STATE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_PLAYER_INFO, wxCommandEvent);

class VP_FileDropTarget : public wxFileDropTarget {
public:
  VideoPanel *m_owner;

  VP_FileDropTarget(VideoPanel *owner) : m_owner(owner) {}

  bool OnDropFiles(wxCoord, wxCoord, const wxArrayString &filenames) override {
    if (m_owner)
      m_owner->HandleDroppedFiles(filenames);
    return true;
  }
};

VideoPanel::VideoPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY) {

  auto *rootSizer = new wxBoxSizer(wxHORIZONTAL);

  // ------------------------------------------------------------
  // Splitter — создаём СНАЧАЛА
  // ------------------------------------------------------------
  m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition,
                                    wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
  m_splitter->SetMinimumPaneSize(120);

  // ------------------------------------------------------------
  // Боковая панель временного плейлиста (родитель — splitter)
  // ------------------------------------------------------------
  m_tempPlaylistPanel = new wxPanel(m_splitter, wxID_ANY);
  wxBoxSizer *tempSizer = new wxBoxSizer(wxVERTICAL);

  // === Заголовок панели ===
  wxPanel *header = new wxPanel(m_tempPlaylistPanel, wxID_ANY);
  wxBoxSizer *headerSizer = new wxBoxSizer(wxHORIZONTAL);

  wxStaticText *lbl = new wxStaticText(header, wxID_ANY, "Temporary playlist");

  headerSizer->Add(lbl, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
  header->SetSizer(headerSizer);

  tempSizer->Add(header, 0, wxEXPAND | wxTOP | wxBOTTOM, 3);

  // === Список файлов ===
  m_tempPlaylistList = new wxListCtrl(
      m_tempPlaylistPanel, ID_VP_TEMP_PLAYLIST_LIST, wxDefaultPosition,
      wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);

  // Колонки
  m_tempPlaylistList->InsertColumn(0, "#", wxLIST_FORMAT_RIGHT, FromDIP(40));
  m_tempPlaylistList->InsertColumn(1, "Name", wxLIST_FORMAT_LEFT, FromDIP(260));

  tempSizer->Add(m_tempPlaylistList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                 5);

  m_tempPlaylistPanel->SetSizer(tempSizer);

  // --- Нижняя панель кнопок временного плейлиста ---
  m_tempPlaylistButtonsPanel = new wxPanel(m_tempPlaylistPanel);
  auto *btnSizer = new wxBoxSizer(wxHORIZONTAL);

  // Загружаем SVG-иконки (wxBitmapBundle)
  wxBitmapBundle prevIcon = LoadSvgIcon("prev", this);
  wxBitmapBundle nextIcon = LoadSvgIcon("next", this);
  wxBitmapBundle shuffleIcon = LoadSvgIcon("shuffle", this);
  wxBitmapBundle removeIcon = LoadSvgIcon("remove", this);
  wxBitmapBundle clearIcon = LoadSvgIcon("clear", this);

  m_btnPrev =
      new wxBitmapButton(m_tempPlaylistButtonsPanel, wxID_ANY, prevIcon);
  m_btnNext =
      new wxBitmapButton(m_tempPlaylistButtonsPanel, wxID_ANY, nextIcon);
  m_btnShuffle =
      new wxBitmapButton(m_tempPlaylistButtonsPanel, wxID_ANY, shuffleIcon);
  m_btnRemove =
      new wxBitmapButton(m_tempPlaylistButtonsPanel, wxID_ANY, removeIcon);
  m_btnClear =
      new wxBitmapButton(m_tempPlaylistButtonsPanel, wxID_ANY, clearIcon);

  // Tooltips
  m_btnPrev->SetToolTip("Previous");
  m_btnNext->SetToolTip("Next");
  m_btnShuffle->SetToolTip("Shuffle / Restore");
  m_btnRemove->SetToolTip("Remove selected");
  m_btnClear->SetToolTip("Clear playlist");

  // Добавляем кнопки в сайзер
  btnSizer->Add(m_btnPrev, 0, wxALL, FromDIP(5));
  btnSizer->Add(m_btnNext, 0, wxALL, FromDIP(5)); // разделитель
  btnSizer->AddStretchSpacer(1);
  btnSizer->Add(m_btnShuffle, 0, wxALL, FromDIP(5)); // разделитель
  btnSizer->AddStretchSpacer(1);
  btnSizer->Add(m_btnRemove, 0, wxALL, FromDIP(5));
  btnSizer->Add(m_btnClear, 0, wxALL, FromDIP(5));

  // Применяем сайзер
  m_tempPlaylistButtonsPanel->SetSizer(btnSizer);
  // Добавляем панель в общий сайзер плейлиста
  m_tempPlaylistPanel->GetSizer()->Add(m_tempPlaylistButtonsPanel, 0,
                                       wxEXPAND | wxTOP, FromDIP(4));

  m_tempPlaylistPanel->Hide();

  m_tempPlaylistList->Bind(wxEVT_LIST_ITEM_ACTIVATED,
                           &VideoPanel::OnTempPlaylistListActivate, this);
  m_tempPlaylistList->Bind(wxEVT_CONTEXT_MENU,
                           &VideoPanel::OnTempPlaylistContextMenu, this);
  m_tempPlaylistList->Bind(wxEVT_KEY_DOWN, &VideoPanel::OnTempPlaylistKeyDown,
                           this);
  m_tempPlaylistList->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &evt) {
    evt.Skip();
    this->CallAfter([this]() {
      if (m_tempPlaylistList)
        m_tempPlaylistList->SetFocus();
    });
  });

  m_btnShuffle->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    m_shuffleActive = !m_shuffleActive;
    ToggleShuffleTempPlaylist(m_shuffleActive);

    if (m_shuffleActive) {
      m_btnShuffle->SetBackgroundColour(wxColour(80, 120, 255)); // подсветка
    } else {
      m_btnShuffle->SetBackgroundColour(wxNullColour); // стандартный фон
    }

    m_btnShuffle->Refresh();
  });

  m_btnClear->Bind(wxEVT_BUTTON,
                   [this](wxCommandEvent &) { ClearTempPlaylist(); });

  m_btnPrev->Bind(wxEVT_BUTTON,
                  [this](wxCommandEvent &) { PlayPrevTempItem(); });

  m_btnNext->Bind(wxEVT_BUTTON,
                  [this](wxCommandEvent &) { PlayNextTempItem(); });

  m_btnRemove->Bind(wxEVT_BUTTON,
                    [this](wxCommandEvent &) { OnTempPlaylistRemove(); });

  // ------------------------------------------------------------
  // Основная вертикальная часть (видео + прогресс + контролы)
  // ------------------------------------------------------------
  m_mainPanel = new wxPanel(m_splitter, wxID_ANY);
  auto *mainSizer = new wxBoxSizer(wxVERTICAL);

  // 1. backend
  m_playerController =
      std::make_unique<PlayerController>(CreateBackend(nullptr));

  // 2. canvas
  MpvGLCanvas *videoCanvas = new MpvGLCanvas(m_mainPanel, nullptr);
  mainSizer->Add(videoCanvas, 1, wxEXPAND);
  m_videoArea = videoCanvas;

  // 3. attach backend
  m_playerController->AttachToWindow(videoCanvas);

  // 4. передаём mpv
  mpv_handle *mpv =
      static_cast<mpv_handle *>(m_playerController->GetMpvHandle());
  videoCanvas->SetMpvHandle(mpv);

  // 5. КРИТИЧНО: принудительно вызвать OnPaint ДО PlayUrl
  videoCanvas->Show();    // даже если родитель скрыт — это нормально
  videoCanvas->Update();  // заставляет wx вызвать OnPaint
  videoCanvas->Refresh(); // помечает область для перерисовки
  wxYield();              // даёт wx выполнить OnPaint прямо сейчас

  // Progress bar
  auto *progressPanel = new wxPanel(m_mainPanel, wxID_ANY);
  m_progressPanel = progressPanel;
  auto *progressSizer = new wxBoxSizer(wxVERTICAL);
  auto *progressTimeSizer = new wxBoxSizer(wxHORIZONTAL);

  m_timeCurrentLabel = new wxStaticText(progressPanel, wxID_ANY, "00:00:00");
  m_timeCurrentLabel->SetFont(
      m_timeCurrentLabel->GetFont().MakeSmaller().MakeSmaller());
  progressTimeSizer->Add(m_timeCurrentLabel, 0,
                         wxALIGN_CENTER_VERTICAL | wxLEFT, 5);

  m_progress = new ProgressSlider(progressPanel);
  m_progress->SetMax(1000);
  m_progress->m_seekCallback = [this](int val) {
    if (m_playerController->GetState() == PlayerState::Stopped) {
      return;
    }

    int percent = val / 10;
    LOG_DEBUG("Seeking to %d%% (value=%d)", percent, val);

    // Для live потоков seek может не поддерживаться
    if (m_isLiveStream) {
      LOG_DEBUG("Live stream detected, seek may not be supported");
    }

    m_pendingSeekPercent = percent;
    m_seekRequestTime = wxGetLocalTimeMillis();
    m_playerController->SeekAbsolute(percent);
  };

  progressTimeSizer->Add(m_progress, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

  m_timeRemainingLabel = new wxStaticText(progressPanel, wxID_ANY, "-00:00:00");
  m_timeRemainingLabel->SetFont(
      m_timeRemainingLabel->GetFont().MakeSmaller().MakeSmaller());
  progressTimeSizer->Add(m_timeRemainingLabel, 0,
                         wxALIGN_CENTER_VERTICAL | wxLEFT, 5);

  m_timeDurationLabel = new wxStaticText(progressPanel, wxID_ANY, "/ 00:00:00");
  m_timeDurationLabel->SetFont(
      m_timeDurationLabel->GetFont().MakeSmaller().MakeSmaller());
  progressTimeSizer->Add(m_timeDurationLabel, 0,
                         wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

  progressSizer->Add(progressTimeSizer, 0, wxEXPAND);

  progressPanel->SetSizer(progressSizer);
  mainSizer->Add(progressPanel, 0, wxEXPAND);

  // Controls panel
  m_controlsPanel = new wxPanel(m_mainPanel, wxID_ANY);
  auto *ctrlSizer = new wxBoxSizer(wxHORIZONTAL);

  // --- Open ---
  m_btnOpen = new wxButton(m_controlsPanel, wxID_ANY, "");
  {
    wxBitmapBundle icon = LoadSvgIcon("folder", this);
    if (icon.IsOk())
      m_btnOpen->SetBitmap(icon);
    else
      m_btnOpen->SetLabel("Open");
  }
  m_btnOpen->Bind(wxEVT_BUTTON, &VideoPanel::OnOpen, this);
  ctrlSizer->Add(m_btnOpen, 0, wxALL, 5);

  // --- Play ---
  m_btnPlay = new wxButton(m_controlsPanel, wxID_ANY, "");
  {
    wxBitmapBundle icon = LoadSvgIcon("play", this);
    if (icon.IsOk())
      m_btnPlay->SetBitmap(icon);
    else
      m_btnPlay->SetLabel("Play");
  }
  m_btnPlay->Bind(wxEVT_BUTTON, &VideoPanel::OnPlay, this);
  ctrlSizer->Add(m_btnPlay, 0, wxALL, 5);

  // --- Pause ---
  m_btnPause = new wxButton(m_controlsPanel, wxID_ANY, "");
  {
    wxBitmapBundle icon = LoadSvgIcon("pause-play", this);
    if (icon.IsOk())
      m_btnPause->SetBitmap(icon);
    else
      m_btnPause->SetLabel("Pause");
  }
  m_btnPause->Bind(wxEVT_BUTTON, &VideoPanel::OnPause, this);
  ctrlSizer->Add(m_btnPause, 0, wxALL, 5);

  // --- Stop ---
  m_btnStop = new wxButton(m_controlsPanel, wxID_ANY, "");
  {
    wxBitmapBundle icon = LoadSvgIcon("stop-play", this);
    if (icon.IsOk())
      m_btnStop->SetBitmap(icon);
    else
      m_btnStop->SetLabel("Stop");
  }
  m_btnStop->Bind(wxEVT_BUTTON, &VideoPanel::OnStop, this);
  ctrlSizer->Add(m_btnStop, 0, wxALL, 5);

  ctrlSizer->AddStretchSpacer(1);

  // --- Mute ---
  m_btnMute = new wxToggleButton(m_controlsPanel, wxID_ANY, "");
  {
    wxBitmapBundle icon = LoadSvgIcon("unmute", this);
    if (icon.IsOk())
      m_btnMute->SetBitmap(icon);
    else
      m_btnMute->SetLabel("Vol");
  }
  m_btnMute->Bind(wxEVT_TOGGLEBUTTON, &VideoPanel::OnMute, this);
  ctrlSizer->Add(m_btnMute, 0, wxALL, 5);

  // --- Volume slider ---
  m_volumeSlider =
      new wxSlider(m_controlsPanel, wxID_ANY, m_lastVolume, 0, 100,
                   wxDefaultPosition, wxSize(120, -1), wxSL_HORIZONTAL);

  Application *app = dynamic_cast<Application *>(wxTheApp);
  if (app) {
    ConfigManager *cfg = app->getConfigManager();
    if (cfg) {
      int vol = cfg->getInt("m_lastVolume", 100);
      m_lastVolume = std::clamp(vol, 0, 100);
      m_volumeSlider->SetValue(m_lastVolume);
      if (m_playerController)
        m_playerController->SetVolume(m_lastVolume);
    }
  }
  m_volumeSlider->Bind(wxEVT_SLIDER, &VideoPanel::OnVolume, this);
  ctrlSizer->Add(m_volumeSlider, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

  // --- Fullscreen ---
  m_btnFullscreen = new wxButton(m_controlsPanel, wxID_ANY, "");
  {
    wxBitmapBundle icon = LoadSvgIcon("expand", this);
    if (icon.IsOk())
      m_btnFullscreen->SetBitmap(icon);
    else
      m_btnFullscreen->SetLabel("Full");
  }
  m_btnFullscreen->Bind(wxEVT_BUTTON, &VideoPanel::OnFullscreen, this);
  ctrlSizer->Add(m_btnFullscreen, 0, wxALL, 5);

  m_controlsPanel->SetSizer(ctrlSizer);
  mainSizer->Add(m_controlsPanel, 0, wxEXPAND);

  m_mainPanel->SetSizer(mainSizer);

  // ------------------------------------------------------------
  // Splitter — инициализация ПОСЛЕ SetSizer и Layout
  // ------------------------------------------------------------
  rootSizer->Add(m_splitter, 1, wxEXPAND);
  SetSizer(rootSizer);
  Layout();

  m_splitter->Initialize(m_mainPanel);

  // ------------------------------------------------------------
  // Focus manager
  // ------------------------------------------------------------
  // ------------------------------------------------------------
  // Focus manager — финальная оптимизированная версия
  // ------------------------------------------------------------
  m_focusManager = new FocusManager(m_videoArea, this);

  // --- Контролы, которым разрешён фокус (кнопки) ---
  std::vector<wxWindow *> focusAllowedButtons = {
      m_btnOpen, m_btnPlay, m_btnPause, m_btnStop, m_btnMute, m_btnFullscreen};

  // --- Контролы, которым запрещён фокус (кроме drag) ---
  std::vector<wxWindow *> focusPreventControls = {
      m_btnOpen,       m_btnPlay,      m_btnPause, m_btnStop,      m_btnMute,
      m_btnFullscreen, m_volumeSlider, m_progress, m_controlsPanel};

  // --- 1) Автоматическая регистрация restoreFocus для кнопок ---
  for (wxWindow *w : focusAllowedButtons) {
    w->Bind(wxEVT_BUTTON, [this](wxCommandEvent &evt) {
      evt.Skip();
      this->CallAfter([this]() {
        if (m_videoArea)
          m_videoArea->SetFocus();
      });
    });
  }

  // --- 2) Автоматическая регистрация preventFocus ---
  for (wxWindow *w : focusPreventControls) {
    w->Bind(wxEVT_SET_FOCUS, [this, w, focusAllowedButtons](wxFocusEvent &e) {
      wxWindow *focused = wxWindow::FindFocus();

      // 1) Плейлист — разрешаем полностью
      if (m_tempPlaylistPanel && m_tempPlaylistPanel->IsDescendant(focused)) {
        e.Skip();
        return;
      }

      // 2) Volume slider — разрешаем только во время drag
      if (w == m_volumeSlider && m_volumeSlider->HasCapture()) {
        e.Skip();
        return;
      }

      // 3) Прогресс-бар
      // разрешаем только во время drag
      // if (w == m_progress && m_isDraggingProgress) {
      // Разрешаем фокус слайдеру ВСЕГДА
      if (w == m_progress) {
        e.Skip();
        return;
      }

      // 4) Кнопки — разрешаем фокус (restoreFocus вернёт его позже)
      if (std::find(focusAllowedButtons.begin(), focusAllowedButtons.end(),
                    w) != focusAllowedButtons.end()) {
        e.Skip();
        return;
      }

      // 5) Всё остальное — вернуть фокус на видео
      if (m_focusManager)
        m_focusManager->EnsureFocus();

      e.Skip();
    });
  }

  // Начальное состояние кнопок
  UpdateUiButtons();

  // Keyboard
  Bind(wxEVT_CHAR_HOOK, &VideoPanel::OnKey, this);
  wxFrame *frame = dynamic_cast<wxFrame *>(wxGetTopLevelParent(this));
  if (frame) {
    frame->Bind(wxEVT_CHAR_HOOK, &VideoPanel::OnFrameKey, this);
  }

  // Drag & drop
  SetDropTarget(new VP_FileDropTarget(this));

  Bind(wxEVT_SHOW, &VideoPanel::OnWindowCreated, this);

  if (m_playerController) {
    m_playerController->SetStreamInfoCallback([this](const StreamInfo &info) {
      // Обрезаем описания кодеков до первого "/"
      auto truncateCodec = [](const std::string &s) -> std::string {
        if (s.empty())
          return s;
        size_t pos = s.find('/');
        return (pos == std::string::npos) ? s : s.substr(0, pos);
      };
      wxString streamInfo = wxString::Format(
          "%s | %dx%d @ %d fps | %s / %s", wxString::FromUTF8(m_currentName),
          info.width, info.height, info.fps,
          wxString::FromUTF8(truncateCodec(info.videoCodec)),
          wxString::FromUTF8(truncateCodec(info.audioCodec)));

      if (m_onStreamInfo) {
        wxTheApp->CallAfter(
            [cb = m_onStreamInfo, streamInfo]() { cb(streamInfo); });
      }
    });

    m_playerController->SetProgressCallback([this](const ProgressInfo &info) {
      wxTheApp->CallAfter([this, info]() {
        m_lastProgress = info;
        OnProgressInfo(info);
      });
    });

    m_playerController->SetStateCallback([this](PlayerState st) {
      wxCommandEvent evt(wxEVT_PLAYER_STATE);
      evt.SetInt((int)st);
      wxPostEvent(this, evt);
    });
  }

  Bind(wxEVT_PLAYER_STATE, &VideoPanel::OnPlayerState, this);

  LoadTempPlaylistFromConfig();

  // EOF таймер (fallback для потоков)
  m_eofTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &VideoPanel::OnEofTimer, this, m_eofTimer.GetId());
  m_eofTimer.Start(200); // 5 раз в секунду

  m_hideCursorTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &VideoPanel::OnHideCursorTimer, this,
       m_hideCursorTimer.GetId());

  m_videoArea->Bind(wxEVT_MOTION, [this](wxMouseEvent &evt) {
    if (!m_isFullscreen)
      return;

    // Показать курсор, если он скрыт
    if (!m_cursorVisible) {
      m_videoArea->SetCursor(wxCURSOR_DEFAULT);
      m_cursorVisible = true;
    }

    // Перезапустить таймер автоскрытия
    m_hideCursorTimer.Start(1500, wxTIMER_ONE_SHOT);

    evt.Skip();
  });

  // fullscreen/restore on doubleclick, play/pause single click
  m_clickTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &VideoPanel::OnClickTimer, this, m_clickTimer.GetId());

  m_videoArea->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &evt) {
    wxPoint pos = evt.GetPosition();
    if (!m_videoArea->GetClientRect().Contains(pos))
      return;

    m_waitingSingleClick = true;
    m_clickTimer.Start(180, wxTIMER_ONE_SHOT); // стандартный double-click delay
  });

  m_videoArea->Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent &evt) {
    wxPoint pos = evt.GetPosition();
    if (!m_videoArea->GetClientRect().Contains(pos))
      return;

    m_waitingSingleClick = false; // отменяем одиночный
    ToggleFullscreen();
  });

  m_forceBlackTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &VideoPanel::OnForceBlackTimer, this,
       m_forceBlackTimer.GetId());
}

void VideoPanel::SetErrorStatus(const wxString &errorMsg) {
  wxFrame *frame = dynamic_cast<wxFrame *>(wxGetTopLevelParent(this));
  if (frame && frame->GetStatusBar()) {
    frame->SetStatusText("Error", 0);
    frame->SetStatusText(errorMsg, 1);
  }
}

void VideoPanel::OnForceBlackTimer(wxTimerEvent &) {
  if (!m_forceBlackActive)
    return;

  int64_t w = 0, h = 0;
  bool hasVideo = false;
  if (m_playerController) {
    hasVideo = m_playerController->GetPropertyInt("video-params/w", w) &&
               m_playerController->GetPropertyInt("video-params/h", h) &&
               w > 0 && h > 0;
  }

  static int wait = 0;
  if (hasVideo) {
    if (++wait >= 3) {
      MpvGLCanvas *canvas = dynamic_cast<MpvGLCanvas *>(m_videoArea);
      if (canvas) {
        canvas->SetForceBlack(false);
        canvas->ShowSpinner(false);
        m_forceBlackActive = false;
        m_forceBlackTimer.Stop();
        m_isLoading = false;
        wait = 0;
        UpdateUiButtons();
      }
    }
  } else {
    wait = 0;
  }
}

void VideoPanel::OnClickTimer(wxTimerEvent &) {
  if (!m_waitingSingleClick)
    return;

  m_waitingSingleClick = false;

  // одиночный клик → play/pause
  if (m_playerController) {
    PlayerState st = m_playerController->GetState();
    if (st == PlayerState::Playing)
      Pause();
    else
      Play();
  }
}

void VideoPanel::OnHideCursorTimer(wxTimerEvent &) {
  if (!m_isFullscreen)
    return;

  m_videoArea->SetCursor(wxCURSOR_BLANK);
  m_cursorVisible = false;
}

VideoPanel::~VideoPanel() {
  try {
    m_pendingTempPlay = false;
    m_pendingTempIndex = -1;

    SaveTempPlaylistToConfig();
  } catch (const std::exception &e) {
    LOG_ERROR(wxString::Format("VideoPanel dtor std::exception: %s", e.what()));
  } catch (...) {
    wxMessageBox("VideoPanel dtor unknown exception");
  }
}

wxString VideoPanel::FormatTime(double seconds) {
  if (seconds < 0 || std::isnan(seconds) || std::isinf(seconds))
    return "00:00:00";

  int total_sec = static_cast<int>(std::round(seconds));
  int hours = total_sec / 3600;
  int minutes = (total_sec % 3600) / 60;
  int secs = total_sec % 60;

  return (hours > 0) ? wxString::Format("%d:%02d:%02d", hours, minutes, secs)
                     : wxString::Format("%02d:%02d", minutes, secs);
}

void VideoPanel::OnProgressInfo(const ProgressInfo &info) {
  m_lastProgress = info;

  if (!m_progress || !m_timeCurrentLabel)
    return;

  // Не обновляем, если пользователь тянет слайдер
  if (!m_isDraggingProgress) {
    bool isStream = (info.duration <= 0 || info.duration > 1000000);
    int value = isStream ? static_cast<int>(info.cachePercent * 10)
                         : static_cast<int>(info.percentPos * 10);
    m_progress->SetValue(value);
  }

  UpdateProgressDisplay(info);
}

void VideoPanel::UpdateProgressDisplay(const ProgressInfo &info) {
  m_timeCurrentLabel->SetLabel(FormatTime(info.timePos));
  m_timeDurationLabel->SetLabel("/ " + FormatTime(info.duration));

  double remaining = info.duration - info.timePos;
  if (remaining < 0)
    remaining = 0;
  m_timeRemainingLabel->SetLabel("-" + FormatTime(remaining));
}

void VideoPanel::OnPlayerState(wxCommandEvent &evt) {
  PlayerState st = (PlayerState)evt.GetInt();

  // ---- Обновляем внутреннее состояние всегда ----
  switch (st) {
  case PlayerState::Error: {
    // ---- Обработка ошибки загрузки ----
    m_isLoading = false;
    m_tempState = TempPlayState::Error;
    SetErrorStatus("Failed to load stream");
    MpvGLCanvas *canvas = dynamic_cast<MpvGLCanvas *>(m_videoArea);
    if (canvas) {
      canvas->ShowSpinner(false);
    }
    if (m_forceBlackActive) {
      m_forceBlackTimer.Stop();
      m_forceBlackActive = false;
    }
    break;
  }
  case PlayerState::FileLoaded: {
    m_lastFileLoadedTime = std::chrono::steady_clock::now();
    m_wasPlayingBeforeStop = false;
    m_tempState = TempPlayState::FileLoaded;
    break;
  }
  case PlayerState::Playing: {
    if (m_forceBlackActive) {
      m_forceBlackTimer.Stop();
      m_forceBlackTimer.Start(100, wxTIMER_CONTINUOUS);
    }
    m_wasPlayingBeforeStop = true;
    m_tempState = TempPlayState::Playing;
    if (m_autoPausedByTabSwitch)
      m_autoPausedByTabSwitch = false;
    if (m_pendingTempPlay) {
      m_isTempPlaylistPlaying = true;
      m_tempCurrentIndex = m_pendingTempIndex;
      m_pendingTempPlay = false;
      m_pendingTempIndex = -1;
      if (m_tempCurrentIndex >= 0 && m_tempPlaylistList) {
        m_tempPlaylistList->SetItemState(
            m_tempCurrentIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        m_tempPlaylistList->EnsureVisible(m_tempCurrentIndex);
      }
    }
    m_eofTimer.Start(200);

    // ---- Сброс счётчиков EOF при возобновлении ----
    m_eofFreezeCount = 0;
    m_eofLastPos = -1.0;
    break;
  }
  case PlayerState::Paused: {
    m_tempState = TempPlayState::Paused;

    // ---- Сброс счётчиков при паузе ----
    m_eofFreezeCount = 0;
    m_eofLastPos = -1.0;
    break;
  }
  case PlayerState::Stopped: {
    auto now = std::chrono::steady_clock::now();
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - m_lastFileLoadedTime)
                     .count();
    if (delta < kStoppedDebounceMs)
      break;
    if (!m_wasPlayingBeforeStop)
      break;
    m_tempState = TempPlayState::Stopped;
    m_eofTimer.Stop();
    if (m_isTempPlaylistPlaying) {
      if (!m_pendingTempPlay) {
        PlayNextTempItem();
      } else {
        m_pendingTempPlay = false;
        m_pendingTempIndex = -1;
      }
    }
    break;
  }
  default:
    break;
  }

  // ---- Отправляем статус в статусбар только если загрузка завершена ----
  if (!m_isLoading) {
    wxString stateText;
    switch (st) {
    case PlayerState::FileLoaded:
      stateText = "File loaded";
      break;
    case PlayerState::Playing:
      stateText = "Playing";
      break;
    case PlayerState::Paused:
      stateText = "Paused";
      break;
    case PlayerState::Stopped:
      stateText = "Stopped";
      break;
    case PlayerState::Error:
      stateText = "Error";
      break;
    default:
      stateText = "";
      break;
    }
    if (m_onPlayerState) {
      wxTheApp->CallAfter(
          [cb = m_onPlayerState, stateText]() { cb(stateText); });
    }
  }

  // ---- Обновляем UI кнопок ----
  UpdateUiButtons();
}

void VideoPanel::UpdateUiButtons() {
  if (!m_btnPlay || !m_btnPause || !m_btnStop)
    return;

  // состояние UI теперь зависит от m_tempState
  UiState effectiveState = UiState::Idle;

  switch (m_tempState) {
  case TempPlayState::Idle:
  case TempPlayState::Stopped:
  case TempPlayState::Error:
    effectiveState = UiState::Idle;
    break;

  case TempPlayState::Requesting:
  case TempPlayState::Loading:
  case TempPlayState::FileLoaded:
  case TempPlayState::Starting:
    effectiveState = UiState::Loading;
    break;

  case TempPlayState::Playing:
    effectiveState = UiState::Playing;
    break;

  case TempPlayState::Paused:
    effectiveState = UiState::Paused;
    break;
  }

  switch (effectiveState) {
  case UiState::Idle:
    m_btnPlay->Enable();
    m_btnPause->Disable();
    m_btnStop->Disable();
    break;

  case UiState::Loading:
    m_btnPlay->Disable();
    m_btnPause->Disable();
    m_btnStop->Enable();
    break;

  case UiState::Playing:
    m_btnPlay->Disable();
    m_btnPause->Enable();
    m_btnStop->Enable();
    break;

  case UiState::Paused:
    m_btnPlay->Enable();
    m_btnPause->Disable();
    m_btnStop->Enable();
    break;
  }

  // ---- Обновляем статусбар ----
  wxFrame *frame = dynamic_cast<wxFrame *>(wxGetTopLevelParent(this));
  if (frame && frame->GetStatusBar()) {
    wxString statusText;
    if (m_isLoading) {
      statusText = "Loading...";
    } else {
      switch (effectiveState) {
      case UiState::Idle:
        statusText = "";
        break;
      case UiState::Loading:
        statusText = "Loading...";
        break;
      case UiState::Playing:
        statusText = "Playing";
        break;
      case UiState::Paused:
        statusText = "Paused";
        break;
      }
    }
    frame->SetStatusText(statusText, 0);
    // Если статус пустой (Idle и не загрузка) – очищаем второй слот
    if (effectiveState == UiState::Idle && !m_isLoading) {
      frame->SetStatusText("", 1);
    }
  }
}

void VideoPanel::SetTabActive(bool active) {
  if (!active) {
    if (m_playerController &&
        m_playerController->GetState() == PlayerState::Playing) {
      LOG_DEBUG("VideoPanel::SetTabActive(false): Pause(), set "
                "m_autoPausedByTabSwitch=true");
      m_autoPausedByTabSwitch = true;
      Pause();
    }
    return;
  }

  LOG_DEBUG("VideoPanel::SetTabActive(true) called; m_autoPausedByTabSwitch=%d",
            (int)m_autoPausedByTabSwitch);

  if (!m_autoPausedByTabSwitch)
    return;

  wxTheApp->CallAfter([this]() {
    LOG_DEBUG("VideoPanel::SetTabActive(CallAfter) entered; "
              "m_autoPausedByTabSwitch=%d",
              (int)m_autoPausedByTabSwitch);

    PlayerState st = m_playerController ? m_playerController->GetState()
                                        : PlayerState::Stopped;
    if (st == PlayerState::Stopped) {
      LOG_DEBUG("VideoPanel::SetTabActive(CallAfter): backend stopped, abort");
      return;
    }

    LOG_DEBUG("VideoPanel::SetTabActive(CallAfter): calling Play()");
    Play();

    // локальный таймер и счётчик попыток — без новых полей класса
    wxTimer *resumeTimer = new wxTimer(this);
    int *attempts = new int(0);

    // захватываем только то, что реально нужно; используем литерал 8 вместо
    // захвата константы
    resumeTimer->Bind(wxEVT_TIMER, [this, resumeTimer,
                                    attempts](wxTimerEvent &) {
      ++(*attempts);
      PlayerState s = m_playerController ? m_playerController->GetState()
                                         : PlayerState::Stopped;
      LOG_DEBUG("ResumeTimer attempt=%d state=%d", *attempts, (int)s);

      if (s == PlayerState::Playing) {
        LOG_DEBUG(
            "ResumeTimer: backend Playing — clearing m_autoPausedByTabSwitch");
        m_autoPausedByTabSwitch = false;

        resumeTimer->Stop();
        delete resumeTimer;
        delete attempts;
        return;
      }

      if (*attempts >= 8) {
        LOG_DEBUG("ResumeTimer: giving up after %d attempts — clearing "
                  "m_autoPausedByTabSwitch",
                  *attempts);
        m_autoPausedByTabSwitch = false;

        resumeTimer->Stop();
        delete resumeTimer;
        delete attempts;
        return;
      }
    });

    resumeTimer->Start(100, wxTIMER_CONTINUOUS);
  });
}

bool VideoPanel::IsUiLoading() const {
  switch (m_tempState) {
  case TempPlayState::Requesting:
  case TempPlayState::Loading:
  case TempPlayState::FileLoaded:
  case TempPlayState::Starting:
    return true;
  default:
    return false;
  }
}
bool VideoPanel::IsUiPlaying() const {
  return m_tempState == TempPlayState::Playing;
}
