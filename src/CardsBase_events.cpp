#include "Application.h"
#include "CardsBase.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Profiler.h"
#include "Utils.h"
#include "epg/EPGData.h"
#include "epg/EPGManager.h"

#include <wx/clipbrd.h>
#include <wx/tooltip.h>

int CardsBase::HitTestIndex(const wxPoint &pos, bool &fav,
                            wxRect *outRect) const {
  fav = false;

  if (m_cols <= 0 || m_rowH <= 0 || m_colW <= 0)
    return -1;

  int vx, vy;
  CalcUnscrolledPosition(pos.x, pos.y, &vx, &vy);

  int localVx = vx - m_gridOffsetX;

  if (localVx < 0)
    return -1;
  if (localVx >= m_cols * m_colW)
    return -1;

  const int col = localVx / m_colW;
  const int row = vy / m_rowH;

  if (col < 0 || row < 0)
    return -1;

  const size_t index = (size_t)row * (size_t)m_cols + (size_t)col;
  if (index >= m_channels.size())
    return -1;

  const int localX = localVx - col * m_colW;
  const int localY = vy - row * m_rowH;

  if (localX < 0 || localX >= m_cardW || localY < 0 || localY >= m_cardH)
    return -1;

  const int cardX = m_gridOffsetX + col * m_colW;
  const int cardY = row * m_rowH;

  if (outRect)
    *outRect = wxRect(cardX, cardY, m_cardW, m_cardH);

  wxRect starRect = GetStarRect(col, row);
  fav = starRect.Contains(vx, vy);

  return (int)index;
}

void CardsBase::OnMouseEnter(wxMouseEvent &evt) {
  m_mouseInside = true;
  m_lastMouseClientPos = ScreenToClient(wxGetMousePosition());
  evt.Skip();
}

void CardsBase::OnMouseMove(wxMouseEvent &evt) {
  wxPoint pos = evt.GetPosition();
  m_lastMouseClientPos = pos;

  bool fav = false;
  int idx = HitTestIndex(pos, fav, nullptr);

  UpdateTooltip(idx);
  UpdateHoverAtPoint(pos);

  evt.Skip();
}

void CardsBase::OnMouseLeave(wxMouseEvent &evt) {
  m_mouseInside = false;
  m_lastTooltipIndex = -1;
  UnsetToolTip();

  if (m_hoverIndex >= 0) {
    int old = m_hoverIndex;
    m_hoverIndex = -1;
    InvalidateCardClientRectByIndex(old);
  }

  evt.Skip();
}

void CardsBase::OnMouseDown(wxMouseEvent &evt) {
  bool fav = false;
  wxRect rect;
  int idx = HitTestIndex(evt.GetPosition(), fav, &rect);
  if (idx < 0)
    return;
  OnCardClick((size_t)idx, fav, rect);
  evt.Skip();
}

void CardsBase::OnResize(wxSizeEvent &evt) {
  evt.Skip();
  ClearAllCaches(false, false);
  if (IsShown()) {
    UpdateLayout();
    InitLRULimits();
  } else {
    int winId = this->GetId();
    CallAfterSafeById(winId, [](wxWindow *w) {
      auto *self = dynamic_cast<CardsBase *>(w);
      if (!self)
        return;

      if (self->IsShown()) {
        self->UpdateLayout();
        self->InitLRULimits();
      }
    });
  }
}

void CardsBase::OnDestroy(wxWindowDestroyEvent &evt) {
  m_closing = true;
  evt.Skip();
}

void CardsBase::OnScrollStop(wxTimerEvent &) {
  if (m_closing)
    return;

  wxLongLong now = wxGetUTCTimeMillis();
  if (now < m_skipWarmupUntil)
    return;

  WarmUpTiles();
}

