// src/MainFrame_PlaylistLogic.cpp
#include "Application.h"
#include "ChannelCards.h"
#include "ChannelList.h"
#include "ConfigManager.h"
#include "Dialogs.h"
#include "FavoritesManager.h"
#include "IconManager.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Playlist.h"
#include "PlaylistManager.h"
#include "Profiler.h"
#include "SettingsDialog.h"
#include "UpdateAllThread.h"
#include "UpdateOneThread.h"
#include "Utils.h"
#include "iptvorg/AddIPTVPlaylistDialog.h"

#include <wx/msgdlg.h>

void MainFrame::RefreshPlaylistView() {
  if (!validatePlaylistManager())
    return;

  // Попытаемся сохранить текущий модельный индекс выделения (если есть)
  int prevSelectedModelIndex = -1;
  if (m_playlistList) {
    long cur =
        m_playlistList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (cur != -1) {
      prevSelectedModelIndex =
          static_cast<int>(m_playlistList->GetItemData(cur));
    }
  }

  m_playlistList->DeleteAllItems();
  auto *mgr = getPlaylistManager();
  const auto &playlists = mgr->getPlaylists();

  long row = 0;
  for (size_t i = 0; i < playlists.size(); ++i) {
    Playlist *pl = playlists[i].get();
    if (!pl)
      continue;

    wxString title = wxString::FromUTF8(pl->getTitle());
    wxString source = wxString::FromUTF8(pl->getSource());
    wxString channels = wxString::Format("%zu", pl->getChannelCount());
    wxString autoUpd = pl->getAutoUpdate() ? "Yes" : "No";
    wxString lastUpd = formatTimestamp(pl->getLastUpdate());

    long itemIndex =
        m_playlistList->InsertItem(row, wxString::Format("%zu", i + 1));
    m_playlistList->SetItem(itemIndex, 1, title);
    m_playlistList->SetItem(itemIndex, 2, source);
    m_playlistList->SetItem(itemIndex, 3, channels);
    m_playlistList->SetItem(itemIndex, 4, autoUpd);
    m_playlistList->SetItem(itemIndex, 5, lastUpd);
    m_playlistList->SetItemData(itemIndex, static_cast<wxUIntPtr>(i));
    ++row;
  }

  adjustTitleColumnWidth();
  updateStatusBar(playlists.size());

  // Попытка восстановить выделение по сохранённому модельному индексу
  bool restored = false;
  if (prevSelectedModelIndex >= 0) {
    for (long r = 0; r < m_playlistList->GetItemCount(); ++r) {
      if (static_cast<int>(m_playlistList->GetItemData(r)) ==
          prevSelectedModelIndex) {
        m_playlistList->SetItemState(r, wxLIST_STATE_SELECTED,
                                     wxLIST_STATE_SELECTED);
        m_playlistList->EnsureVisible(r);
        m_selectedPlaylistIndex = prevSelectedModelIndex;
        enablePlaylistButtons(true);
        restored = true;
        break;
      }
    }
  }

  if (!restored) {
    m_selectedPlaylistIndex = -1;
    enablePlaylistButtons(false);
    SetStatusText("No playlist selected", 1);
  }
}

void MainFrame::addPlaylistToView(size_t index, const Playlist *playlist) {
  const wxString indexStr = wxString::Format("%zu", index + 1);
  const long item =
      m_playlistList->InsertItem(static_cast<long>(index), indexStr);

  m_playlistList->SetItem(item, 1, wxString::FromUTF8(playlist->getTitle()));
  m_playlistList->SetItem(item, 2, wxString::FromUTF8(playlist->getSource()));
  m_playlistList->SetItem(item, 3,
                          wxString::Format("%zu", playlist->getChannelCount()));
  m_playlistList->SetItem(item, 4, playlist->getAutoUpdate() ? "Yes" : "No");
  m_playlistList->SetItem(item, 5, formatTimestamp(playlist->getLastUpdate()));
  m_playlistList->SetItemData(item, static_cast<wxUIntPtr>(index));
}

void MainFrame::updateStatusBar(size_t playlistCount) {
  SetStatusText(wxString::Format("Playlists: %zu", playlistCount), 1);
}

void MainFrame::resetPlaylistSelection() {
  m_openBtn->Enable(false);
  m_updateBtn->Enable(false);
  m_editBtn->Enable(false);
  m_removeBtn->Enable(false);
  m_selectedPlaylistIndex = -1;
}

