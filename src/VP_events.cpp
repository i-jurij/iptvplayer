#include "LogControl.h"
#include "VideoPanel.h"
#include <wx/dir.h>
#include <wx/menu.h>
#include <wx/snglinst.h>

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
  if (IsUiLoading()) {
    LOG_DEBUG("OnStop: ignored because UI is Loading");
    return;
  }
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

  // ======= Fullscreen  =======
  case 'f':
  case 'F':
    ToggleFullscreen();
    // evt.Skip(false);
    evt.StopPropagation();
    return;

  // ======= ESC: выход из fullscreen =======
  case WXK_ESCAPE:
    if (m_isFullscreen) {
      ToggleFullscreen();
      // evt.Skip(false);
      evt.StopPropagation();
      return;
    }
    break;

  case 'M':
  case 'm':
    if (m_playerController) {
      m_playerController->SetMuted(!m_isMuted);
      m_isMuted = !m_isMuted;
      m_btnMute->SetValue(m_isMuted);
    }
    // evt.Skip(false);
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
  int idOpenUrl = wxWindow::NewControlId();

  m_openMenu->Append(idOpenFile, "Open File…");
  m_openMenu->Append(idOpenUrl, "Open URL…");

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

  // Показываем меню
  PopupMenu(m_openMenu, localPos);
}

void VideoPanel::OnWindowCreated(wxShowEvent &event) {
  if (event.IsShown() && !m_isAttached) {
    //LOG_DEBUG("VideoPanel: window shown, attaching backend");

    // Привязываем к m_videoArea (видео-окно), а не к this
    if (m_playerController->AttachToWindow(m_videoArea)) {
      m_isAttached = true;
      //LOG_DEBUG("VideoPanel: backend attached successfully");
    } else {
      //LOG_ERROR("VideoPanel: failed to attach backend");
    }
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
