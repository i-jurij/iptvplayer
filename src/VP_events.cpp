#include "LogControl.h"
#include "MainFrame.h"
#include "Utils_Platform.h"
#include "VideoPanel.h"

#include <wx/dir.h>
#include <wx/menu.h>
#include <wx/snglinst.h>
#include <wx/filename.h>


// ============================================================================
// Events
// ============================================================================

void VideoPanel::OnPlay(wxCommandEvent &) {
  // Игнорируем нажатие Play, если UI в Loading (ожидание подтверждения backend)
  if (IsUiLoading()) {
    LOG_DEBUG("OnPlay: ignored because UI is Loading");
    return;
  }

  // Если сейчас Paused — просто возобновляем
  if (m_playerController &&
      m_playerController->GetState() == PlayerState::Paused) {
    Play();
    return;
  }

  // Если есть временный плейлист — играем выделенный трек
  if (!m_tempPlaylist.IsEmpty()) {
    long sel = m_tempPlaylistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                               wxLIST_STATE_SELECTED);
    if (sel != wxNOT_FOUND) {
      TempPlaylistPlay();
      return;
    }

    // Если ничего не выделено — выделяем первый и играем
    m_tempPlaylistList->SetItemState(0, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
    TempPlaylistPlay();
    return;
  }

  // Иначе — НИЧЕГО НЕ ДЕЛАЕМ, статус НЕ меняем
  LOG_WARN("Play pressed, but nothing to play");
}

void VideoPanel::OnPause(wxCommandEvent &) {
  if (IsUiLoading()) {
    LOG_DEBUG("OnPause: ignored because UI is Loading");
    return;
  }
  Pause();
}

void VideoPanel::OnStop(wxCommandEvent &) {
  Stop();
}

void VideoPanel::OnMute(wxCommandEvent &) { ToggleMute(); }

void VideoPanel::OnVolume(wxCommandEvent &) {
  SetVolume(m_volumeSlider->GetValue());
}

void VideoPanel::OnFullscreen(wxCommandEvent &) { ToggleFullscreen(); }

void VideoPanel::OnFrameKey(wxKeyEvent &evt) {
  if (!this->IsShownOnScreen()) {
    evt.Skip();
    return;
  }

  //LOG_DEBUG("OnFrameKey: key=%d", evt.GetKeyCode());

  OnKey(evt);
  evt.StopPropagation();
}

