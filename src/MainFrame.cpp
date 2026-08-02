#include "MainFrame.h"
#include "Application.h"
#include "ChannelCards.h"
#include "ConfigManager.h"
#include "EventIDs.h"
#include "IconManager.h"
#include "LogControl.h"
#include "Playlist.h"
#include "PlaylistManager.h"
#include "SettingsDialog.h"
#include "UpdateAllThread.h"
#include "UpdateOneThread.h"

#include <wx/artprov.h>
#include <wx/aui/aui.h>
#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/filedlg.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/imaglist.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <ctime>

wxDEFINE_EVENT(EVT_UPDATE_ALL_DONE, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_ONE_DONE, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_EPG_UPDATED, wxCommandEvent);

MainFrame::MainFrame(Application *app)
    : wxFrame(nullptr, wxID_ANY, "IPTV Player", wxDefaultPosition,
              wxSize(800, 600)),
      m_application(app), m_mainPanel(nullptr), m_notebook(nullptr),
      m_playlistPanel(nullptr), m_playlistList(nullptr),
      m_placeholderText(nullptr), m_updateBtn(nullptr), m_editBtn(nullptr),
      m_removeBtn(nullptr), m_selectedPlaylistIndex(-1),
      m_loadedPlaylistIndex(-1), m_progressTimer(this, wxNewId()) {
  const int minW = 800;
  const int minH = 600;

  auto *cfg = getConfigManager();

  std::string mode = "grid";
  if (cfg) {
    mode = cfg->getSetting("channels_view_mode", "grid");
  }
  m_startInGrid = (mode != "list");

  if (cfg) {
    std::string nologoStr = cfg->getSetting("nologo", "true");
    m_channelsNoLogo = (nologoStr == "true");
  } else {
    m_channelsNoLogo = true; // fallback default
  }

  int savedW = cfg ? cfg->getInt("window_width", -1) : -1;
  int savedH = cfg ? cfg->getInt("window_height", -1) : -1;
  int savedX = cfg ? cfg->getInt("window_x", -1) : -1;
  int savedY = cfg ? cfg->getInt("window_y", -1) : -1;

  wxDisplay display(0u);
  wxRect screenRect = display.GetGeometry();
  int screenW = screenRect.GetWidth();
  int screenH = screenRect.GetHeight();

  if (savedW > 0 && savedH > 0) {
    savedW = std::clamp(savedW, minW, screenW);
    savedH = std::clamp(savedH, minH, screenH);

    if (savedX < 0 || savedX + savedW > screenW) {
      savedX = (screenW - savedW) / 2;
    }
    if (savedY < 0 || savedY + savedH > screenH) {
      savedY = (screenH - savedH) / 2;
    }

    wxSize winSize(savedW, savedH);
    wxPoint winPos(savedX, savedY);
    SetSize(wxRect(winPos, winSize));
  } else {
    int width = std::max(static_cast<int>(screenW * 0.75), minW);
    int height = std::max(static_cast<int>(screenH * 0.75), minH);
    SetSize(wxSize(width, height));
    Centre();
  }

  SetMinSize(wxSize(minW, minH));

  // Потоковые события обновления плейлистов
  Bind(EVT_UPDATE_PROGRESS, &MainFrame::onUpdateProgress, this);
  Bind(EVT_UPDATE_ONE_DONE, &MainFrame::onUpdateOneDone, this);
  Bind(EVT_UPDATE_ALL_DONE, &MainFrame::onUpdateAllDone, this);
  Bind(wxEVT_TIMER, &MainFrame::onProgressTimeout, this,
       m_progressTimer.GetId());

  // Кнопки управления плейлистами
  Bind(wxEVT_BUTTON, &MainFrame::onAddPlaylistFile, this, ID_ADD_PLAYLIST_FILE);
  Bind(wxEVT_BUTTON, &MainFrame::onAddPlaylistUrl, this, ID_ADD_PLAYLIST_URL);
  Bind(wxEVT_BUTTON, &MainFrame::onUpdateAllPlaylists, this,
       ID_UPDATE_ALL_PLAYLISTS);
  Bind(wxEVT_BUTTON, &MainFrame::onOpenPlaylist, this, ID_OPEN_PLAYLIST);
  Bind(wxEVT_BUTTON, &MainFrame::onUpdatePlaylist, this, ID_UPDATE_PLAYLIST);
  Bind(wxEVT_BUTTON, &MainFrame::onEditPlaylist, this, ID_EDIT_PLAYLIST);
  Bind(wxEVT_BUTTON, &MainFrame::onRemovePlaylist, this, ID_REMOVE_PLAYLIST);

  // Контекстное меню плейлистов
  Bind(wxEVT_MENU, &MainFrame::onOpenPlaylist, this, ID_OPEN_PLAYLIST);
  Bind(wxEVT_MENU, &MainFrame::onUpdatePlaylist, this, ID_UPDATE_PLAYLIST);
  Bind(wxEVT_MENU, &MainFrame::onEditPlaylist, this, ID_EDIT_PLAYLIST);
  Bind(wxEVT_MENU, &MainFrame::onRemovePlaylist, this, ID_REMOVE_PLAYLIST);

  // События списка плейлистов
  Bind(wxEVT_LIST_ITEM_SELECTED, &MainFrame::onPlaylistSelected, this,
       wxID_ANY);
  Bind(wxEVT_LIST_ITEM_ACTIVATED, &MainFrame::onPlaylistActivated, this,
       wxID_ANY);
  Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &MainFrame::onPlaylistRightClick, this,
       wxID_ANY);

  // Результат добавления плейлиста из URL
  Bind(wxEVT_COMMAND_BUTTON_CLICKED, &MainFrame::onAddFromUrlSuccess, this,
       ID_ADD_FROM_URL_SUCCESS);
  Bind(wxEVT_COMMAND_BUTTON_CLICKED, &MainFrame::onAddFromUrlError, this,
       ID_ADD_FROM_URL_ERROR);

  createMainPanel();
  createStatusBar();

  RefreshPlaylistView();

  Centre();

  m_notebook->Bind(
      wxEVT_AUINOTEBOOK_PAGE_CHANGED, [this](wxAuiNotebookEvent &evt) {
        LOG_DEBUG("MainFrame: notebook page changed event: selection=%d, "
                  "m_ignoreNotebookEvents=%d",
                  evt.GetSelection(), (int)m_ignoreNotebookEvents.load());

        if (m_ignoreNotebookEvents.load()) {
          LOG_DEBUG("Notebook change ignored due to guard");
          return;
        }

        int sel = evt.GetSelection();
        LOG_DEBUG("MainFrame: notebook page changed event: selection=%d, "
                  "pagePtr=%p, pageLabel='%s', m_videoPageIdx=%d, "
                  "m_videoPanel=%p, m_ignoreNotebookEvents=%d",
                  sel, (void *)m_notebook->GetPage(sel),
                  m_notebook->GetPageText(sel).ToUTF8().data(), m_videoPageIdx,
                  (void *)m_videoPanel, (int)m_ignoreNotebookEvents.load());

        evt.Skip();

        // --- Управление VideoPanel (активность вкладки) ---
        if (m_videoPanel && m_videoPageIdx != wxNOT_FOUND &&
            sel != m_videoPageIdx) {
          m_videoPanel->SetTabActive(false);
        }

        if (m_videoPanel && m_videoPageIdx != wxNOT_FOUND &&
            sel == m_videoPageIdx) {
          wxWindow *page = m_notebook->GetPage(sel);
          if (page == m_videoPanel) {
            m_videoPanel->SetTabActive(true);
          } else {
            LOG_DEBUG("MainFrame: selection==m_videoPageIdx but "
                      "notebook->GetPage(sel) != m_videoPanel; sel=%d page=%p "
                      "m_videoPanel=%p",
                      sel, (void *)page, (void *)m_videoPanel);
          }
        }

        // --- Вызов обработчиков страниц ---
        HandleChannelPageChanged(sel);
        HandleFavPageChanged(sel);
        HandlePlaylistPageChanged(sel);
        HandleEpgPageChanged(sel);

        // --- Управление видимостью UI в полноэкранном режиме (из внутреннего
        // обработчика) ---
        if (m_videoPanel) {
          bool isVideoPage =
              (m_videoPageIdx != wxNOT_FOUND && sel == m_videoPageIdx);
          if (!isVideoPage) {
            // Ушли с Video: показать UI и остановить таймер
            m_videoPanel->SetControlsVisible(true, false);
          } else {
            // Вернулись на Video: если UI видим, запустить таймер
            if (m_videoPanel->IsControlsVisible()) {
              m_videoPanel->SetControlsVisible(true, true);
            } else {
              // UI скрыт – таймер не запускаем, он запустится при движении мыши
              m_videoPanel->SetControlsVisible(false, false);
            }
          }
        }
      });

  Bind(wxEVT_CHAR_HOOK, &MainFrame::OnGlobalCharHook, this);

  Bind(wxEVT_BUTTON, &MainFrame::onAddIPTVPlaylist, this, ID_ADD_IPTV_PLAYLIST);

  Bind(EVT_EPG_UPDATED, &MainFrame::OnEPGUpdated, this);

  m_epgCoalesceTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &MainFrame::OnEpgCoalesceTimer, this,
       m_epgCoalesceTimer.GetId());
}

