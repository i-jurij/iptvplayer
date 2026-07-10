#include "ConfigManager.h"
#include "EventIDs.h"
#include "LogControl.h"
#include "Utils.h"
#include "VP_SvgIcon.h"
#include "VideoPanel.h"

#include <random>
#include <wx/dir.h>
#include <wx/filename.h>

#include <fstream>
#include <sstream>
#include <wx/splitter.h>

static bool ExtractNumberFromName(const wxString &name, int &outNumber) {
  // Ищем первую подряд идущую последовательность цифр
  int len = name.Length();
  int start = -1;
  for (int i = 0; i < len; ++i) {
    if (wxIsdigit(name[i])) {
      start = i;
      break;
    }
  }

  if (start == -1)
    return false;

  int end = start;
  while (end < len && wxIsdigit(name[end]))
    ++end;

  wxString numStr = name.Mid(start, end - start);
  long val = 0;
  if (!numStr.ToLong(&val))
    return false;

  outNumber = static_cast<int>(val);
  return true;
}

void VideoPanel::LoadTempPlaylistFromConfig() {
  Application *app = dynamic_cast<Application *>(wxTheApp);
  if (!app)
    return;

  ConfigManager *cfg = app->getConfigManager();
  if (!cfg)
    return;

  std::string raw = cfg->getSetting("temp_playlist", "");
  if (raw.empty())
    return;

  wxArrayString files;
  {
    std::stringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ';')) {
      if (!item.empty())
        files.Add(wxString::FromUTF8(item));
    }
  }

  if (files.IsEmpty())
    return;

  AddToTempPlaylist(files);

  int idx = cfg->getInt("temp_playlist_index", -1);
  if (idx >= 0 && idx < static_cast<int>(m_tempPlaylist.size())) {
    m_tempCurrentIndex = idx;
  }

  if (!m_tempPlaylist.IsEmpty()) {
    ShowTempPlaylist();
  }
  // восстанавливаем выделение после загрузки
  if (m_tempCurrentIndex >= 0) {
    m_tempPlaylistList->SetItemState(m_tempCurrentIndex, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
    m_tempPlaylistList->EnsureVisible(m_tempCurrentIndex);
  }
}

void VideoPanel::SaveTempPlaylistToConfig() {
  Application *app = dynamic_cast<Application *>(wxTheApp);
  if (!app)
    return;

  ConfigManager *cfg = app->getConfigManager();
  if (!cfg)
    return;

  if (m_tempPlaylist.IsEmpty()) {
    cfg->removeSetting("temp_playlist");
    cfg->removeSetting("temp_playlist_index");
    return;
  }

  std::string joined;
  joined.reserve(m_tempPlaylist.size() * 32);

  for (size_t i = 0; i < m_tempPlaylist.size(); ++i) {
    std::string utf8 = m_tempPlaylist[i].ToUTF8().data();
    joined += utf8;
    if (i + 1 < m_tempPlaylist.size())
      joined += ";";
  }

  cfg->setSetting("temp_playlist", joined);
  cfg->setInt("temp_playlist_index", m_tempCurrentIndex);
}

void VideoPanel::AddToTempPlaylist(const wxArrayString &files) {
  m_tempPlaylist.Clear();
  for (const auto &f : files) {
    m_tempPlaylist.Add(f);
  }

  // Обновляем отображение списка (без сортировки)
  m_tempPlaylistList->DeleteAllItems();
  for (size_t i = 0; i < m_tempPlaylist.size(); ++i) {
    wxFileName fn(m_tempPlaylist[i]);
    long idx =
        m_tempPlaylistList->InsertItem(i, wxString::Format("%d", (int)i + 1));
    m_tempPlaylistList->SetItem(idx, 1, fn.GetFullName());
  }

  m_tempPlaylistList->SetColumnWidth(1, wxLIST_AUTOSIZE);
  m_tempPlaylistList->SetColumnWidth(1, wxLIST_AUTOSIZE_USEHEADER);
  m_tempPlaylistList->SetColumnWidth(0, FromDIP(40));
}