void MainFrame::enablePlaylistButtons(bool enable) {
  m_openBtn->Enable(enable);
  m_updateBtn->Enable(enable);
  m_editBtn->Enable(enable);
  m_removeBtn->Enable(enable);
}

void MainFrame::loadPlaylistChannels(const std::vector<Channel> &channels,
                                     const wxString &title) {
  if (IsBeingDeleted())
    return;

  wxString header =
      wxString::Format("Playlist: %s / Channels: %zu", title, channels.size());
  m_channelsHeader->SetLabel(header);

  // 1) Сохраняем оригинальный список каналов для фильтрации/сортировки
  m_allChannels = channels;

  // Ensure filter UI exists before we try to populate it
  if (!m_filterPanel) {
    createChannelsFilterPanel();
    if (m_channelViewBook) {
      UpdateFilterPanelVisibility();
    }
  }

  // 2) Заполняем выпадающие списки фильтров (group/country/lang)
  FillFilterChoices(channels);

  // 3) Применяем текущие фильтры и сортировку (обновит list и cards)
  ApplyFiltersAndSort();

  // --- EPG INTEGRATION (SQLite, только плейлист) ---
  Application *app = static_cast<Application *>(wxTheApp);
  if (app) {
    EPGManager *epg = app->GetEPGManager();
    if (epg) {
      auto *mgr = getPlaylistManager();
      std::string playlistId;
      if (mgr && m_loadedPlaylistIndex >= 0) {
        Playlist *pl =
            mgr->getPlaylist(static_cast<size_t>(m_loadedPlaylistIndex));
        if (pl)
          playlistId = pl->getUniqueId();
      }

      if (!playlistId.empty()) {
        epg->SetCurrentPlaylistId(playlistId);
        bool loaded = epg->LoadMappingForPlaylist(playlistId, channels);
        if (!loaded) {
          epg->CancelMatching();
          if (!m_isMatching.exchange(true)) {
            epg->MatchChannelsAsync(
                channels, playlistId,
                [this](int matched, int total, int progress, bool success) {
                  wxTheApp->CallAfter(
                      [this, matched, total, progress, success]() {
                        if (progress < total) {
                          // Промежуточный прогресс – статус будет обновлён в
                          // OnEpgProgress
                          return;
                        }

                        m_isMatching = false;

                        if (success) {
                          SetStatusText(
                              wxString::Format("EPG matched %d/%d channels",
                                               matched, total),
                              1);
                          SetStatusText("EPG matching completed", 0);
                        } else {
                          SetStatusText("EPG matching failed", 1);
                          SetStatusText("Error: EPG matching failed", 0);
                        }
                      });
                });
          } else {
            LOG_DEBUG("MatchChannelsAsync already running, skipping");
          }
        } else {
          // Mapping already loaded – nothing to do
        }
      } else {
        LOG_WARN("loadPlaylistChannels: playlistId is empty");
      }
    }
  }
  // --- END EPG INTEGRATION ---

  // 4) Обновляем видимость панели фильтров и делаем Refresh/автофокус отложенно
  CallAfter([this]() {
    UpdateFilterPanelVisibility();

    if (m_channelCards) {
      m_channelCards->RefreshCards();
    }

    // --- АВТОФОКУС ПОСЛЕ ЗАГРУЗКИ ПЛЕЙЛИСТА ---
    if (m_notebook->GetSelection() == m_channelsPageIdx) {
      if (m_channelViewBook && m_channelViewBook->GetSelection() == 0) {
        // List
        if (m_channelList)
          m_channelList->SetFocusFromKbd();
      } else {
        // Grid
        if (m_channelCards)
          m_channelCards->SetFocusIgnoringChildren();
      }
    }
  });
}