MainFrame::~MainFrame() {
  try {
    auto *cfg = getConfigManager();
    if (cfg) {
      wxRect rect = GetRect();
      cfg->setSetting("window_width", std::to_string(rect.GetWidth()));
      cfg->setSetting("window_height", std::to_string(rect.GetHeight()));
      cfg->setSetting("window_x", std::to_string(rect.GetX()));
      cfg->setSetting("window_y", std::to_string(rect.GetY()));
      cfg->setInt("m_lastVolume", m_videoPanel->GetLastVolume());
      cfg->setSetting("nologo", m_channelsNoLogo ? "true" : "false");

      cfg->saveSettings();

      std::vector<wxString> recentWx = m_videoPanel->GetRecentFiles();

      std::vector<std::string> recentRaw;
      recentRaw.reserve(recentWx.size());
      for (auto &s : recentWx)
        recentRaw.push_back(std::string(s.mb_str()));

      cfg->setRecentFiles(recentRaw);
    }

    IconManager::Shutdown();
    m_closing = true;
    // Ожидание завершения всех фоновых задач
    for (auto &fut : m_backgroundTasks) {
      if (fut.valid()) {
        fut.wait();
      }
    }
  } catch (const std::exception &e) {
    wxMessageBox(
        wxString::Format("MainFrame dtor std::exception: %s", e.what()));
  } catch (...) {
    wxMessageBox("MainFrame dtor unknown exception");
  }
}

