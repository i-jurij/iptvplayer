#include "Application.h"
#include "ConfigManager.h"
#include "Dialogs.h"
#include "EventIDs.h"
#include "IconManager.h"
#include "MainFrame.h"
#include "Playlist.h"
#include "PlaylistManager.h"
#include "UpdateAllThread.h"
#include "UpdateOneThread.h"
#include "Utils.h"

#include <wx/app.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/timer.h>

// ─────────────────────────────────────────────────────────────
// Event handler implementations
// ─────────────────────────────────────────────────────────────
void MainFrame::onPlaylistRightClick(wxListEvent &event) {
  if (event.GetId() != ID_PLAYLIST_LIST)
    return;

  const long listIndex = event.GetIndex();
  if (listIndex < 0)
    return;

  // Сохраняем выбранный индекс
  const wxUIntPtr data = m_playlistList->GetItemData(listIndex);
  m_selectedPlaylistIndex = static_cast<int>(data);

  // Создаём меню
  wxMenu menu;
  menu.Append(ID_OPEN_PLAYLIST, "Open");
  menu.Append(ID_UPDATE_PLAYLIST, "Update");
  menu.Append(ID_EDIT_PLAYLIST, "Edit");
  menu.Append(ID_REMOVE_PLAYLIST, "Remove");

  // Показываем меню
  PopupMenu(&menu);
}

void MainFrame::onPlaylistActivated(wxListEvent &event) {
  if (event.GetId() != ID_PLAYLIST_LIST)
    return;

  const long listIndex = event.GetIndex();
  if (listIndex < 0) {
    SetStatusText("Please select a playlist first.");
    return;
  }

  const wxUIntPtr data = m_playlistList->GetItemData(listIndex);
  m_selectedPlaylistIndex = static_cast<int>(data);

  Playlist *pl = GetPlaylistByIndex(m_selectedPlaylistIndex);
  if (!pl) {
    wxMessageBox("Playlist not found", "Error", wxOK | wxICON_ERROR, this);
    return;
  }

  // Очистка логотипов предыдущего плейлиста
  if (!m_loadedPlaylistName.empty()) {
    LogoCache::ClearPlaylist(m_loadedPlaylistName);
  }

  // Очистка очередей загрузки логотипов
  if (m_channelList) {
    m_channelList->PauseLogoLoading();
    m_channelList->ResumeLogoLoading();
  }
  if (m_channelCards) {
    m_channelCards->PauseLogoLoading();
    m_channelCards->ResumeLogoLoading();
  }

  // Запоминаем новый плейлист
  m_loadedPlaylistIndex = m_selectedPlaylistIndex;
  m_loadedPlaylistName = pl->getTitle();

  if (auto *cfg = getConfigManager()) {
    cfg->setSetting("last_opened_playlist", m_loadedPlaylistName);
    cfg->saveSettings();
  }

  showPanel(m_channelList->GetParent());

  const auto &channels = pl->getChannels();
  const std::string playlistName = pl->getTitle();

  loadPlaylistChannels(channels, playlistName);

  m_channelsHeader->SetLabel(wxString::Format(
      "Playlist: %s / Channels: %zu", wxString::FromUTF8(pl->getTitle()),
      pl->getChannelCount()));

  CallAfter([this]() { refreshFavorites(); });
}

void MainFrame::onOpenPlaylist(wxCommandEvent &event) {
  if (event.GetId() != ID_PLAYLIST_LIST)
    return;

  if (!validatePlaylistSelection()) {
    return;
  }

  Playlist *pl = GetPlaylistByIndex(m_selectedPlaylistIndex);
  if (!pl) {
    wxMessageBox("Playlist not found", "Error", wxOK | wxICON_ERROR, this);
    return;
  }

  // Очистка логотипов предыдущего плейлиста
  if (!m_loadedPlaylistName.empty()) {
    LogoCache::ClearPlaylist(m_loadedPlaylistName);
  }

  // Очистка очередей загрузки логотипов
  if (m_channelList) {
    m_channelList->PauseLogoLoading();
    m_channelList->ResumeLogoLoading();
  }
  if (m_channelCards) {
    m_channelCards->PauseLogoLoading();
    m_channelCards->ResumeLogoLoading();
  }

  // Запоминаем новый плейлист
  m_loadedPlaylistIndex = m_selectedPlaylistIndex;
  m_loadedPlaylistName = pl->getTitle();

  if (auto *cfg = getConfigManager()) {
    cfg->setSetting("last_opened_playlist", m_loadedPlaylistName);
    cfg->saveSettings();
  }

  showPanel(m_channelList->GetParent());

  const auto &channels = pl->getChannels();
  const std::string playlistName = pl->getTitle();

  loadPlaylistChannels(channels, playlistName);

  m_channelsHeader->SetLabel(wxString::Format(
      "Playlist: %s / Channels: %zu", wxString::FromUTF8(pl->getTitle()),
      pl->getChannelCount()));

  CallAfter([this]() { refreshFavorites(); });
}