void VideoPanel::OnKey(wxKeyEvent &evt) {
  //LOG_DEBUG("OnKey: key=%d", evt.GetKeyCode());

  wxWindow *focused = wxWindow::FindFocus();
  int code = evt.GetKeyCode();
  int mods = evt.GetModifiers(); // для Ctrl/Shift

  // ------------------------------------------------------------
  // 1) ФОКУС ВО ВРЕМЕННОМ ПЛЕЙЛИСТЕ → VideoPanel НЕ трогает клавиши
  // ------------------------------------------------------------
  if (m_tempPlaylistPanel && m_tempPlaylistPanel->IsDescendant(focused)) {
    evt.Skip();
    return;
  }

  // ------------------------------------------------------------
  // 2) ФОКУС В КОНТРОЛАХ → ↑↓ = громкость
  // ------------------------------------------------------------
  if (m_controlsPanel && m_controlsPanel->IsDescendant(focused)) {

    if (code == WXK_UP) {
      int v = std::clamp(m_volumeSlider->GetValue() + 5, 0, 100);
      m_volumeSlider->SetValue(v);
      if (m_playerController)
        m_playerController->SetVolume(v);

      // evt.Skip(false); // вариант 1
      evt.StopPropagation(); // вариант 2 (надёжнее)
      return;
    }

    if (code == WXK_DOWN) {
      int v = std::clamp(m_volumeSlider->GetValue() - 5, 0, 100);
      m_volumeSlider->SetValue(v);
      if (m_playerController)
        m_playerController->SetVolume(v);

      // evt.Skip(false);
      evt.StopPropagation();
      return;
    }

    // остальные клавиши → по общей логике ниже
  }

  // ------------------------------------------------------------
  // 3) ОБЩИЕ ХОТКЕИ ВИДЕО (GLCanvas, прогресс, кнопки и т.д.)
  // ------------------------------------------------------------
  switch (code) {
    // ======= Play / Pause =======
  case WXK_SPACE:
    if (m_playerController &&
        m_playerController->GetState() == PlayerState::Playing) {
      Pause();
    } else {
      Play();
    }
    evt.StopPropagation();
    return;

  // ======= Перемотка =======
  case WXK_LEFT:
    if (m_playerController) {
      if (mods & wxMOD_SHIFT)
        m_playerController->SeekRelative(-30);
      else if (mods & wxMOD_CONTROL)
        m_playerController->SeekRelative(-1);
      else
        m_playerController->SeekRelative(-5);
    }
    evt.Skip(false);
    return;

  case WXK_RIGHT:
    if (m_playerController) {
      if (mods & wxMOD_SHIFT)
        m_playerController->SeekRelative(+30);
      else if (mods & wxMOD_CONTROL)
        m_playerController->SeekRelative(+1);
      else
        m_playerController->SeekRelative(+5);
    }
    evt.Skip(false);
    return;

  case WXK_HOME:
    if (m_playerController) {
      m_playerController->SeekAbsolute(0); // 0%
    }
    evt.Skip(false);
    return;

  case WXK_END:
    if (m_playerController) {
      m_playerController->SeekAbsolute(100); // 100%
    }
    evt.Skip(false);
    return;

  case 'M':
  case 'm':
    if (m_playerController) {
      m_btnMute->SetValue(!m_btnMute->GetValue());
      ToggleMute();
    }
    evt.StopPropagation();
    return;

  // ======= Громкость =======
  case WXK_UP:
    if (m_playerController) {
      int step = (mods & wxMOD_CONTROL) ? 1 : 5;
      int newVol = std::clamp(m_lastVolume + step, 0, 100);
      m_volumeSlider->SetValue(newVol);
      SetVolume(newVol);
    }
    evt.Skip(false);
    return;

  case WXK_DOWN:
    if (m_playerController) {
      int step = (mods & wxMOD_CONTROL) ? 1 : 5;
      int newVol = std::clamp(m_lastVolume - step, 0, 100);
      m_volumeSlider->SetValue(newVol);
      SetVolume(newVol);
    }
    evt.Skip(false);
    return;

  // ======= Скорость =======
  case '[':
    if (m_playerController) {
      m_playerController->AdjustSpeed(-0.1);
    }
    evt.Skip(false);
    return;

  case ']':
    if (m_playerController) {
      m_playerController->AdjustSpeed(+0.1);
    }
    evt.Skip(false);
    return;

  case '{':
    if (m_playerController) {
      m_playerController->AdjustSpeed(-0.5);
    }
    evt.Skip(false);
    return;

  case '}':
    if (m_playerController) {
      m_playerController->AdjustSpeed(+0.5);
    }
    evt.Skip(false);
    return;

  case WXK_BACK: // Backspace — сброс скорости
    if (m_playerController) {
      m_playerController->ResetSpeed();
    }
    evt.Skip(false);
    return;

  // ======= Аудиодорожки =======
  case '+': // next audio
    if (m_playerController) {
      m_playerController->NextAudioTrack();
    }
    evt.Skip(false);
    return;

  case '_': // prev audio (Shift + '-')
    if (m_playerController) {
      m_playerController->PrevAudioTrack();
    }
    evt.Skip(false);
    return;

  // ======= Субтитры =======
  case 'v':
  case 'V':
    if (m_playerController) {
      m_playerController->ToggleSubtitles();
    }
    evt.Skip(false);
    return;

  case 'j':
  case 'J':
    if (m_playerController) {
      m_playerController->NextSubtitleTrack();
    }
    evt.Skip(false);
    return;

  case 'h':
  case 'H':
    if (m_playerController) {
      m_playerController->PrevSubtitleTrack();
    }
    evt.Skip(false);
    return;

  case 'q':
  case 'Q':
    Stop();
    evt.Skip(false);
    return;

  default:
    break;
  }

  evt.Skip();
}