void MainFrame::OnGlobalCharHook(wxKeyEvent &evt) {
  int key = evt.GetKeyCode();

  // ESC – выход из fullscreen (если он активен)
  if (key == WXK_ESCAPE) {
    // TypeAheadSearch перехватывает ESC в своих виджетах и не передаёт дальше,
    // поэтому здесь ESC не дойдёт, если фокус в поиске.
    if (m_videoPanel && m_videoPanel->IsFullscreen()) {
      m_videoPanel->ToggleFullscreen();
      evt.Skip(false);
      return;
    }
  }
  // F/F – переключение fullscreen
  else if (key == 'f' || key == 'F') {
    if (m_videoPanel) {
      bool isFullscreen = m_videoPanel->IsFullscreen();
      bool isVideoPage = IsVideoPageActive();

      // Если fullscreen уже включён – выключаем всегда (с любой страницы)
      if (isFullscreen) {
        m_videoPanel->ToggleFullscreen();
        evt.Skip(false);
        return;
      }
      // Если fullscreen выключен – включаем только на Video
      else if (isVideoPage) {
        m_videoPanel->ToggleFullscreen();
        evt.Skip(false);
        return;
      }
    }
  }

  evt.Skip();
}

bool MainFrame::IsVideoPageActive() const {
  if (!m_videoPanel || m_videoPageIdx == wxNOT_FOUND)
    return false;
  return m_notebook->GetSelection() == m_videoPageIdx;
}