//====================================================================
// Add from File
//====================================================================
void MainFrame::onAddPlaylistFile(wxCommandEvent &WXUNUSED(event)) {
  if (!validateApplication() || !validatePlaylistManager())
    return;
  AddPlaylistFileDialog dlg(this);
  if (dlg.ShowModal() != wxID_OK)
    return;

  auto *mgr = getPlaylistManager();
  ErrorCode ec = mgr->addPlaylistFromFile(dlg.GetFilePath().ToStdWstring(),
                                          dlg.GetTitle().ToStdWstring());

  if (ec == ErrorCode::OK) {
    savePlaylistsToConfig();
    RefreshPlaylistView();
    SetStatusText(
        wxString::Format("Playlist added: %s",
                         wxString::FromUTF8(dlg.GetTitle().ToStdString())));
  } else if (ec == ErrorCode::DUPLICATE) {
    SetStatusText("Duplicate playlist not added.");
    wxLogWarning(wxString::FromUTF8(mgr->getLastError()));
  } else {
    showError(this, "Failed to add playlist:\n" +
                        wxString::FromUTF8(mgr->getLastError()));
    SetStatusText("Failed to add playlist from file.");
  }
}

//====================================================================
// Add from URL (async)
//====================================================================
void MainFrame::onAddPlaylistUrl(wxCommandEvent &WXUNUSED(event)) {
  if (!validateApplication() || !validatePlaylistManager())
    return;
  AddPlaylistUrlDialog dlg(this);
  if (dlg.ShowModal() != wxID_OK)
    return;

  auto *mgr = getPlaylistManager();

  // Конвертация в UTF-8
  std::string url = dlg.GetUrl().ToUTF8().data();
  std::string title = dlg.GetTitle().ToUTF8().data();
  std::string userAgent = dlg.GetUserAgent().ToUTF8().data();

  // show top gauge and initialize UI indicators
  SetStatusText("Loading playlist from URL...", 0);

  // temporarily set window title to include progress (0% at start)
  wxString appName = wxGetApp().GetAppName();
  if (appName.IsEmpty())
    appName = "IPTV Player";
  SetTitle(wxString::Format("%s — Loading (0%%)", appName));

  // start progress timeout for this single URL load (one-shot)
  int timeout = std::stoi(
      wxGetApp().getConfigManager()->getSetting("playlistTimeoutMs", "30000"));
  m_progressTimer.Start(timeout, wxTIMER_ONE_SHOT);

  ErrorCode ec = mgr->addPlaylistFromUrl(url, title, userAgent);
  if (ec == ErrorCode::DUPLICATE) {
    SetStatusText("Duplicate playlist not added.", 0);
    wxLogWarning(wxString::FromUTF8(mgr->getLastError()));
    // restore title
    wxString appName2 = wxGetApp().GetAppName();
    if (appName2.IsEmpty())
      appName2 = "IPTV Player";

    m_progressTimer.Stop();
    SetTitle(appName2);
    return;
  }
}

void MainFrame::onAddFromUrlSuccess(wxCommandEvent &WXUNUSED(event)) {
  m_progressTimer.Stop();

  savePlaylistsToConfig();
  RefreshPlaylistView();

  // restore window title
  wxString appName = wxGetApp().GetAppName();
  if (appName.IsEmpty())
    appName = "IPTV Player";
  SetTitle(appName);

  // final status and log
  SetStatusText("Playlist added from URL.", 0);
}