void CardsBase::OnScroll(wxScrollWinEvent &evt) {
  int oldY = m_lastScrollY;

  int sx, sy;
  GetViewStart(&sx, &sy);

  int px, py;
  GetScrollPixelsPerUnit(&px, &py);
  if (py <= 0)
    py = 1;

  int newY = sy * py;
  int deltaY = newY - oldY;

  wxLongLong now = wxGetUTCTimeMillis();
  int dt = (int)(now - m_lastScrollEvent).ToLong();
  m_lastScrollEvent = now;

  // направление
  if (deltaY > 0)
    m_scrollDirection = +1;
  else if (deltaY < 0)
    m_scrollDirection = -1;
  else
    m_scrollDirection = 0;

  // быстрый скролл → пропустить warmup
  if (std::abs(deltaY) > 2 * m_rowH && dt < 50) {
    m_skipWarmupUntil = now + 80; // короткая пауза
  }

  // всегда перезапускаем debounce‑таймер
  m_scrollStopTimer.StartOnce(80);

  m_lastScrollY = newY;

  // если очень быстрый скролл — сбросить очередь
  if (m_rowH > 0 && std::abs(deltaY) > 15 * m_rowH) {
    m_logoQueuePQ.clear();
    if (m_logoTimer.IsRunning())
      m_logoTimer.Stop();
  }

  evt.Skip();
}

void CardsBase::OnMouseWheel(wxMouseEvent &evt) {
  int rot = evt.GetWheelRotation();
  wxLongLong now = wxGetUTCTimeMillis();
  int dt = (int)(now - m_lastScrollEvent).ToLong();
  m_lastScrollEvent = now;

  if (rot < 0)
    m_scrollDirection = +1;
  else if (rot > 0)
    m_scrollDirection = -1;

  // быстрый скролл → пропустить warmup
  if (std::abs(rot) > 120 && dt < 50) {
    m_skipWarmupUntil = now + 80;
  }

  // debounce‑таймер
  m_scrollStopTimer.StartOnce(80);

  int winId = this->GetId();
  CallAfterSafeById(winId, [](wxWindow *w) {
    auto *self = dynamic_cast<CardsBase *>(w);
    if (!self)
      return;
    if (self->m_mouseInside) {
      self->UpdateHoverAtPoint(self->m_lastMouseClientPos);
    }
  });

  evt.Skip();
}

void CardsBase::UpdateTooltip(int index) {
  if (index == m_lastTooltipIndex)
    return;

  if (index < 0 || index >= static_cast<int>(m_channels.size())) {
    UnsetToolTip();
    m_lastTooltipIndex = index;
    return;
  }

  UnsetToolTip();
  m_lastTooltipIndex = index;

  const Channel &ch = m_channels[index];

  wxString name = wxString::FromUTF8(ch.getName());
  wxString group = wxString::FromUTF8(ch.getGroupTitle());
  wxString country = wxString::FromUTF8(ch.getCountry());
  wxString lang = wxString::FromUTF8(ch.getLanguage());

  auto normalize = [](wxString &s) {
    s.Trim(true).Trim(false);
    if (s.IsEmpty())
      return;
    if (s.CmpNoCase("undefined") == 0 || s.CmpNoCase("null") == 0 ||
        s.CmpNoCase("none") == 0) {
      s.clear();
    }
  };

  normalize(group);
  normalize(country);
  normalize(lang);

  wxString tip;
  tip << name;

  if (!group.IsEmpty())
    tip << "\n" << group;

  if (!country.IsEmpty() || !lang.IsEmpty()) {
    tip << "\n";
    if (!country.IsEmpty())
      tip << country;
    if (!country.IsEmpty() && !lang.IsEmpty())
      tip << " • ";
    if (!lang.IsEmpty())
      tip << lang;
  }

  if (tip.IsEmpty()) {
    UnsetToolTip();
    return;
  }

  // Добавляем текущую программу из EPG (если доступна)
  Application *app = static_cast<Application *>(wxTheApp);
  if (app && index >= 0 && index < (int)m_channels.size()) {
    EPGManager *epg = app->GetEPGManager();
    if (epg && epg->IsLoaded()) {
      const Channel &ch = m_channels[index];
      std::string tvgId = ch.getTvgId();
      if (!tvgId.empty()) {
        EpgProgram prog = epg->GetCurrentProgram(tvgId);
        if (!prog.title.empty()) {
          tip << "\nProgram: " << wxString::FromUTF8(prog.title);
        }
      }
    }
  }

  CallAfter([this, tip]() {
    SetToolTip(wxEmptyString);
    SetToolTip(tip);
  });
}