void MainFrame::refreshFavorites() {
  PROFILE_SCOPE("MainFrame::refreshFavorites");
  if (!m_application)
    return;

  auto favChannels = m_application->getFavoritesManager().list();

  // --- EPG для избранных ---
  Application *app = static_cast<Application *>(wxTheApp);
  if (app) {
    EPGManager *epg = app->GetEPGManager();
    if (epg && !favChannels.empty()) {
      const std::string favPlaylistId = "favorites";
      bool loaded = epg->LoadMappingForPlaylist(favPlaylistId, favChannels);
      if (!loaded) {
        epg->CancelMatching();
        if (!m_isMatchingFavorites.exchange(true)) {
          epg->MatchChannelsAsync(
              favChannels, favPlaylistId,
              [this](int matched, int total, int progress, bool success) {
                wxTheApp->CallAfter([this, matched, total, progress,
                                     success]() {
                  if (progress < total) {
                    // Промежуточный прогресс – статус будет обновлён в
                    // OnEpgProgress
                    return;
                  }

                  m_isMatchingFavorites = false;

                  if (success) {
                    SetStatusText(
                        wxString::Format("Favorites EPG matched %d/%d",
                                         matched, total),
                        1);
                    SetStatusText("Favorites EPG updated", 0);
                  } else {
                    SetStatusText("Favorites EPG matching failed", 1);
                    SetStatusText("Error: favorites EPG matching failed", 0);
                  }
                });
              });
        }
      } else {
        // mapping уже загружен – ничего не делаем
      }
    }
  }
  // --- Конец EPG для избранных ---

  if (m_favHeader) {
    m_favHeader->SetLabel(wxString::Format("Favorites: %lu channels",
                                           (unsigned long)favChannels.size()));
  }

  if (m_favList) {
    m_favList->loadChannels(favChannels);
  }

  std::vector<std::pair<std::string, std::string>> favKeys;
  favKeys.reserve(favChannels.size());
  
  for (const auto &c : favChannels)
    favKeys.emplace_back(c.getName(), c.getPlaylistName());

  if (m_favCards) {
    CallAfter([this, favChannels, favKeys]() {
      m_favCards->SetChannels(favChannels);
      m_favCards->SyncFavorites(favKeys);
      m_favCards->RefreshCards();
      m_favCards->Refresh();
    });
  } else {
    LOG_DEBUG("refreshFavorites: m_favCards is null");
  }

  if (m_channelList) {
    m_channelList->BeginFavoritesSync();
    m_channelList->GetModel()->SetFavorites(favKeys);
    m_channelList->EndFavoritesSync();
    m_channelList->Refresh();
  } else {
    LOG_DEBUG("refreshFavorites: m_channelList is null");
  }

  if (m_channelCards) {
    m_channelCards->RefreshCards();
  }

  if (m_favViewBook)
    m_favViewBook->Layout();
}

bool MainFrame::validatePlaylistSelection(bool showMessage) {
  // Контрол может быть ещё не создан
  if (!m_playlistList) {
    if (showMessage)
      showError(this, "Playlist list is not available");
    m_selectedPlaylistIndex = -1;
    enablePlaylistButtons(false);
    return false;
  }

  // Получаем индекс выделенной строки в контроле (строка -> модельный индекс)
  long item =
      m_playlistList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (item == -1) {
    if (showMessage) {
      wxMessageBox("Please select a playlist first.", "Info",
                   wxOK | wxICON_INFORMATION, this);
    }
    m_selectedPlaylistIndex = -1;
    enablePlaylistButtons(false);
    SetStatusText("No playlist selected", 1);
    return false;
  }

  // В item хранится модельный индекс в SetItemData
  wxUIntPtr data = m_playlistList->GetItemData(item);
  int modelIndex = static_cast<int>(data);

  // Проверка валидности относительно PlaylistManager
  auto *mgr = getPlaylistManager();
  if (!mgr || modelIndex < 0 ||
      static_cast<size_t>(modelIndex) >= mgr->getPlaylists().size()) {
    if (showMessage)
      showError(this, "Selected playlist is not available");
    m_selectedPlaylistIndex = -1;
    enablePlaylistButtons(false);
    SetStatusText("No playlist selected", 1);
    return false;
  }

  // Всё ок — синхронизируем внутреннее поле и включаем кнопки
  m_selectedPlaylistIndex = modelIndex;
  enablePlaylistButtons(true);

  // Обновим статусбар с названием (необязательно, но полезно)
  Playlist *pl = mgr->getPlaylist(static_cast<size_t>(modelIndex));
  if (pl) {
    SetStatusText(wxString::Format("Playlist selected: %s",
                                   wxString::FromUTF8(pl->getTitle())),
                  1);
  }

  return true;
}

bool MainFrame::validatePlaylistManager() const {
  if (!getPlaylistManager()) {
    showError(const_cast<MainFrame *>(this), "PlaylistManager not available");
    return false;
  }
  return true;
}

bool MainFrame::validateApplication() const {
  if (!m_application) {
    showError(const_cast<MainFrame *>(this), "Application not available");
    return false;
  }
  return true;
}