bool VideoPanel::LoadPlaylistFile(const wxString &path, wxArrayString &out) {
  std::ifstream f(path.ToStdString(), std::ios::binary);
  if (!f.is_open())
    return false;

  std::string line;

  // --- Удаляем BOM ---
  {
    char bom[3] = {0};
    f.read(bom, 3);
    if (!(bom[0] == '\xEF' && bom[1] == '\xBB' && bom[2] == '\xBF')) {
      f.seekg(0); // нет BOM → возвращаемся в начало
    }
  }

  wxFileName base(path);
  wxString baseDir = base.GetPath();

  wxString lastExtinf; // сохраняем #EXTINF, если нужно использовать позже

  while (std::getline(f, line)) {
    // Убираем CRLF
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    if (line.empty())
      continue;

    // Комментарии
    if (line[0] == '#') {
      if (line.rfind("#EXTINF", 0) == 0) {
        lastExtinf = wxString::FromUTF8(line);
      }
      continue;
    }

    wxString w = wxString::FromUTF8(line);
    if (w.empty())
      continue;

    // Относительные пути → делаем абсолютными
    if (!w.StartsWith("http://") && !w.StartsWith("https://") &&
        !w.StartsWith("rtsp://") && !wxFileName(w).IsAbsolute()) {
      wxFileName rel(baseDir, w);
      w = rel.GetFullPath();
    }

    out.Add(w);

    //LOG_DEBUG("LoadPlaylistFile: add '%s'", w);

    lastExtinf.clear();
  }

  return !out.IsEmpty();
}