PlaylistManager *MainFrame::getPlaylistManager() const {
  return m_application ? m_application->getPlaylistManager() : nullptr;
}

ConfigManager *MainFrame::getConfigManager() const {
  return m_application ? m_application->getConfigManager() : nullptr;
}

ChannelCards *MainFrame::GetChannelCards() { return m_channelCards; }

void MainFrame::ApplyFullscreen(bool fs) {
  ShowFullScreen(fs, wxFULLSCREEN_ALL);

  // Обновляем отступы главного sizer’а
  int border = fs ? 0 : 6;
  if (m_mainSizerGaugeItem) {
    m_mainSizerGaugeItem->SetBorder(border);
  }
  if (m_mainSizerHeaderItem) {
    m_mainSizerHeaderItem->SetBorder(border);
  }
  if (m_mainSizerNotebookItem) {
    m_mainSizerNotebookItem->SetBorder(border);
  }
  if (m_mainPanel) {
    m_mainPanel->Layout();
    m_mainPanel->Refresh();
  }
}

void MainFrame::HandleEpgPageChanged(int sel) {
  if (sel != m_epgPageIdx)
    return;
  LOG_DEBUG("HandleEpgPageChanged: sel=%d", sel);
}

void MainFrame::OnEpgToggle(wxCommandEvent &) {
  if (m_videoPanel)
    m_videoPanel->SetTabActive(false);
  ToggleHeaderGroup(m_btnEpg);
  m_notebook->SetSelection(m_epgPageIdx);
}

void MainFrame::SwitchToEpgTab(const std::string &channelId,
                               const std::string &channelName) {
  if (!m_epgPanel)
    return;
  m_notebook->SetSelection(m_epgPageIdx);
  m_epgPanel->SetCurrentChannel(channelId, channelName);
  ToggleHeaderGroup(m_btnEpg);
}

void MainFrame::OnEPGUpdated(wxCommandEvent &event) {
  int status = event.GetInt();
  wxString error = event.GetString();
  UpdateEPGStatus(status, error);

  // Коалесцирование: запускаем таймер, если ещё не запущен
  if (!m_epgUpdatePending) {
    m_epgUpdatePending = true;
    m_epgCoalesceTimer.StartOnce(1000); // 1000 мс задержка
  }
}

void MainFrame::OnEpgCoalesceTimer(wxTimerEvent &) {
  m_epgUpdatePending = false;

  if (m_channelList) {
    m_channelList->RefreshProgramColumn();
  }
  if (m_channelCards) {
    m_channelCards->InvalidateAll();
    m_channelCards->Refresh();
  }
  if (m_favList) {
    m_favList->RefreshProgramColumn();
  }
  if (m_favCards) {
    m_favCards->InvalidateAll();
    m_favCards->Refresh();
  }
}

void MainFrame::UpdateEPGStatus(int status, const wxString &error) {
  wxString statusText;
  switch (status) {
  case EPG_STATUS_OK:
    statusText = "EPG updated";
    break;
  case EPG_STATUS_LOADING:
    statusText = "EPG loading...";
    break;
  case EPG_STATUS_ERROR:
    statusText = "EPG error: " + error;
    break;
  case EPG_STATUS_NO_SOURCES:
    statusText = "No EPG sources configured";
    break;
  default:
    statusText = "EPG status unknown";
  }
  // Используем поле 1 статусбара (второе поле)
  SetStatusText(statusText, 1);
}

void MainFrame::CleanupFinishedTasks() {
  m_backgroundTasks.erase(
      std::remove_if(m_backgroundTasks.begin(), m_backgroundTasks.end(),
                     [](std::future<void> &f) {
                       return f.valid() &&
                              f.wait_for(std::chrono::seconds(0)) ==
                                  std::future_status::ready;
                     }),
      m_backgroundTasks.end());
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