Playlist *MainFrame::GetPlaylistByIndex(int idx) const {
  auto *mgr = getPlaylistManager();
  if (!mgr)
    return nullptr;
  const auto &list = mgr->getPlaylists();
  return (idx >= 0 && static_cast<size_t>(idx) < list.size()) ? list[idx].get()
                                                              : nullptr;
}

void MainFrame::savePlaylistsToConfig() {
  auto *playlistManager = getPlaylistManager();
  auto *configManager = getConfigManager();

  if (playlistManager && configManager) {
    playlistManager->savePlaylists();
    configManager->saveSettings();
  }
}

void MainFrame::handlePlaylistExport(const wxString &exportPath,
                                     int playlistIndex) {
  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  const int idx =
      (playlistIndex >= 0)
          ? playlistIndex
          : (validatePlaylistSelection(false) ? m_selectedPlaylistIndex : -1);
  if (idx < 0) {
    showError(this, "No playlist selected for export");
    return;
  }

  ErrorCode ec =
      mgr->exportPlaylist(static_cast<size_t>(idx), exportPath.ToStdString());
  if (ec != ErrorCode::OK) {
    showError(this, "Failed to export playlist:\n" +
                        wxString::FromUTF8(mgr->getLastError()));
    return;
  }
  savePlaylistsToConfig();
  RefreshPlaylistView();
  showInfo(this, "Playlist exported successfully.");
}

void MainFrame::editPlaylistAtIndex(int index) {
  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;
  Playlist *pl = mgr->getPlaylist(static_cast<size_t>(index));
  if (!pl) {
    showError(this, "Playlist not found");
    return;
  }

  // ⚠️ сохраняем данные до передачи в диалог
  wxString title = wxString::FromUTF8(pl->getTitle());
  wxString source = wxString::FromUTF8(pl->getSource());
  wxString userAgent = wxString::FromUTF8(pl->getUserAgent());
  bool autoUpdate = pl->getAutoUpdate();

  EditPlaylistDialog dlg(this, title, source, userAgent, autoUpdate);
  dlg.SetPlaylistIndex(static_cast<size_t>(index));

  if (dlg.ShowModal() == wxID_OK) {
    updatePlaylistFromDialog(index, dlg);
  }
}

void MainFrame::updatePlaylistFromDialog(int index,
                                         const EditPlaylistDialog &dlg) {
  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  ErrorCode ec = mgr->editPlaylist(
      static_cast<size_t>(index), dlg.GetTitle().ToStdString(),
      dlg.GetSource().ToStdString(), dlg.GetUserAgent().ToStdString(),
      dlg.GetAutoUpdate());
  if (ec != ErrorCode::OK) {
    showError(this, "Failed to save playlist:\n" +
                        wxString::FromUTF8(mgr->getLastError()));
    return;
  }

  // Объявляем app и pl
  Application *app = static_cast<Application *>(wxTheApp);
  Playlist *pl = mgr->getPlaylist(static_cast<size_t>(index));
  if (app && app->GetEPGManager() && pl) {
    app->GetEPGManager()->InvalidatePlaylistMapping(pl->getUniqueId());
  }

  savePlaylistsToConfig();
  RefreshPlaylistView();
  showInfo(this, "Playlist saved.");
}

void MainFrame::startAutoUpdateFromSavedPlaylists() {
  if (!validateApplication() || !validatePlaylistManager())
    return;
  if (m_autoUpdateStarted)
    return;
  m_autoUpdateStarted = true;

  auto *mgr = getPlaylistManager();
  const auto &pls = mgr->getPlaylists();

  // Собираем индексы URL-плейлистов с автообновлением
  std::vector<std::size_t> autoIndices;
  for (std::size_t i = 0; i < pls.size(); ++i) {
    const Playlist *p = pls[i].get();
    if (!p)
      continue;
    // if (p->isUrl() && p->getAutoUpdate())
    if (p->getAutoUpdate())
      autoIndices.push_back(i);
  }

  if (autoIndices.empty())
    return; // нечего обновлять

  // UI: показать верхний гейдж и заблокировать кнопки
  if (m_gaugeTop) {
    m_gaugeTop->SetRange(static_cast<int>(autoIndices.size()));
    m_gaugeTop->SetValue(0);
    m_gaugeTop->Show();
  }
  if (m_updateBtn)
    m_updateBtn->Enable(false);
  if (m_updateAllBtn)
    m_updateAllBtn->Enable(false);
  if (auto *sizer = GetSizer())
    sizer->Layout();

  // Запускаем UpdateAllThread с конкретным списком индексов
  UpdateAllThread *thread =
      new UpdateAllThread(this, getPlaylistManager(), autoIndices);
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
    showError(this, "Unable to start auto-update thread.");
  }
}