void MainFrame::onAddFromUrlError(wxCommandEvent &WXUNUSED(event)) {
  m_progressTimer.Stop();
  auto *mgr = getPlaylistManager();
  const std::string lastError = mgr->getLastError();

  if (lastError.find("Duplicate") != std::string::npos) {
    SetStatusText("Duplicate playlist not added.", 0);
    wxLogWarning(wxString::FromUTF8(lastError));
  } else {
    SetStatusText("Failed to add playlist from URL.", 0);
    wxLogError("Failed to add playlist from URL: %s",
               wxString::FromUTF8(lastError));
  }

  // restore title
  wxString appName = wxGetApp().GetAppName();
  if (appName.IsEmpty())
    appName = "IPTV Player";
  SetTitle(appName);
}

void MainFrame::onProgressTimeout(wxTimerEvent &WXUNUSED(event)) {
  SetStatusText("Playlist loading timed out.", 0);
  // restore title
  wxString appName = wxGetApp().GetAppName();
  if (appName.IsEmpty())
    appName = "IPTV Player";
  SetTitle(appName);

  wxMessageDialog dlg(this,
                      "Playlist loading timed out.\nDo you want to retry?",
                      "Timeout", wxYES_NO | wxICON_WARNING);

  if (dlg.ShowModal() == wxID_YES) {
    AddPlaylistUrlDialog retryDlg(this);
    if (retryDlg.ShowModal() == wxID_OK) {
      auto *mgr = getPlaylistManager();

      // Конвертация в UTF-8
      std::string url = retryDlg.GetUrl().ToUTF8().data();
      std::string title = retryDlg.GetTitle().ToUTF8().data();
      std::string userAgent = retryDlg.GetUserAgent().ToUTF8().data();

      // Проверка дубликата
      if (mgr->isDuplicate(title, url)) {
        showError(this, "Duplicate playlist: " + title);
        SetStatusText("Duplicate playlist not added.");
        return;
      }

      SetStatusText("Retrying playlist load...");

      int timeout = std::stoi(wxGetApp().getConfigManager()->getSetting(
          "playlistTimeoutMs", "30000"));
      m_progressTimer.Start(timeout, wxTIMER_ONE_SHOT);

      mgr->addPlaylistFromUrl(url, title, userAgent);
    }
  } else {
    showError(this, "Playlist loading canceled.");
  }
}

void MainFrame::onPlaylistSelected(wxListEvent &event) {
  if (event.GetId() != ID_PLAYLIST_LIST)
    return;

  if (!validatePlaylistManager()) {
    return;
  }

  long itemIndex = event.GetIndex();
  if (itemIndex < 0) {
    return;
  }

  int playlistIndex = static_cast<int>(m_playlistList->GetItemData(itemIndex));
  auto *playlistManager = getPlaylistManager();
  if (!playlistManager || playlistIndex < 0 ||
      playlistIndex >= (int)playlistManager->getPlaylists().size()) {
    return;
  }

  Playlist *pl =
      playlistManager->getPlaylist(static_cast<size_t>(playlistIndex));
  if (!pl) {
    return;
  }

  // сохраняем выбранный индекс
  m_selectedPlaylistIndex = playlistIndex;

  // обновляем статусбар
  wxString header = wxString::Format("Playlist selected: %s",
                                     wxString::FromUTF8(pl->getTitle()));
  SetStatusText(header, 1);

  // включаем нижние кнопки
  enablePlaylistButtons(true);
}

void MainFrame::onEditPlaylist(wxCommandEvent &event) {
  if (!validatePlaylistManager())
    return;
  const wxString exportPath = event.GetString();
  if (!exportPath.IsEmpty()) {
    handlePlaylistExport(exportPath, event.GetInt());
    return;
  }
  if (!validatePlaylistSelection())
    return;
  editPlaylistAtIndex(m_selectedPlaylistIndex);
}

