#include "LogControl.h"
#include "VideoPanel.h"
#include <wx/dir.h>
#include <wx/menu.h>
#include <wx/snglinst.h>

// ============================================================================
// Events
// ============================================================================

void VideoPanel::OnPlay(wxCommandEvent &) {
  // 1) Если уже идёт воспроизведение (Play/Pause логика через пробел и т.п.)
  //    — оставляем текущее поведение: просто "продолжить"
  if (m_playerController &&
      m_playerController->GetState() == PlayerState::Paused) {
    Play();
    return;
  }

  // 2) Если есть временный плейлист — играем выделенный трек
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

  // 3) Иначе — НИЧЕГО НЕ ДЕЛАЕМ, статус НЕ меняем
  wxLogWarning("Play pressed, but nothing to play");
}

void VideoPanel::OnPause(wxCommandEvent &) { Pause(); }

void VideoPanel::OnStop(wxCommandEvent &) { Stop(); }

void VideoPanel::OnMute(wxCommandEvent &) { ToggleMute(); }

void VideoPanel::OnVolume(wxCommandEvent &) {
  SetVolume(m_volumeSlider->GetValue());
}

void VideoPanel::OnFullscreen(wxCommandEvent &) { ToggleFullscreen(); }

void VideoPanel::OnFrameKey(wxKeyEvent &evt) {
  OnKey(evt);
  evt.Skip();
}

void VideoPanel::OnKey(wxKeyEvent &evt) {
    // Если фокус внутри панели плейлиста — не перехватываем клавиши
    wxWindow *focused = wxWindow::FindFocus();
    if (m_tempPlaylistList &&
        (focused == m_tempPlaylistList ||
         (m_tempPlaylistPanel && m_tempPlaylistPanel->IsDescendant(focused)))) {
      evt.Skip();
      return;
    }

    int code = evt.GetKeyCode();
    int mods = evt.GetModifiers(); // для Ctrl/Shift

    switch (code) {
    // ======= Play / Pause (как было) =======
    case WXK_SPACE:
      if (m_playerController &&
          m_playerController->GetState() == PlayerState::Playing) {
        Pause();
      } else {
        Play();
      }
      evt.Skip(false);
      return;

    // ======= Mute (как было) =======
    case 'm':
    case 'M':
      if (m_playerController) {
        m_playerController->SetMuted(!m_isMuted);
        m_isMuted = !m_isMuted;
        m_btnMute->SetValue(m_isMuted);
      }
      evt.Skip(false);
      return;

    // ======= Fullscreen (как было) =======
    case 'f':
    case 'F':
      ToggleFullscreen();
      evt.Skip(false);
      return;

    // ======= ESC: выход из fullscreen =======
    case WXK_ESCAPE:
      if (m_isFullscreen) {
        ToggleFullscreen();
        evt.Skip(false);
        return;
      }
      break;

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
    LOG_DEBUG("VideoPanel: window shown, attaching backend");

    // Привязываем к m_videoArea (видео-окно), а не к this
    if (m_playerController->AttachToWindow(m_videoArea)) {
      m_isAttached = true;
      LOG_DEBUG("VideoPanel: backend attached successfully");
    } else {
      LOG_ERROR("VideoPanel: failed to attach backend");
    }
  }
}

void VideoPanel::OnVideoAreaPaint(wxPaintEvent &event) {
  wxPaintDC dc(m_videoArea);
  dc.SetBrush(wxBrush(*wxBLACK));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRectangle(m_videoArea->GetClientRect());
  event.Skip(); // Пусть mpv/внешний плеер рисуют поверх
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
  // 1) Плейлист не активен
  if (!m_isTempPlaylistPlaying)
    return;

  // 2) Во временном плейлисте меньше двух файлов → переход не нужен
  if (m_tempPlaylist.size() < 2)
    return;

  const ProgressInfo &info = m_lastProgress;

  // 3) Поток без duration (IPTV, радио)
  if (info.duration <= 0) {
    // Если time-pos не меняется 3 секунды → поток "кончился"
    static double lastPos = -1;
    static int stillCount = 0;

    if (info.timePos == lastPos) {
      stillCount++;
      if (stillCount >= 15) { // 15 * 200ms = 3 секунды
        stillCount = 0;
        PlayNextTempItem();
      }
    } else {
      stillCount = 0;
    }

    lastPos = info.timePos;
    return;
  }

  // 4) Локальный файл — проверяем оставшееся время
  double remaining = info.duration - info.timePos;

  if (remaining < 0.3 && !m_waitingForNext) {
    m_waitingForNext = true;
    PlayNextTempItem();
    return;
  }

  // 5) Новый трек → сброс флага
  if (info.timePos < 0.5) {
    m_waitingForNext = false;
  }
}