void MainFrame::HighlightLoadedPlaylistInList() {
  if (!m_playlistList)
    return;

  if (m_loadedPlaylistIndex < 0)
    return;

  long count = m_playlistList->GetItemCount();
  if (m_loadedPlaylistIndex >= count)
    return;

  // выделяем строку
  m_playlistList->SetItemState(m_loadedPlaylistIndex, wxLIST_STATE_SELECTED,
                               wxLIST_STATE_SELECTED);

  m_playlistList->EnsureVisible(m_loadedPlaylistIndex);

  // обновляем статусбар
  SetStatusText(wxString::Format("Playlist selected: %s",
                                 wxString::FromUTF8(m_loadedPlaylistName)),
                1);
}

void MainFrame::HandlePlaylistPageChanged(int sel) {
  int playlistPage = m_notebook->FindPage(m_playlistPanel);
  if (sel == playlistPage) {
    CallAfter([this]() { HighlightLoadedPlaylistInList(); });
    if (m_epgChannels)
      m_epgChannels->SetActive(false);
    if (m_epgFavorites)
      m_epgFavorites->SetActive(false);
  }
}

void MainFrame::CheckAndSuggestPlaylist() {
  if (m_playlistSuggestionShown)
    return;
  
  m_playlistSuggestionShown = true;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  // Проверяем, есть ли плейлисты
  if (!mgr->getPlaylists().empty())
    return;

  // Проверяем, не закрывается ли приложение
  if (m_closing || IsBeingDeleted())
    return;

  // Показываем вопрос
  wxMessageDialog dlg(this,
                      "No playlists found.\n\n"
                      "Would you like to add one from the IPTV-Org repository?",
                      "Welcome to IPTV Player", wxYES_NO | wxICON_QUESTION);
  dlg.SetYesNoLabels("Yes, add playlist", "No, I'll add later");

  if (dlg.ShowModal() != wxID_YES)
    return;

  // Открываем диалог добавления плейлиста
  AddIPTVPlaylistDialog addDlg(this, mgr);
  if (addDlg.ShowModal() == wxID_OK) {
    wxString url = addDlg.GetSelectedUrl();
    wxString title = addDlg.GetSelectedTitle();

    std::string titleStr = title.ToStdString();
    ErrorCode ec = mgr->addPlaylistFromUrl(url.ToStdString(), titleStr, "");
    if (ec == ErrorCode::OK) {
      savePlaylistsToConfig();
      RefreshPlaylistView();
      SetStatusText(wxString::Format("Playlist added: %s", title), 0);
    } else {
      showError(this, "Failed to add playlist:\n" +
                          wxString::FromUTF8(mgr->getLastError()));
    }
  }
}

// --------------------------------------------------------------------------
// Вспомогательные методы
// --------------------------------------------------------------------------

void MainFrame::ResetUIAfterUpdate() {
  // Восстанавливаем кнопки
  if (m_updateBtn)
    m_updateBtn->Enable(true);
  if (m_updateAllBtn)
    m_updateAllBtn->Enable(true);

  // Скрываем гейдж
  if (m_gaugeTop) {
    m_gaugeTop->SetValue(0);
    m_gaugeTop->Hide();
  }
  if (GetSizer())
    GetSizer()->Layout();

  // Сохраняем и обновляем список плейлистов
  savePlaylistsToConfig();
  RefreshPlaylistView();

  // Восстанавливаем заголовок окна
  wxString appName = wxGetApp().GetAppName();
  if (appName.IsEmpty())
    appName = "IPTV Player";
  SetTitle(appName);
}