void MainFrame::onRemovePlaylist(wxCommandEvent &WXUNUSED(event)) {
  if (!validateApplication() || !validatePlaylistManager() ||
      !validatePlaylistSelection()) {
    return;
  }

  auto *mgr = getPlaylistManager();
  const int removeIndex = m_selectedPlaylistIndex;
  Playlist *playlist = mgr->getPlaylist(static_cast<size_t>(removeIndex));
  if (!playlist) {
    SetStatusText("Playlist not found");
    wxLogWarning("Playlist not found for index %d", removeIndex);
    return;
  }

  std::string title = playlist->getTitle();

  bool removeSource = false;
  if (!showRemovePlaylistDialog(this, playlist, removeSource))
    return;

  ErrorCode ec =
      mgr->removePlaylist(static_cast<size_t>(removeIndex), removeSource);
  if (ec != ErrorCode::OK) {
    showError(this, "Failed to remove playlist:\n" +
                        wxString::FromUTF8(mgr->getLastError()));
    return;
  }

  // Очистка логотипов удалённого плейлиста
  LogoCache::ClearPlaylist(title);
  IconManager::DeletePlaylistIcons(title);

  // Очистка очередей загрузки логотипов
  if (m_channelList) {
    m_channelList->PauseLogoLoading();
    m_channelList->ResumeLogoLoading();
  }
  if (m_channelCards) {
    m_channelCards->PauseLogoLoading();
    m_channelCards->ResumeLogoLoading();
  }

  // Удаляем избранные каналы этого плейлиста
  wxGetApp().getFavoritesManager().removeByPlaylist(title);

  CallAfter([this]() { refreshFavorites(); });

  savePlaylistsToConfig();
  RefreshPlaylistView();
  SetStatusText(
      wxString::Format("Playlist '%s' removed", wxString::FromUTF8(title)));
  wxLogInfo("Playlist removed: %s", wxString::FromUTF8(title));

  if (removeIndex == m_loadedPlaylistIndex ||
      (!m_loadedPlaylistName.empty() && m_loadedPlaylistName == title)) {

    m_channelList->loadChannelsAsync({}, "");
    if (m_channelCards)
      m_channelCards->SetChannels({}, "empty");

    m_channelsHeader->SetLabel("Playlist: - / Channels: 0");

    m_loadedPlaylistIndex = -1;
    m_loadedPlaylistName.clear();

    m_notebook->ChangeSelection(m_notebook->FindPage(m_playlistPanel));
  }

  if (mgr->getPlaylists().empty()) {
    m_selectedPlaylistIndex = -1;
    enablePlaylistButtons(false);
    SetStatusText("No playlist selected", 1);

    m_notebook->ChangeSelection(m_notebook->FindPage(m_playlistPanel));
  } else {
    long cur = m_playlistList ? m_playlistList->GetNextItem(
                                    -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)
                              : -1;
    if (cur != -1) {
      m_selectedPlaylistIndex =
          static_cast<int>(m_playlistList->GetItemData(cur));
      enablePlaylistButtons(true);
    } else {
      m_selectedPlaylistIndex = -1;
      enablePlaylistButtons(false);
    }
  }
}

void MainFrame::onUpdatePlaylist(wxCommandEvent &WXUNUSED(event)) {
  if (!validateApplication() || !validatePlaylistManager() ||
      !validatePlaylistSelection())
    return;

  const int sel = m_selectedPlaylistIndex;

  if (auto *mgr = getPlaylistManager()) {
    if (sel >= 0 && sel < static_cast<int>(mgr->getPlaylists().size())) {
      Playlist *pl = mgr->getPlaylist(static_cast<size_t>(sel));
      if (pl) {
        wxString pname = wxString::FromUTF8(pl->getTitle());
        SetStatusText(wxString::Format("Updating playlist: %s", pname), 0);
        wxString appName = wxGetApp().GetAppName();
        if (appName.IsEmpty())
          appName = "IPTV Player";
        SetTitle(wxString::Format("%s — Updating: %s", appName, pname));
      }
    }
  }

  m_updateBtn->Enable(false);
  if (m_updateAllBtn)
    m_updateAllBtn->Enable(false);

  if (auto *sizer = GetSizer())
    sizer->Layout();

  UpdateOneThread *thread = new UpdateOneThread(this, getPlaylistManager(),
                                                static_cast<std::size_t>(sel));
  if (thread->Create() != wxTHREAD_NO_ERROR ||
      thread->Run() != wxTHREAD_NO_ERROR) {
    delete thread;

    m_updateBtn->Enable(true);
    if (m_updateAllBtn)
      m_updateAllBtn->Enable(true);
    if (auto *sizer = GetSizer())
      sizer->Layout();
    showError(this, "Unable to start update thread.");
  }
}