void CardsBase::UpdateHoverAtPoint(const wxPoint &clientPos) {
  bool fav = false;
  wxRect dummy;
  int newHover = HitTestIndex(clientPos, fav, &dummy);

  if (newHover == m_hoverIndex && fav == m_hoverFav)
    return;

  int oldHover = m_hoverIndex;
  int oldFocus = m_focusIndex;

  m_hoverIndex = newHover;
  m_hoverFav = fav;

  if (m_hoverIndex >= 0)
    m_focusIndex = m_hoverIndex;

  if (oldHover >= 0)
    InvalidateCardClientRectByIndex(oldHover);
  if (m_hoverIndex >= 0)
    InvalidateCardClientRectByIndex(m_hoverIndex);

  if (oldFocus >= 0 && oldFocus != m_hoverIndex)
    InvalidateCardClientRectByIndex(oldFocus);
  if (m_focusIndex >= 0)
    InvalidateCardClientRectByIndex(m_focusIndex);
}

void CardsBase::OnKeyDown(wxKeyEvent &evt) {
  if (!this->IsShownOnScreen() || m_channels.empty()) {
    evt.Skip();
    return;
  }

  const int key = evt.GetKeyCode();

  if (m_focusIndex < 0)
    m_focusIndex = 0;

  auto syncHoverToFocus = [&]() {
    int oldHover = m_hoverIndex;
    m_hoverIndex = m_focusIndex;
    m_hoverFav = false;

    if (oldHover >= 0 && oldHover != m_focusIndex)
      InvalidateCardClientRectByIndex(oldHover);
    if (m_hoverIndex >= 0)
      InvalidateCardClientRectByIndex(m_hoverIndex);
  };

  auto moveFocus = [&](int newIndex) {
    if (newIndex < 0 || newIndex >= (int)m_channels.size())
      return;

    int old = m_focusIndex;
    m_focusIndex = newIndex;

    if (old >= 0 && old != newIndex)
      InvalidateCardClientRectByIndex(old, false);
    InvalidateCardClientRectByIndex(newIndex, false);

    syncHoverToFocus();
  };

  auto ensureVisible = [&](int index) {
    int row = index / m_cols;

    int sx, sy;
    GetViewStart(&sx, &sy);
    int px, py;
    GetScrollPixelsPerUnit(&px, &py);
    if (py <= 0)
      py = 1;

    int viewTop = sy * py;
    int viewBottom = viewTop + GetClientSize().GetHeight();

    int itemTop = row * m_rowH;
    int itemBottom = itemTop + m_rowH;

    if (itemTop < viewTop) {
      Scroll(-1, itemTop / py);
    } else if (itemBottom > viewBottom) {
      Scroll(-1, (itemBottom - GetClientSize().GetHeight()) / py);
    }
  };

  const int totalRows = (int)((m_channels.size() + m_cols - 1) / m_cols);
  int curRow = m_focusIndex / m_cols;
  int curCol = m_focusIndex % m_cols;

  int clientH = GetClientSize().GetHeight();
  if (clientH <= 0)
    clientH = m_rowH;

  switch (key) {
  case WXK_LEFT:
    if (curCol > 0) {
      int ni = m_focusIndex - 1;
      moveFocus(ni);
      ensureVisible(ni);
    }
    break;

  case WXK_RIGHT:
    if (m_focusIndex + 1 < (int)m_channels.size()) {
      int ni = m_focusIndex + 1;
      moveFocus(ni);
      ensureVisible(ni);
    }
    break;

  case WXK_UP:
    if (curRow > 0) {
      int ni = m_focusIndex - m_cols;
      moveFocus(ni);
      ensureVisible(ni);
    }
    break;

  case WXK_DOWN:
    if (m_focusIndex + m_cols < (int)m_channels.size()) {
      int ni = m_focusIndex + m_cols;
      moveFocus(ni);
      ensureVisible(ni);
    }
    break;

  case WXK_PAGEUP:
  case 380: {
    int sx, sy;
    GetViewStart(&sx, &sy);
    int px, py;
    GetScrollPixelsPerUnit(&px, &py);
    if (py <= 0)
      py = 1;

    int rowsVisible = clientH / m_rowH;
    if (rowsVisible < 1)
      rowsVisible = 1;

    int targetRow = curRow - rowsVisible;
    if (targetRow < 0)
      targetRow = 0;

    int targetTop = targetRow * m_rowH;
    Scroll(-1, targetTop / py);

    GetViewStart(&sx, &sy);
    int realTop = sy * py;
    int focusRow = (realTop + m_rowH / 2) / m_rowH;
    int index = focusRow * m_cols;
    if (index >= (int)m_channels.size())
      index = (int)m_channels.size() - 1;

    moveFocus(index);
    break;
  }

  case WXK_PAGEDOWN:
  case 381: {
    int sx, sy;
    GetViewStart(&sx, &sy);
    int px, py;
    GetScrollPixelsPerUnit(&px, &py);
    if (py <= 0)
      py = 1;

    int rowsVisible = clientH / m_rowH;
    if (rowsVisible < 1)
      rowsVisible = 1;

    int targetRow = curRow + rowsVisible;
    if (targetRow >= totalRows)
      targetRow = totalRows - 1;

    int targetTop = targetRow * m_rowH;
    Scroll(-1, targetTop / py);

    GetViewStart(&sx, &sy);
    int realTop = sy * py;
    int focusRow = (realTop + m_rowH / 2) / m_rowH;
    int index = focusRow * m_cols;
    if (index >= (int)m_channels.size())
      index = (int)m_channels.size() - 1;

    moveFocus(index);
    break;
  }

  case WXK_HOME:
  case 375:
    Scroll(-1, 0);
    moveFocus(0);
    break;

  case WXK_END:
  case 382: {
    int lastIndex = (int)m_channels.size() - 1;

    int px, py;
    GetScrollPixelsPerUnit(&px, &py);
    if (py <= 0)
      py = 1;

    int totalH = totalRows * m_rowH;
    int targetTop = std::max(0, totalH - clientH);

    Scroll(-1, targetTop / py);
    moveFocus(lastIndex);
    break;
  }

  case WXK_SPACE: {
    if (m_focusIndex < 0 || m_focusIndex >= (int)m_channels.size())
      break;

    if (m_cols <= 0 || m_cardW <= 0 || m_cardH <= 0)
      break;

    int idx = m_focusIndex;
    int oldFocus = m_focusIndex;

    int col = idx % m_cols;
    int row = idx / m_cols;
    int cardX = m_gridOffsetX + col * m_colW;
    int cardY = row * m_rowH;
    wxRect rect(cardX, cardY, m_cardW, m_cardH);

    OnCardClick(static_cast<size_t>(idx), true, rect);

    m_focusIndex = idx;
    m_hoverIndex = idx;
    m_hoverFav = false;

    if (oldFocus >= 0 && oldFocus != m_focusIndex)
      InvalidateCardClientRectByIndex(oldFocus, false);
    InvalidateCardClientRectByIndex(m_focusIndex, false);

    {
      int sx, sy;
      GetViewStart(&sx, &sy);
      int px, py;
      GetScrollPixelsPerUnit(&px, &py);
      if (py <= 0)
        py = 1;

      int viewTop = sy * py;
      int viewBottom = viewTop + GetClientSize().GetHeight();

      int itemTop = row * m_rowH;
      int itemBottom = itemTop + m_rowH;

      if (itemTop < viewTop) {
        Scroll(-1, itemTop / py);
      } else if (itemBottom > viewBottom) {
        Scroll(-1, (itemBottom - GetClientSize().GetHeight()) / py);
      }
    }

    CallAfter([this, idx, oldFocus]() {
      if (IsBeingDeleted() || !IsShownOnScreen())
        return;

      m_focusIndex = idx;
      m_hoverIndex = idx;
      m_hoverFav = false;

      if (oldFocus >= 0 && oldFocus != m_focusIndex)
        InvalidateCardClientRectByIndex(oldFocus, false);
      InvalidateCardClientRectByIndex(m_focusIndex, false);

      int row = idx / m_cols;
      int sx, sy;
      GetViewStart(&sx, &sy);
      int px, py;
      GetScrollPixelsPerUnit(&px, &py);
      if (py <= 0)
        py = 1;

      int viewTop = sy * py;
      int viewBottom = viewTop + GetClientSize().GetHeight();
      int itemTop = row * m_rowH;
      int itemBottom = itemTop + m_rowH;

      if (itemTop < viewTop) {
        Scroll(-1, itemTop / py);
      } else if (itemBottom > viewBottom) {
        Scroll(-1, (itemBottom - GetClientSize().GetHeight()) / py);
      }
    });

    return;
  }

  case WXK_RETURN:
  case WXK_NUMPAD_ENTER: {
    if (m_focusIndex < 0 || m_focusIndex >= (int)m_channels.size()) {
      break;
    }

    int idx = m_focusIndex;
    int col = idx % m_cols;
    int row = idx / m_cols;

    int cardX = m_gridOffsetX + col * m_colW;
    int cardY = row * m_rowH;

    wxRect rect(cardX, cardY, m_cardW, m_cardH);

    OnCardClick(static_cast<size_t>(idx), false, rect);

    return;
  }

  default:
    evt.Skip();
    return;
  }
}