void MainFrame::CleanupLogosForPlaylist(Playlist *pl) {
  if (!pl)
    return;

  std::vector<std::string> validNames;
  validNames.reserve(pl->getChannelCount());
  for (const auto &ch : pl->getChannels())
    validNames.push_back(ch.getName());

  IconManager::CleanupUnusedIcons(pl->getTitle(), validNames);

  // Удаляем .marker файлы
  const std::string playlist = pl->getTitle();
  for (const auto &ch : pl->getChannels()) {
    const std::string &name = ch.getName();
    std::string markerPath = IconManager::GetIconPath(playlist, name);
    if (markerPath.size() > 5)
      markerPath = markerPath.substr(0, markerPath.size() - 5) + ".marker";
    wxString mpath = wxString::FromUTF8(markerPath);
    if (wxFileExists(mpath)) {
      wxRemoveFile(mpath);
    }
  }
}

void MainFrame::CleanupLogosForAllPlaylists() {
  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  for (const auto &plPtr : mgr->getPlaylists()) {
    Playlist *pl = plPtr.get();
    if (!pl)
      continue;
    CleanupLogosForPlaylist(pl);

    if (pl->getTitle() == m_loadedPlaylistName) {
      m_playlistUpdated = true;
    }
  }
}

wxString MainFrame::GetPlaylistName(Playlist *pl) const {
  if (!pl)
    return "-";
  return wxString::FromUTF8(pl->getTitle());
}

bool MainFrame::ReloadPlaylistIfCurrent(Playlist *pl) {
  if (!pl)
    return false;
  if (pl->getTitle() != m_loadedPlaylistName)
    return false;

  const auto &channels = pl->getChannels();
  const std::string &plName = pl->getTitle();

  loadPlaylistChannels(channels, wxString::FromUTF8(plName));
  refreshFavorites();

  LOG_INFO("Playlist '%s' reloaded after update", plName);
  return true;
}