void MainFrame::onUpdateAllPlaylists(wxCommandEvent &WXUNUSED(event)) {
  if (!validateApplication() || !validatePlaylistManager())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr) {
    showError(this, "Playlist manager is not available.");
    return;
  }
  if (mgr->getPlaylists().empty()) {
    SetStatusText("Playlist panel is open but list is empty. Add a playlist "
                  "before updating all.");
    return;
  }

  if (m_updateBtn)
    m_updateBtn->Enable(false);
  if (m_updateAllBtn)
    m_updateAllBtn->Enable(false);

  // determine total count to update
  int totalInt = 1;
  if (auto *mgr = getPlaylistManager()) {
    const auto &pls = mgr->getPlaylists();
    totalInt = static_cast<int>(pls.size() > 0 ? pls.size() : 1);
  }

  // initialize top gauge with real total
  if (m_gaugeTop) {
    m_gaugeTop->SetRange(totalInt);
    m_gaugeTop->SetValue(0);
    m_gaugeTop->Show();
  }

  // debug
  wxLogDebug("onUpdateAllPlaylists: initialized gauge range=%d", totalInt);

  // status and title
  SetStatusText("Updating playlists...", 0);
  wxString appName = wxGetApp().GetAppName();
  if (appName.IsEmpty())
    appName = "IPTV Player";
  SetTitle(wxString::Format("%s — Updating (0%%)", appName));

  if (auto *sizer = GetSizer())
    sizer->Layout();

  UpdateAllThread *thread = new UpdateAllThread(this, getPlaylistManager());
  if (thread->Create() != wxTHREAD_NO_ERROR ||
      thread->Run() != wxTHREAD_NO_ERROR) {
    delete thread;
    if (m_gaugeTop)
      m_gaugeTop->Hide();
    if (m_updateBtn)
      m_updateBtn->Enable(true);
    if (m_updateAllBtn)
      m_updateAllBtn->Enable(true);
    if (auto *sizer = GetSizer())
      sizer->Layout();
    showError(this, "Unable to start update thread.");
  }
}

void MainFrame::onUpdateAllDone(wxCommandEvent &ev) {
  if (IsBeingDeleted())
    return;

  const int updated = ev.GetInt();

  if (m_gaugeTop) {
    m_gaugeTop->SetValue(0);
    m_gaugeTop->Hide();
  }
  if (GetSizer())
    GetSizer()->Layout();

  if (m_updateAllBtn)
    m_updateAllBtn->Enable(true);
  if (m_updateBtn)
    m_updateBtn->Enable(true);

  savePlaylistsToConfig();
  RefreshPlaylistView();

  // restore title
  wxString appName = wxGetApp().GetAppName();
  if (appName.IsEmpty())
    appName = "IPTV Player";
  SetTitle(appName);

  // --- Очистка логотипов для всех обновлённых плейлистов ---
  auto *mgr = getPlaylistManager();
  if (mgr) {
    for (const auto &plPtr : mgr->getPlaylists()) {
      Playlist *pl = plPtr.get();
      if (!pl)
        continue;

      std::vector<std::string> validNames;
      validNames.reserve(pl->getChannelCount());
      for (const auto &ch : pl->getChannels())
        validNames.push_back(ch.getName());

      IconManager::CleanupUnusedIcons(pl->getTitle(), validNames);
      
      if (pl->getTitle() == m_loadedPlaylistName) {
        m_playlistUpdated = true;
      }
    }
  }

  SetStatusText(wxString::Format("Updated %d playlists.", updated));
  wxLogInfo("UpdateAll done: %d playlists updated", updated);
}