void VideoPanel::OnOpen(wxCommandEvent &) {
  // Пересоздаём меню каждый раз
  if (m_openMenu) {
    delete m_openMenu;
    m_openMenu = nullptr;
  }
  m_openMenu = new wxMenu;

  // Основные действия
  int idOpenFile = wxWindow::NewControlId();
  int idOpenFolder = wxWindow::NewControlId();
  int idOpenUrl = wxWindow::NewControlId();

  m_openMenu->Append(idOpenFile, _U("Open File…"));
  m_openMenu->Append(idOpenFolder, _U("Open Folder…"));
  m_openMenu->Append(idOpenUrl, _U("Open URL…"));

  m_openMenu->AppendSeparator();

  // MRU (Recent)
  if (m_recentFiles.size() > 0) {
    wxMenu *recentMenu = new wxMenu;
    for (size_t i = 0; i < m_recentFiles.size(); ++i) {
      int id = wxWindow::NewControlId();
      recentMenu->Append(id, m_recentFiles[i]);
      Bind(wxEVT_MENU, [this, i](wxCommandEvent &) { OpenRecent(i); }, id);
    }
    m_openMenu->AppendSubMenu(recentMenu, "Recent");
  }

  // Bind для основных пунктов
  Bind(wxEVT_MENU, [this](wxCommandEvent &) { OpenFile(); }, idOpenFile);
  Bind(wxEVT_MENU, [this](wxCommandEvent &) { OpenCatalog(); }, idOpenFolder);
  Bind(wxEVT_MENU, [this](wxCommandEvent &) { OpenUrl(); }, idOpenUrl);

  // Позиция кнопки в экранных координатах
  wxPoint screenPos = m_btnOpen->ClientToScreen(wxPoint(0, 0));
  // Позиция VideoPanel в экранных координатах
  wxPoint panelScreenPos = this->ClientToScreen(wxPoint(0, 0));
  // Позиция кнопки в координатах VideoPanel
  wxPoint localPos = screenPos - panelScreenPos;

  // Оцениваем высоту меню
  int itemCount = (int)m_openMenu->GetMenuItemCount();
  int lineH = GetCharHeight(); // высота строки текста
  int padding = FromDIP(16);   // верх/низ + внутренние отступы
  int menuHeight = itemCount * lineH + padding;

  // Отступ между кнопкой и меню
  int gap = FromDIP(4);

  // Координата верхнего левого угла меню:
  // верх кнопки - высота меню - отступ
  localPos.y -= (menuHeight + gap);

  // Не даём меню уйти выше панели
  if (localPos.y < 0)
    localPos.y = 0;

  // Откладываем показ меню до следующей итерации event loop: на GTK
  // вызов PopupMenu прямо внутри обработчика кнопки может привести к
  // мгновенному закрытию (событие release кнопки ещё в очереди).
  wxMenu *menuPtr = m_openMenu;
  wxPoint popupPos = localPos;
  CallAfter([this, menuPtr, popupPos]() {
    if (menuPtr)
      PopupMenu(menuPtr, popupPos);
  });
}

void VideoPanel::OnWindowCreated(wxShowEvent &event) {
  if (!event.IsShown() || m_isAttached)
    return;

  if (!m_playerController || !m_playerController->GetBackend())
    return;

  if (m_playerController->AttachToWindow(m_videoArea)) {
    m_isAttached = true;
    LOG_DEBUG("VideoPanel: backend attached successfully");
  } else {
    LOG_ERROR("VideoPanel: failed to attach backend");
  }
}

void VideoPanel::OnVideoAreaResize(wxSizeEvent &event) {
  event.Skip();

  if (!m_playerController)
    return;

  int w, h;
  m_videoArea->GetClientSize(&w, &h);

  m_playerController->ResizeEmbeddedWindow(w, h);
}

void VideoPanel::OnEofTimer(wxTimerEvent &) {
  if (!m_playerController || !m_progress)
    return;

  const ProgressInfo &info = m_lastProgress;

  // ---- Защита от невалидного времени ----
  if (std::isnan(info.timePos) || info.timePos < 0) {
    return;
  }

  // === Pending seek ===
  if (m_pendingSeekPercent >= 0) {
    int backendPercent = static_cast<int>(std::round(info.percentPos));
    wxLongLong now = wxGetLocalTimeMillis();
    long elapsedMs = (now - m_seekRequestTime).GetLo();

    if (std::abs(backendPercent - m_pendingSeekPercent) <= 1) {
      m_pendingSeekPercent = -1;
      m_progress->SetValue(backendPercent * 10);
    } else if (elapsedMs > kSeekConfirmTimeoutMs) {
      m_playerController->SeekAbsolute(m_pendingSeekPercent);
      m_seekRequestTime = now;
    }

    if (m_pendingSeekPercent >= 0)
      return;
  }

  // === Progress update ===
  if (!m_progress->IsDragging() && m_pendingSeekPercent < 0) {
    if (m_playerController->GetState() == PlayerState::Playing) {
      m_isLiveStream = (info.duration <= 0 || info.duration > 1000000);

      int value = m_isLiveStream ? static_cast<int>(info.cachePercent * 10)
                                 : static_cast<int>(info.percentPos * 10);

      m_progress->SetValue(value);
      UpdateProgressDisplay(info);
    }
  }

  // === Temporary playlist fallback ===
  if (!m_isTempPlaylistPlaying)
    return;
  if (m_tempPlaylist.size() < 2)
    return;
  if (m_playerController->GetState() == PlayerState::Stopped)
    return;

  // ---- Если на паузе – сбросить счётчики и выйти ----
  if (m_playerController->GetState() == PlayerState::Paused) {
#ifdef DEBUG
    LOG_DEBUG("OnEofTimer: Paused state detected — resetting EOF counters");
#endif
    m_eofFreezeCount = 0;
    m_eofLastPos = -1.0;
    return;
  }

  if (!m_isLiveStream) {
    if (info.timePos == m_eofLastPos) {
      m_eofFreezeCount++;
      if (m_eofFreezeCount >= 15) {
        m_eofFreezeCount = 0;
        m_tempState = TempPlayState::Stopped;
        PlayNextTempItem();
      }
    } else {
      m_eofFreezeCount = 0;
    }
    m_eofLastPos = info.timePos;
  }
}

