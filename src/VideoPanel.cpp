#include "VideoPanel.h"
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

VideoPanel::VideoPanel(wxWindow *parent, PlayerController *player)
    : wxPanel(parent, wxID_ANY) {
  m_playerController = player;

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

  // Video frame
  m_videoArea = new wxWindow(m_mainPanel, wxID_ANY);
  m_videoArea->SetBackgroundColour(*wxBLACK);
  m_videoArea->SetBackgroundStyle(wxBG_STYLE_PAINT);
  /*
m_videoArea->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &) {
  if (m_focusManager)
    m_focusManager->EnsureFocus();
});
*/
  m_videoArea->Bind(wxEVT_SIZE, &VideoPanel::OnVideoAreaResize, this);
  m_videoArea->Bind(wxEVT_PAINT, &VideoPanel::OnVideoAreaPaint, this);
  m_videoArea->Bind(wxEVT_KEY_DOWN, &VideoPanel::OnKey, this);

  m_videoArea->SetFocus();

  mainSizer->Add(m_videoArea, 1, wxEXPAND);

  // Progress bar
  auto *progressPanel = new wxPanel(m_mainPanel, wxID_ANY);
  auto *progressSizer = new wxBoxSizer(wxVERTICAL);

  auto *progressTimeSizer = new wxBoxSizer(wxHORIZONTAL);

  m_timeCurrentLabel = new wxStaticText(progressPanel, wxID_ANY, "00:00:00");
  m_timeCurrentLabel->SetFont(
      m_timeCurrentLabel->GetFont().MakeSmaller().MakeSmaller());
  progressTimeSizer->Add(m_timeCurrentLabel, 0,
                         wxALIGN_CENTER_VERTICAL | wxLEFT, 5);

  m_progress = new wxSlider(progressPanel, wxID_ANY, 0, 0, 1000,
                            wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
  m_progress->Bind(wxEVT_LEFT_DOWN,
                   [this](wxMouseEvent &) { m_isDraggingProgress = true; });
  m_progress->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
    m_isDraggingProgress = false;
    if (m_playerController) {
      int percent = m_progress->GetValue() / 10;
      m_playerController->SeekAbsolute(percent);
    }
  });
  progressTimeSizer->Add(m_progress, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

  m_timeRemainingLabel = new wxStaticText(progressPanel, wxID_ANY, "-00:00:00");
  m_timeRemainingLabel->SetFont(
      m_timeRemainingLabel->GetFont().MakeSmaller().MakeSmaller());
  progressTimeSizer->Add(m_timeRemainingLabel, 0,
                         wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

  m_timeDurationLabel = new wxStaticText(progressPanel, wxID_ANY, "/ 00:00:00");
  m_timeDurationLabel->SetFont(
      m_timeDurationLabel->GetFont().MakeSmaller().MakeSmaller());
  progressTimeSizer->Add(m_timeDurationLabel, 0,
                         wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

  progressSizer->Add(progressTimeSizer, 0, wxEXPAND);

  // Cache indicator
  auto *cacheSizer = new wxBoxSizer(wxHORIZONTAL);

  m_cacheGauge = new wxGauge(progressPanel, wxID_ANY, 100, wxDefaultPosition,
                             wxSize(150, 16), wxGA_HORIZONTAL | wxGA_SMOOTH);
  m_cacheGauge->SetValue(0);
  cacheSizer->Add(m_cacheGauge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT,
                  5);

  progressSizer->Add(cacheSizer, 0, wxEXPAND | wxTOP, 3);
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
  m_focusManager = new FocusManager(m_videoArea, this);
  /*
    auto preventFocus = [this](wxWindow *w) {
      w->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &) {
        if (m_focusManager)
          m_focusManager->EnsureFocus();
      });
    };
  */
  auto preventFocus = [this](wxWindow *w) {
    w->Bind(wxEVT_SET_FOCUS, [this, w](wxFocusEvent &e) {
      // Если фокус уже в плейлисте или пользователь явно кликнул в плейлист —
      // не мешаем
      if (m_tempPlaylistList && m_tempPlaylistList->HasFocus()) {
        e.Skip();
        return;
      }

      // Если фокус переходит на сам плейлист (например, дочерний элемент) — не
      // мешаем
      wxWindow *focused = wxWindow::FindFocus();
      if (m_tempPlaylistPanel && m_tempPlaylistPanel->IsDescendant(focused)) {
        e.Skip();
        return;
      }

      // Если это слайдер и он захвачен мышью (перетаскивание) — не мешаем
      if (w == m_volumeSlider && m_volumeSlider->HasCapture()) {
        e.Skip();
        return;
      }

      // В остальных случаях — поведение прежнее
      if (m_focusManager)
        m_focusManager->EnsureFocus();

      e.Skip();
    });
  };

  preventFocus(m_btnOpen);
  preventFocus(m_btnPlay);
  preventFocus(m_btnPause);
  preventFocus(m_btnStop);
  preventFocus(m_btnMute);
  preventFocus(m_btnFullscreen);
  preventFocus(m_volumeSlider);
  preventFocus(m_progress);
  preventFocus(m_controlsPanel);

  // Начальное состояние кнопок
  m_btnPlay->Enable();
  m_btnPause->Disable();
  m_btnStop->Disable();

  // Keyboard
  Bind(wxEVT_CHAR_HOOK, &VideoPanel::OnKey, this);
  wxFrame *frame = dynamic_cast<wxFrame *>(wxGetTopLevelParent(this));
  if (frame) {
    frame->Bind(wxEVT_CHAR_HOOK, &VideoPanel::OnFrameKey, this);
  }

  // Drag & drop
  SetDropTarget(new VP_FileDropTarget(this));

  Bind(wxEVT_SHOW, &VideoPanel::OnWindowCreated, this);

  m_playerController->SetStreamInfoCallback([this](const StreamInfo &info) {
    wxString streamInfo = wxString::Format(
        "%s | %dx%d @ %d fps | %s / %s", wxString::FromUTF8(m_currentName),
        info.width, info.height, info.fps, wxString::FromUTF8(info.videoCodec),
        wxString::FromUTF8(info.audioCodec));

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

  Bind(wxEVT_PLAYER_STATE, &VideoPanel::OnPlayerState, this);

  LoadTempPlaylistFromConfig();

  // EOF таймер (fallback для потоков)
  m_eofTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &VideoPanel::OnEofTimer, this, m_eofTimer.GetId());
  m_eofTimer.Start(200); // 5 раз в секунду
}

VideoPanel::~VideoPanel() {
  try {
    if (m_playerController) {
      m_playerController->Detach();
    }

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
  // Сохраняем последний прогресс для таймера автоперехода
  m_lastProgress = info;

  // Отслеживаем смену файла — при новом треке сбрасываем флаг ожидания перехода
  wxString currentName = wxString::FromUTF8(m_currentName);
  if (currentName != m_lastPlayedFile) {
    m_lastPlayedFile = currentName;
    m_waitingForNext = false;
  }

  if (!m_progress || !m_timeCurrentLabel)
    return;

  // Не обновляем, если пользователь тянет слайдер
  if (!m_isDraggingProgress) {
    m_progress->SetValue(
        static_cast<int>(info.percentPos * 10)); // 0-100 -> 0-1000
  }

  UpdateProgressDisplay(info);
  UpdateCacheDisplay(info);
}

void VideoPanel::UpdateProgressDisplay(const ProgressInfo &info) {
  m_timeCurrentLabel->SetLabel(FormatTime(info.timePos));
  m_timeDurationLabel->SetLabel("/ " + FormatTime(info.duration));

  double remaining = info.duration - info.timePos;
  if (remaining < 0)
    remaining = 0;
  m_timeRemainingLabel->SetLabel("-" + FormatTime(remaining));
}

void VideoPanel::UpdateCacheDisplay(const ProgressInfo &info) {
  bool isStream = (info.duration <= 0 || info.duration > 1000000);

  if (isStream && (info.cachePercent > 0 || info.cacheDuration > 0)) {
    m_cacheGauge->Show();
    m_cacheGauge->SetValue(info.cachePercent);
  } else {
    m_cacheGauge->Hide();
  }

  m_cacheGauge->GetParent()->Layout(); // ← здесь
}

void VideoPanel::OnPlayerState(wxCommandEvent &evt) {
  PlayerState st = (PlayerState)evt.GetInt();

  switch (st) {

  // -------------------------
  // FILE_LOADED (mpv сообщил, что файл загружен)
  // -------------------------
  case PlayerState::FileLoaded: {
    // Запоминаем время FILE_LOADED — используем для debounce ложных Stopped
    m_lastFileLoadedTime = std::chrono::steady_clock::now();
    m_wasPlayingBeforeStop = false; // ещё не было Playing
    LOG_DEBUG("OnPlayerState: FileLoaded recorded time");
    break;
  }

  // -------------------------
  // PLAYING
  // -------------------------
  case PlayerState::Playing: {
    m_btnPlay->Disable();
    m_btnPause->Enable();
    m_btnStop->Enable();

    // Подтверждаем, что файл действительно начал играть
    m_wasPlayingBeforeStop = true;

    // Если запуск был инициирован из temp playlist — теперь включаем режим
    // плейлиста
    if (m_pendingTempPlay) {
      m_isChannelPlaying = false;
      m_isTempPlaylistPlaying = true;
      if (m_pendingTempIndex >= 0)
        m_tempCurrentIndex = m_pendingTempIndex;
      m_pendingTempPlay = false;
      m_pendingTempIndex = -1;
      LOG_DEBUG(
          "OnPlayerState: Playing confirmed -> temp playlist mode ON, idx=%d",
          m_tempCurrentIndex);
    }
    break;
  }

  // -------------------------
  // PAUSED
  // -------------------------
  case PlayerState::Paused: {
    m_btnPlay->Enable();
    m_btnPause->Disable();
    m_btnStop->Enable();
    break;
  }

  // -------------------------
  // STOPPED (END_FILE или ручной стоп)
  // -------------------------
  case PlayerState::Stopped: {
    // Обновляем UI
    m_btnPlay->Enable();
    m_btnPause->Disable();
    m_btnStop->Disable();

    // --- Автопереход ---
    // 1) Плейлист не активен → просто стоп
    if (!m_isTempPlaylistPlaying)
      break;

    // Если мы в состоянии pending (запуск из temp playlist ожидает
    // подтверждения Playing),
    // и пришёл Stopped до подтверждения — считаем это неудачным стартом и
    // просто очищаем pending.
    if (m_pendingTempPlay) {
      LOG_DEBUG("OnPlayerState: Stopped received while pending -> clear "
                 "pending and ignore");
      m_pendingTempPlay = false;
      m_pendingTempIndex = -1;
      // Не делаем автопереход
      break;
    }

    // 2) Debounce: игнорируем Stopped, если он пришёл слишком быстро после
    // FILE_LOADED
    {
      auto now = std::chrono::steady_clock::now();
      auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - m_lastFileLoadedTime)
                       .count();
      if (delta >= 0 && delta < kStoppedDebounceMs) {
        LOG_DEBUG("OnPlayerState: Ignoring Stopped (delta %d ms < %d ms)",
                   (int)delta, kStoppedDebounceMs);
        break;
      }
    }

    // 3) Дополнительная проверка: автопереход только если до Stopped был
    // Playing
    if (!m_wasPlayingBeforeStop) {
      LOG_DEBUG("OnPlayerState: Ignoring Stopped because wasPlayingBeforeStop "
                 "== false");
      break;
    }

    // 4) Меньше двух файлов → переход не нужен
    if (m_tempPlaylist.size() < 2)
      break;

    // 5) Переход к следующему треку
    PlayNextTempItem();
    break;
  }

  default:
    break;
  }
}