void MainFrame::onUpdateOneDone(wxCommandEvent &ev) {
  if (IsBeingDeleted())
    return;

  const int success = ev.GetInt();

  if (GetSizer())
    GetSizer()->Layout();

  if (m_updateBtn)
    m_updateBtn->Enable(true);
  if (m_updateAllBtn)
    m_updateAllBtn->Enable(true);

  savePlaylistsToConfig();
  RefreshPlaylistView();

  wxString appName = wxGetApp().GetAppName();
  if (appName.IsEmpty())
    appName = "IPTV Player";
  SetTitle(appName);

  // --- Очистка старых логотипов ---
  if (validatePlaylistSelection(false)) {
    auto *mgr = getPlaylistManager();
    if (mgr && m_selectedPlaylistIndex >= 0) {
      Playlist *pl =
          mgr->getPlaylist(static_cast<size_t>(m_selectedPlaylistIndex));
      if (pl) {
        std::vector<std::string> validNames;
        validNames.reserve(pl->getChannelCount());
        for (const auto &ch : pl->getChannels())
          validNames.push_back(ch.getName());

        IconManager::CleanupUnusedIcons(pl->getTitle(), validNames);

        if (pl->getTitle() == m_loadedPlaylistName) {
          m_playlistUpdated = true;
        }
      }

      if (pl) {
        const std::string playlist = pl->getTitle();

        for (const auto &ch : pl->getChannels()) {
          const std::string &name = ch.getName();

          std::string markerPath = IconManager::GetIconPath(playlist, name);
          if (markerPath.size() > 5)
            markerPath =
                markerPath.substr(0, markerPath.size() - 5) + ".marker";

          wxString mpath = wxString::FromUTF8(markerPath);
          if (wxFileExists(mpath)) {
            wxRemoveFile(mpath);
          }
        }
      }
    }
  }

  // Попробуем получить имя обновлённого плейлиста (если доступно)
  wxString playlistName = "-";
  if (auto *mgr = getPlaylistManager()) {
    if (m_selectedPlaylistIndex >= 0 &&
        m_selectedPlaylistIndex <
            static_cast<int>(mgr->getPlaylists().size())) {
      Playlist *pl =
          mgr->getPlaylist(static_cast<size_t>(m_selectedPlaylistIndex));
      if (pl)
        playlistName = wxString::FromUTF8(pl->getTitle());
    }
  }

  if (success) {
    SetStatusText(
        wxString::Format("Playlist '%s' updated successfully.", playlistName),
        0);
    SetStatusText("Update finished.", 1);
    wxLogInfo("UpdateOne done: playlist '%s' updated successfully",
              playlistName);
  } else {
    SetStatusText(
        wxString::Format("Playlist '%s' update failed.", playlistName), 0);
    SetStatusText("Update failed.", 1);
    wxLogError("UpdateOne failed for playlist '%s': %s", playlistName,
               wxString::FromUTF8(getPlaylistManager()->getLastError()));
  }
  if (GetSizer())
    GetSizer()->Layout();
  Refresh();
}

void MainFrame::onUpdateProgress(wxCommandEvent &ev) {
  if (IsBeingDeleted())
    return;

  const int processed = ev.GetInt();
  const int total = static_cast<int>(ev.GetExtraLong());

  if (total <= 0)
    return;

  // --- безопасное обновление top gauge с форсированной перерисовкой и логом
  if (m_gaugeTop && m_gaugeTop->IsShown()) {
    int effectiveTotal =
        (total > 0) ? total
                    : (m_gaugeTop->GetRange() > 0 ? m_gaugeTop->GetRange() : 1);
    if (effectiveTotal <= 0)
      effectiveTotal = 1;

    // clamp processed
    int clampedProcessed = std::max(0, std::min(processed, effectiveTotal));

    // set range first, then value
    m_gaugeTop->SetRange(effectiveTotal);
    m_gaugeTop->SetValue(clampedProcessed);

    // force layout and redraw: сначала сам виджет, затем его родитель и окно
    m_gaugeTop->Refresh();
    m_gaugeTop->Update();
    if (m_gaugeTop->GetParent()) {
      m_gaugeTop->GetParent()->Layout();
      m_gaugeTop->GetParent()->Refresh();
      m_gaugeTop->GetParent()->Update();
    }
    if (GetSizer()) {
      GetSizer()->Layout();
    }

    // compute percent
    int percent = static_cast<int>((100.0 * clampedProcessed) / effectiveTotal);

    // update status bar and title
    wxString title = wxString::FromUTF8(m_loadedPlaylistName);
    if (title.IsEmpty())
      title = "-";
    SetStatusText(wxString::Format("Updating playlist: %s — %d%% (%d/%d)",
                                   title, percent, clampedProcessed,
                                   effectiveTotal),
                  0);
    SetStatusText(wxString::Format("Updating: %d%% (%d/%d)", percent,
                                   clampedProcessed, effectiveTotal),
                  1);

    wxString appName = wxGetApp().GetAppName();
    if (appName.IsEmpty())
      appName = "IPTV Player";
    SetTitle(wxString::Format("%s — Updating (%d%%)", appName, percent));
  }
}

void MainFrame::onPlDelKeyDown(wxKeyEvent &event) {
  if (event.GetId() != ID_PLAYLIST_LIST)
    return;

  if (event.GetKeyCode() == WXK_DELETE) {
    if (validatePlaylistSelection(false)) {
      wxCommandEvent evt;
      onRemovePlaylist(evt);
      return;
    }
  }
  event.Skip();
}