void MainFrame::RemoveChannelFromPlaylist(const Channel &ch) {
  LOG_DEBUG("RemoveChannelFromPlaylist: START for channel '%s'",
            ch.getName().c_str());

  auto *mgr = getPlaylistManager();
  if (!mgr) {
    LOG_DEBUG("RemoveChannelFromPlaylist: PlaylistManager is null");
    return;
  }

  const std::string &playlistName = ch.getPlaylistName();
  if (playlistName.empty()) {
    LOG_DEBUG("RemoveChannelFromPlaylist: playlistName is empty");
    return;
  }

  Playlist *playlist = mgr->findByTitle(playlistName);
  if (!playlist) {
    LOG_DEBUG("RemoveChannelFromPlaylist: playlist not found");
    return;
  }

  // Диалог подтверждения
  wxMessageDialog dlg(
      this,
      wxString::Format("Remove channel '%s' from playlist '%s' permanently?",
                       wxString::FromUTF8(ch.getName()),
                       wxString::FromUTF8(playlistName)),
      "Remove Channel", wxYES_NO | wxICON_QUESTION);
  if (dlg.ShowModal() != wxID_YES) {
    LOG_DEBUG("RemoveChannelFromPlaylist: user cancelled");
    return;
  }

  // 1) Удаляем из объекта Playlist (память)
  LOG_DEBUG("RemoveChannelFromPlaylist: removing from Playlist object...");
  if (!playlist->removeChannel(ch)) {
    LOG_DEBUG("RemoveChannelFromPlaylist: playlist->removeChannel failed");
    return;
  }
  LOG_DEBUG("RemoveChannelFromPlaylist: playlist->removeChannel succeeded, new "
            "count=%zu",
            playlist->getChannelCount());

  // 2) Удаляем из m_allChannels (кэш всех каналов текущего плейлиста)
  LOG_DEBUG("RemoveChannelFromPlaylist: removing from m_allChannels...");
  auto it = std::find_if(
      m_allChannels.begin(), m_allChannels.end(), [&](const Channel &c) {
        return c.getName() == ch.getName() && c.getUrl() == ch.getUrl();
      });
  if (it != m_allChannels.end()) {
    m_allChannels.erase(it);
    LOG_DEBUG("RemoveChannelFromPlaylist: m_allChannels erase succeeded, new "
              "size=%zu",
              m_allChannels.size());
  } else {
    LOG_DEBUG("RemoveChannelFromPlaylist: channel not found in m_allChannels");
  }

  // 3) Если удаляемый канал принадлежит текущему загруженному плейлисту –
  //    обновляем представление инкрементально (без полного перестроения)
  if (m_loadedPlaylistName == playlistName) {
    LOG_DEBUG("RemoveChannelFromPlaylist: updating current playlist view");

    // 3a) Удаляем из модели данных списка (это само обновит представление)
    if (m_channelList) {
      ChannelDataModel *model = m_channelList->GetModel();
      if (model) {
        LOG_DEBUG("RemoveChannelFromPlaylist: calling model->RemoveChannel");
        model->RemoveChannel(ch.getName(), ch.getUrl());
        LOG_DEBUG("RemoveChannelFromPlaylist: model->RemoveChannel done");
      }
    }

    // 3b) Обновляем заголовок (количество каналов)
    wxString header = wxString::Format("Playlist: %s / Channels: %zu",
                                       wxString::FromUTF8(m_loadedPlaylistName),
                                       m_allChannels.size());
    m_channelsHeader->SetLabel(header);
    LOG_DEBUG("RemoveChannelFromPlaylist: header updated");

    // 3c) Удаляем карточку (если она есть)
    if (m_channelCards) {
      bool removed =
          m_channelCards->RemoveChannel(ch.getName(), ch.getPlaylistName());
      if (removed) {
        LOG_DEBUG("RemoveChannelFromPlaylist: card removed");
      } else {
        LOG_DEBUG("RemoveChannelFromPlaylist: card not found, fallback to full "
                  "refresh");
        m_channelCards->SetChannels(m_allChannels, m_loadedPlaylistName);
      }
    }
  } else {
    // Если удаляем канал из другого плейлиста, обновляем только список
    // плейлистов
    LOG_DEBUG("RemoveChannelFromPlaylist: playlist is not current, refreshing "
              "playlist view");
    RefreshPlaylistView();
  }

  // 4) Статусбар
  SetStatusText(wxString::Format("Channel '%s' removed from playlist '%s'",
                                 wxString::FromUTF8(ch.getName()),
                                 wxString::FromUTF8(playlistName)),
                1);

  // 5) Фоновое сохранение и очистка файлов (с вызовом RemoveChannelMapping в
  // фоне)
  std::string chNameBg = ch.getName();
  std::string tvgId = ch.getTvgId();
  std::string iconPath = IconManager::GetIconPath(playlistName, chNameBg);
  std::string svgPath = IconManager::GetSvgPath(playlistName, chNameBg);
  std::string pngPath = iconPath.substr(0, iconPath.size() - 5) + ".png";
  std::string markerPath = iconPath.substr(0, iconPath.size() - 5) + ".marker";

  CleanupFinishedTasks();
  m_backgroundTasks.push_back(std::async(
      std::launch::async, [mgr, playlistName, chNameBg, tvgId, iconPath,
                           svgPath, pngPath, markerPath]() {
        LOG_DEBUG("RemoveChannelFromPlaylist[bg]: start saving and cleanup");

        // Сохраняем плейлист
        if (mgr) {
          Playlist *pl = mgr->findByTitle(playlistName);
          if (pl) {
            mgr->savePlaylist(pl);
            LOG_DEBUG("RemoveChannelFromPlaylist[bg]: playlist saved");
          }
        }

        // Удаляем файлы логотипов
        auto removeFile = [](const std::string &path) {
          if (path.empty())
            return;
          wxString wxPath = wxString::FromUTF8(path);
          if (wxFileExists(wxPath)) {
            wxRemoveFile(wxPath);
            LOG_DEBUG("RemoveChannelFromPlaylist[bg]: removed file %s",
                      path.c_str());
          }
        };
        removeFile(iconPath);
        removeFile(pngPath);
        removeFile(svgPath);
        removeFile(markerPath);

        // Удаляем мастер-логотип из кэша
        LOG_DEBUG(
            "RemoveChannelFromPlaylist[bg]: calling LogoCache::DropMaster");
        LogoCache::DropMaster(playlistName, chNameBg);
        LOG_DEBUG("RemoveChannelFromPlaylist[bg]: LogoCache::DropMaster done");

        // --- Удаление из EPG и избранного (синхронно в фоновом потоке) ---
        Application *app = static_cast<Application *>(wxTheApp);
        if (app) {
          if (app->GetEPGManager()) {
            app->GetEPGManager()->RemoveChannelMapping(tvgId);
            LOG_DEBUG("RemoveChannelFromPlaylist[bg]: EPG mapping removed");
          }
          app->getFavoritesManager().remove(chNameBg, playlistName);
          LOG_DEBUG("RemoveChannelFromPlaylist[bg]: removed from favorites");
        }

        LOG_DEBUG("RemoveChannelFromPlaylist[bg]: finished");
      }));

  LOG_DEBUG("RemoveChannelFromPlaylist: END");
}