void VideoPanel::PlayNextTempItem() {
  if (!m_isTempPlaylistPlaying)
    return;

  if (m_tempPlaylist.IsEmpty())
    return;

  int current = -1;

  if (m_tempCurrentIndex >= 0)
    current = m_tempCurrentIndex;
  else if (m_pendingTempPlay && m_pendingTempIndex >= 0)
    current = m_pendingTempIndex;
  else
    current = 0;

  int next = current + 1;

  if (next >= (int)m_tempPlaylist.size()) {
    Stop();
    return;
  }

  m_tempCurrentIndex = next;

  wxString path = m_tempPlaylist[next];

  m_tempState = TempPlayState::Loading;
  UpdateUiButtons();

  if (m_tempPlaylistList) {
    m_tempPlaylistList->SetItemState(next, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
    m_tempPlaylistList->EnsureVisible(next);
  }

  StartTempPlayAsync(path, next, false, "next");
}

void VideoPanel::OnTempPlaylistRemove() {
  // Собираем все выделенные строки
  std::vector<long> toRemove;
  long item = -1;

  for (;;) {
    item = m_tempPlaylistList->GetNextItem(item, wxLIST_NEXT_ALL,
                                           wxLIST_STATE_SELECTED);
    if (item == -1)
      break;

    toRemove.push_back(item);
  }

  if (toRemove.empty())
    return;

  // Удаляем с конца, чтобы индексы не смещались
  std::sort(toRemove.begin(), toRemove.end(), std::greater<long>());

  for (long idx : toRemove) {
    if (idx >= 0 && idx < (long)m_tempPlaylist.size()) {
      m_tempPlaylist.RemoveAt(idx);
      m_tempPlaylistList->DeleteItem(idx);
    }
  }

  // Если всё удалили → скрываем панель
  if (m_tempPlaylist.IsEmpty()) {
    ClearTempPlaylist();
    return;
  }
  // 🔥 ДОБАВЛЕНО: пересчитываем индекс после удаления
  if (m_tempCurrentIndex >= (int)m_tempPlaylist.size())
    m_tempCurrentIndex = m_tempPlaylist.size() - 1;

  if (m_tempCurrentIndex >= 0) {
    m_tempPlaylistList->SetItemState(m_tempCurrentIndex, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
  }

  // Сохраняем изменения
  SaveTempPlaylistToConfig();

  m_tempPlaylistList->SetFocus();
}

void VideoPanel::OnTempPlaylistOpenFolder(wxCommandEvent &) {
  long sel = m_tempPlaylistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                             wxLIST_STATE_SELECTED);

  if (sel == wxNOT_FOUND)
    return;

  if (sel < 0 || sel >= (int)m_tempPlaylist.size())
    return;

  wxFileName fn(m_tempPlaylist[sel]);
  wxString folder = fn.GetPath();

  if (!folder.empty())
    wxLaunchDefaultApplication(folder);
}

void VideoPanel::ShowTempPlaylist() {
  if (!m_splitter || !m_tempPlaylistPanel || !m_mainPanel)
    return;

  if (!m_splitter->IsSplit()) {
    m_splitter->SplitVertically(m_tempPlaylistPanel, m_mainPanel, 250);
  }

  m_tempPlaylistPanel->Show();
  Layout();
}

void VideoPanel::ClearTempPlaylist() {
  m_tempPlaylist.Clear();

  if (m_tempPlaylistList)
    m_tempPlaylistList->DeleteAllItems();

  m_isTempPlaylistPlaying = false;
  m_tempCurrentIndex = -1;

  // сбрасываем имя при очистке плейлиста
  m_currentName.clear();

  if (m_splitter && m_splitter->IsSplit())
    m_splitter->Unsplit(m_tempPlaylistPanel);

  if (m_tempPlaylistPanel)
    m_tempPlaylistPanel->Hide();

  Layout();
  SaveTempPlaylistToConfig();
}

void VideoPanel::OnTempPlaylistListActivate(wxListEvent &evt) {
  if (evt.GetId() != ID_VP_TEMP_PLAYLIST_LIST)
    return;

  int sel = evt.GetIndex();
  if (sel < 0 || sel >= (int)m_tempPlaylist.size())
    return;

  wxString path = m_tempPlaylist[sel];

  // Помечаем pending запуск из temp playlist и сохраняем индекс
  m_pendingTempPlay = true;
  m_pendingTempIndex = sel;

  //LOG_DEBUG("OnTempPlaylistListActivate: pending set idx=%d",
    //        m_pendingTempIndex);
  m_tempState = TempPlayState::Loading;
  UpdateUiButtons();

  StartTempPlayAsync(path, sel, false, "activate");

  m_tempPlaylistList->SetItemState(sel, wxLIST_STATE_SELECTED,
                                   wxLIST_STATE_SELECTED);

  this->CallAfter([this, sel]() {
    if (m_tempPlaylistList) {
      m_tempPlaylistList->SetFocus();
      m_tempPlaylistList->EnsureVisible(sel);
    }
  });
}

void VideoPanel::OnTempPlaylistContextMenu(wxContextMenuEvent &evt) {
  if (evt.GetId() != ID_VP_TEMP_PLAYLIST_LIST)
    return;

  // Проверяем, есть ли выделенный элемент
  long sel = m_tempPlaylistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                             wxLIST_STATE_SELECTED);

  if (sel == wxNOT_FOUND)
    return;

  // Показываем меню
  wxMenu menu;

  int idPlay = wxWindow::NewControlId();
  int idUp = wxWindow::NewControlId();
  int idDown = wxWindow::NewControlId();
  int idRename = wxWindow::NewControlId();
  int idRemove = wxWindow::NewControlId();
  int idClear = wxWindow::NewControlId();
  int idOpenFolder = wxWindow::NewControlId();

  menu.Append(idPlay, "Play (Enter)");
  menu.Append(idUp, "Move up (Ctrl+↑)");
  menu.Append(idDown, "Move down (Ctrl+↓)");
  menu.AppendSeparator();
  menu.Append(idRename, "Rename (F2)");
  menu.AppendSeparator();
  menu.Append(idRemove, "Remove (Delete)");
  menu.Append(idClear, "Clear playlist (Ctrl+L)");
  menu.AppendSeparator();
  menu.Append(idOpenFolder, "Open containing folder");

  menu.Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { TempPlaylistPlay(); }, idPlay);
  menu.Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { TempPlaylistMoveUp(); }, idUp);
  menu.Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { TempPlaylistMoveDown(); }, idDown);
  menu.Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { TempPlaylistRename(); }, idRename);
  menu.Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { OnTempPlaylistRemove(); },
      idRemove);
  menu.Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { ClearTempPlaylist(); }, idClear);
  menu.Bind(wxEVT_MENU, &VideoPanel::OnTempPlaylistOpenFolder, this,
            idOpenFolder);

  PopupMenu(&menu);
}