void CardsBase::OnContextMenu(wxContextMenuEvent &evt) {
  wxMenu menu;
  int idProgramGuide = wxNewId();
  int idCopyUrl = wxNewId();
  int idCopyName = wxNewId();
  int idRemove = wxNewId();
  menu.Append(idProgramGuide, "Program Guide");
  menu.Append(idCopyUrl, "Copy URL");
  menu.Append(idCopyName, "Copy Name");
  menu.Append(idRemove, "Remove from playlist");

  wxPoint pos = evt.GetPosition();
  if (pos == wxDefaultPosition) {
    pos = wxGetMousePosition();
  }

  wxPoint clientPos = ScreenToClient(pos);
  bool fav = false;
  wxRect rect;
  int idx = HitTestIndex(clientPos, fav, &rect);
  if (idx < 0)
    return;

  int selection = GetPopupMenuSelectionFromUser(menu);
  if (selection == idProgramGuide) {
    MainFrame *mf = GetMainFrame();
    if (mf) {
      mf->SwitchToEpgTab(m_channels[idx]);
    }
  } else if (selection == idCopyUrl) {
    if (wxTheClipboard->Open()) {
      wxTheClipboard->SetData(
          new wxTextDataObject(wxString::FromUTF8(m_channels[idx].getUrl())));
      wxTheClipboard->Close();
    }
  } else if (selection == idCopyName) {
    if (wxTheClipboard->Open()) {
      wxTheClipboard->SetData(
          new wxTextDataObject(wxString::FromUTF8(m_channels[idx].getName())));
      wxTheClipboard->Close();
    }
  } // --- Добавляем обработку удаления ---
  else if (selection == idRemove) {
    MainFrame *mf = GetMainFrame();
    if (mf) {
      mf->RemoveChannelFromPlaylist(m_channels[idx]);
    }
  }
}