void VideoPanel::OnRecord(wxCommandEvent &) {
  if (!m_playerController)
    return;

  if (m_isRecording) {
    m_playerController->StopRecording();
    return;
  }

  PlayerState state = m_playerController->GetState();
  if (state != PlayerState::Playing && state != PlayerState::Paused) {
    wxMessageBox(_U("Recording is only available during playback"), _U("Error"),
                 wxOK | wxICON_WARNING);
    return;
  }

  if (!InitializeRecordDirectory()) {
    wxMessageBox(_U("Could not initialize record directory"), _U("Error"),
                 wxOK | wxICON_ERROR);
    return;
  }

  wxString filename = GenerateRecordFilename();
  if (filename.IsEmpty()) {
    wxMessageBox(_U("Could not generate filename"), _U("Error"),
                 wxOK | wxICON_ERROR);
    return;
  }

  wxString fullPath = m_recordDirectory + "/" + filename;
  m_playerController->StartRecording(fullPath.ToUTF8().data());
}

void VideoPanel::OnRecordStateChanged(bool isRecording,
                                      const std::string &filename,
                                      const std::string &error) {
  m_isRecording = isRecording;
  UpdateRecordButtonState();

  wxFrame *frame = dynamic_cast<wxFrame *>(wxGetTopLevelParent(this));
  if (!frame || !frame->GetStatusBar())
    return;

  if (!error.empty()) {
    m_recordStatusTimer.Stop();
    frame->SetStatusText(
        wxString::Format(_U("Record error: %s"), wxString::FromUTF8(error)), 1);
    return;
  }

  if (isRecording) {
    m_recordStartTime = wxGetLocalTimeMillis();
    m_recordStatusTimer.Start(1000, wxTIMER_CONTINUOUS);
    wxString fname = wxString::FromUTF8(filename);
    wxFileName fn(fname);
    frame->GetStatusBar()->SetStatusText(
        wxString::Format(wxString::FromUTF8("🔴 ") +
                             _U("Recording: %s [00:00:00]"),
                         fn.GetFullName()),
        1);
  } else {
    m_recordStatusTimer.Stop();
    if (!m_lastStreamInfo.IsEmpty()) {
      frame->GetStatusBar()->SetStatusText(m_lastStreamInfo, 1);
    } else {
      frame->GetStatusBar()->SetStatusText("", 1);
    }
  }
}