void VideoPanel::TempPlaylistPlay() {
  long sel = m_tempPlaylistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                             wxLIST_STATE_SELECTED);
  if (sel < 0)
    return;

  wxString path = m_tempPlaylist[sel];

  // Помечаем pending запуск из temp playlist и сохраняем индекс
  m_pendingTempPlay = true;
  m_pendingTempIndex = (int)sel;

  //LOG_DEBUG("TempPlaylistPlay: pending set idx=%d", m_pendingTempIndex);
  m_tempState = TempPlayState::Loading;
  UpdateUiButtons();

  StartTempPlayAsync(path, sel, false, "manual");

  // Обновляем выделение в UI (UX), но не меняем m_tempCurrentIndex пока нет
  // Playing
  m_tempPlaylistList->SetItemState(sel, wxLIST_STATE_SELECTED,
                                   wxLIST_STATE_SELECTED);

  this->CallAfter([this, sel]() {
    if (m_tempPlaylistList) {
      m_tempPlaylistList->SetFocus();
      m_tempPlaylistList->EnsureVisible(sel);
    }
  });
}

void VideoPanel::TempPlaylistMoveUp() {
  long sel = m_tempPlaylistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                             wxLIST_STATE_SELECTED);
  if (sel <= 0)
    return;

  std::swap(m_tempPlaylist[sel], m_tempPlaylist[sel - 1]);

  // обновляем отображение
  RefreshTempPlaylistWithoutSorting();

  m_tempPlaylistList->SetItemState(sel - 1, wxLIST_STATE_SELECTED,
                                   wxLIST_STATE_SELECTED);
  m_tempPlaylistList->SetFocus();
}

void VideoPanel::TempPlaylistMoveDown() {
  long sel = m_tempPlaylistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                             wxLIST_STATE_SELECTED);
  if (sel == wxNOT_FOUND || sel >= (long)m_tempPlaylist.size() - 1)
    return;

  std::swap(m_tempPlaylist[sel], m_tempPlaylist[sel + 1]);

  RefreshTempPlaylistWithoutSorting();

  m_tempPlaylistList->SetItemState(sel + 1, wxLIST_STATE_SELECTED,
                                   wxLIST_STATE_SELECTED);
  m_tempPlaylistList->SetFocus();
}