void VideoPanel::OnRecordStatusTimer(wxTimerEvent &) {
  if (!m_isRecording) {
    m_recordStatusTimer.Stop();
    return;
  }

  wxFrame *frame = dynamic_cast<wxFrame *>(wxGetTopLevelParent(this));
  if (!frame || !frame->GetStatusBar())
    return;

  long elapsedMs = (wxGetLocalTimeMillis() - m_recordStartTime).GetLo();
  int seconds = elapsedMs / 1000;
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs = seconds % 60;
  wxString duration = wxString::Format("%02d:%02d:%02d", hours, minutes, secs);

  wxString current = frame->GetStatusBar()->GetStatusText(1);
  // Совпадает с форматом, выставленным в OnRecordStateChanged:
  // "🔴 " + _U("Recording: %s [00:00:00]")
  const wxString prefix = wxString::FromUTF8("🔴 ") + _U("Recording: ");
  if (current.StartsWith(prefix)) {
    int pos = current.find(prefix);
    if (pos != wxNOT_FOUND) {
      wxString filenamePart = current.Mid(pos + prefix.Length());
      int bracketPos = filenamePart.find('[');
      if (bracketPos != wxNOT_FOUND) {
        filenamePart = filenamePart.Mid(0, bracketPos).Trim();
      }
      frame->GetStatusBar()->SetStatusText(
          prefix + filenamePart + wxString::Format(" [%s]", duration), 1);
    }
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
  SetControlsVisible(false, false);
}

void VideoPanel::OnProgressInfo(const ProgressInfo &info) {
  m_lastProgress = info;

  if (!m_currentMediaKey.empty() && m_mediaPosCallback) {
    m_mediaPosCallback(m_currentMediaKey, info.timePos, info.duration, false);
  }

  if (!m_progress || !m_timeCurrentLabel)
    return;

  if (!m_isDraggingProgress) {
    bool isStream = (info.duration <= 0 || info.duration > 1000000);
    int value = isStream ? static_cast<int>(info.cachePercent * 10)
                         : static_cast<int>(info.percentPos * 10);
    m_progress->SetValue(value);
  }

  UpdateProgressDisplay(info);

  if (!IsShownOnScreen())
    return;

  wxFrame *frame = dynamic_cast<wxFrame *>(wxGetTopLevelParent(this));
  if (!frame || !frame->GetStatusBar())
    return;

  if (info.pausedForCache && !m_isLoading) {
    if (!m_bufferingStatusShown) {
      m_bufferingStatusShown = true;
      frame->SetStatusText(_U("Buffering..."), 0);
    }
  } else if (m_bufferingStatusShown) {
    m_bufferingStatusShown = false;
    wxString statusText;
    switch (m_tempState) {
    case TempPlayState::Idle:
    case TempPlayState::Stopped:
    case TempPlayState::Error:
      statusText = "";
      break;
    case TempPlayState::Requesting:
    case TempPlayState::Loading:
    case TempPlayState::FileLoaded:
    case TempPlayState::Starting:
      statusText = _U("Loading...");
      break;
    case TempPlayState::Playing:
      statusText = _U("Playing");
      break;
    case TempPlayState::Paused:
      statusText = _U("Paused");
      break;
    }
    frame->SetStatusText(statusText, 0);
  }
}


void VideoPanel::OnPlayerState(wxCommandEvent &evt) {
  PlayerState st = (PlayerState)evt.GetInt();

  if ((st == PlayerState::Paused || st == PlayerState::Stopped ||
       st == PlayerState::Error) &&
      !m_currentMediaKey.empty() && m_mediaPosCallback) {
    m_mediaPosCallback(m_currentMediaKey, m_lastProgress.timePos,
                       m_lastProgress.duration, true);
  }

  // ---- Обновляем внутреннее состояние всегда ----
  switch (st) {
  case PlayerState::Error: {
    // ---- Обработка ошибки загрузки ----
    m_isLoading = false;
    m_tempState = TempPlayState::Error;
    SetErrorStatus(_U("Failed to load stream"));
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
    m_autoPausedByTabSwitch = false;
    m_eofTimer.Stop();
    if (m_isTempPlaylistPlaying) {
      if (!m_pendingTempPlay) {
        PlayNextTempItem();
      } else {
        m_pendingTempPlay = false;
        m_pendingTempIndex = -1;
      }
    } else if (m_stoppedByUser) {
      // Ручной Stop — возврат на вкладку-источник
      if (m_playbackSource) {
        MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this));
        if (mf && mf->getNotebook()) {
          int srcIdx = mf->getNotebook()->FindPage(m_playbackSource);
          if (srcIdx != mf->getNotebook()->GetSelection()) {
            m_onRequestTabSwitch(srcIdx);
          }
        }
      }
    } else if (m_onPlaybackEnded) {
      // Авто-след. эпизод (сериалы) или ничего
      m_onPlaybackEnded();
    } else if (m_playbackSource) {
      // EOF без слушателей — возврат на вкладку-источник
      MainFrame *mf = dynamic_cast<MainFrame *>(wxGetTopLevelParent(this));
      if (mf && mf->getNotebook()) {
        int srcIdx = mf->getNotebook()->FindPage(m_playbackSource);
        if (srcIdx != mf->getNotebook()->GetSelection()) {
          m_onRequestTabSwitch(srcIdx);
        }
      }
    }
    m_stoppedByUser = false;
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
      stateText = _U("File loaded");
      break;
    case PlayerState::Playing:
      stateText = _U("Playing");
      break;
    case PlayerState::Paused:
      stateText = _U("Paused");
      break;
    case PlayerState::Stopped:
      stateText = _U("Stopped");
      break;
    case PlayerState::Error:
      stateText = _U("Error");
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

  if (m_isRecording &&
      (st == PlayerState::Stopped || st == PlayerState::Error)) {
    if (m_playerController) {
      m_playerController->StopRecording();
    }
  }

  // ---- Обновляем UI кнопок ----
  UpdateUiButtons();
}