void VideoPanel::RefreshTempPlaylistWithoutSorting() {
  m_tempPlaylistList->DeleteAllItems();

  for (size_t i = 0; i < m_tempPlaylist.size(); ++i) {
    wxFileName fn(m_tempPlaylist[i]);
    long idx =
        m_tempPlaylistList->InsertItem(i, wxString::Format("%d", (int)i + 1));
    m_tempPlaylistList->SetItem(idx, 1, fn.GetFullName());
  }

  m_tempPlaylistList->SetColumnWidth(1, wxLIST_AUTOSIZE);
  m_tempPlaylistList->SetColumnWidth(1, wxLIST_AUTOSIZE_USEHEADER);
  m_tempPlaylistList->SetColumnWidth(0, FromDIP(40));

  // Восстановить выделение и фокус
  if (m_tempCurrentIndex >= 0 &&
      m_tempCurrentIndex < (int)m_tempPlaylist.size()) {
    m_tempPlaylistList->SetItemState(m_tempCurrentIndex, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
    m_tempPlaylistList->EnsureVisible(m_tempCurrentIndex);
  } else if (!m_tempPlaylist.IsEmpty()) {
    m_tempCurrentIndex = 0;
    m_tempPlaylistList->SetItemState(0, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
    m_tempPlaylistList->EnsureVisible(0);
  }

  m_tempPlaylistList->SetFocus();
}

void VideoPanel::OnTempPlaylistKeyDown(wxKeyEvent &evt) {
  //LOG_DEBUG("OnTempPlaylistKeyDown: key=%d id=%d obj=%p", evt.GetKeyCode(),
    //        evt.GetId(), evt.GetEventObject());
  // helper: снять текущее выделение
  auto clearCurrentSelection = [this]() {
    long cur = m_tempPlaylistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                               wxLIST_STATE_SELECTED);
    if (cur != -1)
      m_tempPlaylistList->SetItemState(cur, 0, wxLIST_STATE_SELECTED);
  };

  long sel = m_tempPlaylistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                             wxLIST_STATE_SELECTED);
  long count = m_tempPlaylistList->GetItemCount();

  bool ctrl = evt.ControlDown();

  // CTRL + L → Clear playlist
  if (evt.ControlDown() &&
      (evt.GetKeyCode() == 'L' || evt.GetKeyCode() == 'l')) {
    ClearTempPlaylist();
    return;
  }

  switch (evt.GetKeyCode()) {
  // -------------------------
  // ENTER → Play
  // -------------------------
  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    TempPlaylistPlay();
    evt.StopPropagation();
    return;
  // -------------------------
  // DELETE → Remove
  // -------------------------
  case WXK_DELETE:
    OnTempPlaylistRemove();
    evt.StopPropagation();
    return;

  case WXK_SPACE:
    if (m_playerController) {
      if (m_playerController->GetState() == PlayerState::Playing) {
        Pause();
      } else {
        Play();
      }
    }
    evt.StopPropagation();
    return;
  // -------------------------
  // CTRL + UP → Move Up
  // -------------------------
  case WXK_UP:
    if (ctrl) {
      TempPlaylistMoveUp();
      return;
    }
    if (sel > 0) {
      clearCurrentSelection();
      m_tempCurrentIndex = sel - 1;
      m_tempPlaylistList->SetItemState(
          m_tempCurrentIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
      m_tempPlaylistList->EnsureVisible(m_tempCurrentIndex);
    }
    return;
  // -------------------------
  // CTRL + DOWN → Move Down
  // -------------------------
  case WXK_DOWN:
    if (ctrl) {
      TempPlaylistMoveDown();
      return;
    }
    if (sel >= 0 && sel < count - 1) {
      clearCurrentSelection();
      m_tempCurrentIndex = sel + 1;
      m_tempPlaylistList->SetItemState(
          m_tempCurrentIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
      m_tempPlaylistList->EnsureVisible(m_tempCurrentIndex);
    }
    return;

  case WXK_PAGEUP: {
    long target = std::max<long>(0, sel - 10);
    clearCurrentSelection();
    m_tempCurrentIndex = target;
    m_tempPlaylistList->SetItemState(target, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
    m_tempPlaylistList->EnsureVisible(target);
    return;
  }

  case WXK_PAGEDOWN: {
    long target = std::min<long>(count - 1, sel + 10);
    clearCurrentSelection();
    m_tempCurrentIndex = target;
    m_tempPlaylistList->SetItemState(target, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
    m_tempPlaylistList->EnsureVisible(target);
    return;
  }

  case WXK_HOME:
    if (count > 0) {
      clearCurrentSelection();
      m_tempCurrentIndex = 0;
      m_tempPlaylistList->SetItemState(0, wxLIST_STATE_SELECTED,
                                       wxLIST_STATE_SELECTED);
      m_tempPlaylistList->EnsureVisible(0);
    }
    return;

  case WXK_END:
    if (count > 0) {
      long last = count - 1;
      clearCurrentSelection();
      m_tempCurrentIndex = last;
      m_tempPlaylistList->SetItemState(last, wxLIST_STATE_SELECTED,
                                       wxLIST_STATE_SELECTED);
      m_tempPlaylistList->EnsureVisible(last);
    }
    return;

  default:
    evt.Skip();
    return;
  }
}

void VideoPanel::TempPlaylistRename() {
  long sel = m_tempPlaylistList->GetNextItem(-1, wxLIST_NEXT_ALL,
                                             wxLIST_STATE_SELECTED);
  if (sel < 0)
    return;

  wxString oldName = m_tempPlaylistList->GetItemText(sel, 1);

  wxTextEntryDialog dlg(this, "Rename item:", "Rename", oldName);
  if (dlg.ShowModal() != wxID_OK)
    return;

  wxString newName = dlg.GetValue();

  // Меняем только отображаемое имя, файл на диске не трогаем
  m_tempPlaylistList->SetItem(sel, 1, newName);
}

void VideoPanel::PlayPrevTempItem() {
  if (!m_isTempPlaylistPlaying || m_tempPlaylist.IsEmpty())
    return;

  // Вычисляем текущий индекс как в PlayNextTempItem
  int current = -1;
  if (m_tempCurrentIndex >= 0) {
    current = m_tempCurrentIndex;
  } else if (m_pendingTempPlay && m_pendingTempIndex >= 0) {
    current = m_pendingTempIndex;
  } else {
    current = 0;
  }

  int prev = current - 1;
  if (prev < 0)
    return;

  m_tempCurrentIndex = prev;
  wxString path = m_tempPlaylist[prev];

  m_tempState = TempPlayState::Loading;
  UpdateUiButtons();

  StartTempPlayAsync(path, prev, false, "prev");

  // выделение
  m_tempPlaylistList->SetItemState(prev, wxLIST_STATE_SELECTED,
                                   wxLIST_STATE_SELECTED);

  m_tempPlaylistList->EnsureVisible(prev);
}

void VideoPanel::ToggleShuffleTempPlaylist(bool enable) {
  if (m_tempPlaylist.size() < 2)
    return;

  if (enable) {
    // сохраняем оригинальный порядок
    m_tempPlaylistOriginalOrder = m_tempPlaylist;
    // 🔥 ДОБАВЛЕНО: сохраняем путь текущего файла перед shuffle
    wxString currentPath;
    if (m_tempCurrentIndex >= 0 &&
        m_tempCurrentIndex < (int)m_tempPlaylist.size())
      currentPath = m_tempPlaylist[m_tempCurrentIndex];

    // современный shuffle
    static std::random_device rd;
    static std::mt19937 g(rd());
    std::shuffle(m_tempPlaylist.begin(), m_tempPlaylist.end(), g);

    m_shuffleActive = true;
    // 🔥 ДОБАВЛЕНО: восстанавливаем текущий индекс после shuffle
    if (!currentPath.IsEmpty()) {
      int newIndex = m_tempPlaylist.Index(currentPath);
      if (newIndex != wxNOT_FOUND)
        m_tempCurrentIndex = newIndex;
    }
  } else {
    // восстановление
    if (!m_tempPlaylistOriginalOrder.IsEmpty())
      m_tempPlaylist = m_tempPlaylistOriginalOrder;

    m_shuffleActive = false;
  }

  RefreshTempPlaylistWithoutSorting();

  // выделяем текущий трек, если он есть
  if (m_tempCurrentIndex >= 0 &&
      m_tempCurrentIndex < (int)m_tempPlaylist.size()) {
    m_tempPlaylistList->SetItemState(m_tempCurrentIndex, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
  }
}
