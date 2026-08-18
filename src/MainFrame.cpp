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

        // Сброс отложенной загрузки EPG
        m_epgDebounceTimer.Stop();
        m_epgPendingPanel = nullptr;
        
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

        if (sel == m_videoPageIdx) {
          if (m_epgChannels)
            m_epgChannels->SetActive(false);
          if (m_epgFavorites)
            m_epgFavorites->SetActive(false);
        }

        // --- Вызов обработчиков страниц ---
        HandleChannelPageChanged(sel);
        HandleFavPageChanged(sel);
        HandlePlaylistPageChanged(sel);

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

  m_epgProgressTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &MainFrame::OnEpgProgressTimer, this,
       m_epgProgressTimer.GetId());
  
  // Установка колбэка для EPGManager
  if (m_application && m_application->GetEPGManager()) {
    auto *epg = m_application->GetEPGManager();

    epg->SetOnRefreshStarted([this]() {
      if (m_gaugeTop) {
        m_gaugeTop->SetRange(100);
        m_gaugeTop->SetValue(0);
        m_gaugeTop->Show();
        m_epgProgressTimer.Start(200);
      }
    });

    epg->SetOnUpdateFinished([this](int status, const std::string &error) {
      wxCommandEvent evt;
      evt.SetInt(status);
      evt.SetString(wxString::FromUTF8(error));
      this->OnEPGUpdated(evt);
    });
  }

  m_epgDebounceTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &MainFrame::OnEpgDebounceTimer, this,
       m_epgDebounceTimer.GetId());
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

void MainFrame::OnEpgDebounceTimer(wxTimerEvent &) {
  if (m_epgPendingPanel) {
    m_epgPendingPanel->SetChannel(m_epgPendingChannel);
    m_epgPendingPanel = nullptr;
  }
}

void MainFrame::OnEpgProgressTimer(wxTimerEvent &) {
  auto *epg = m_application->GetEPGManager();
  if (!epg) {
    m_epgProgressTimer.Stop();
    return;
  }

  const auto &prog = epg->GetDownloadProgress();
  if (prog.abort.load()) {
    m_epgProgressTimer.Stop();
    if (m_gaugeTop)
      m_gaugeTop->Hide();
    return;
  }

  double total = prog.totalBytes.load();
  double downloaded = prog.downloadedBytes.load();

  if (total > 0 && m_gaugeTop) {
    int percent = static_cast<int>((downloaded / total) * 100);
    m_gaugeTop->SetValue(percent);
    SetStatusText(wxString::Format("Downloading EPG: %d%%", percent), 1);
  } else if (m_gaugeTop) {
    m_gaugeTop->Pulse();
    SetStatusText("Downloading EPG...", 1);
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

void MainFrame::OnEpgToggle(wxCommandEvent &) {
  if (m_videoPanel)
    m_videoPanel->SetTabActive(false);
  ToggleHeaderGroup(m_btnEpg);
  m_notebook->SetSelection(m_epgPageIdx);
}

void MainFrame::OnEPGUpdated(wxCommandEvent &event) {
  int status = event.GetInt();
  wxString error = event.GetString();

  m_epgProgressTimer.Stop();

  if (m_epgAdminPanel) {
    m_epgAdminPanel->UpdateSourceList();
    m_epgAdminPanel->SetRefreshing(false);
  }

  if (m_epgChannels && m_epgChannels->HasChannel()) {
    m_epgChannels->LoadProgramsForChannel(m_epgChannels->GetCurrentChannelId(),
                                          m_epgChannels->GetCurrentDate());
  }

  if (m_epgFavorites && m_epgFavorites->HasChannel()) {
    m_epgFavorites->LoadProgramsForChannel(
        m_epgFavorites->GetCurrentChannelId(),
        m_epgFavorites->GetCurrentDate());
  }

  if (m_gaugeTop) {
    m_gaugeTop->Hide();
    m_gaugeTop->SetValue(0);
  }

  if (status == EPG_STATUS_OK) {
    SetStatusText("EPG updated successfully", 0);
    SetStatusText("", 1);
  } else if (status == EPG_STATUS_ERROR) {
    SetStatusText("EPG update failed", 0);
    SetStatusText(error, 1);
  } else if (status == EPG_STATUS_NO_SOURCES) {
    SetStatusText("No EPG sources configured", 0);
    SetStatusText("", 1);
  } else {
    SetStatusText("", 0);
    SetStatusText("", 1);
  }

  event.Skip();
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

void MainFrame::PlayChannel(const Channel &ch) {
  if (!m_videoPanel)
    return;

  // Инкремент токена — предыдущие отложенные showPanel станут неактуальны
  uint64_t token = ++m_showPanelToken;

  // Запускаем воспроизведение
  m_videoPanel->PlayChannel(ch);

  // Планируем безопасное переключение: выполняем только если токен актуален
  CallAfter([this, token]() {
    // Проверка токена
    if (token != m_showPanelToken.load()) {
      LOG_DEBUG("PlayChannel::CallAfter: token stale, skipping showPanel");
      return;
    }

    // Проверяем состояние плеера
    if (!(m_videoPanel && m_videoPanel->m_playerController)) {
      LOG_DEBUG("PlayChannel::CallAfter: no videoPanel or playerController");
      return;
    }

    auto state = m_videoPanel->m_playerController->GetState();
    if (state != PlayerState::Playing) {
      LOG_DEBUG("PlayChannel::CallAfter: player not Playing (state=%d), "
                "skipping showPanel",
                (int)state);
      return;
    }

    // Показываем панель Video и синхронно обновляем UI
    showPanel(m_videoPanel);
    if (m_btnVideo) {
      m_btnVideo->SetValue(true);
      ToggleHeaderGroup(m_btnVideo);
    }

    // Форсируем обновление canvas и сообщаем backend о размере
    auto *area = m_videoPanel->GetVideoArea();
    if (area) {
      area->Show();
      area->Refresh();
      area->Update();
      area->SetFocus();

      int w = 0, h = 0;
      area->GetClientSize(&w, &h);
      if (w > 0 && h > 0 && m_videoPanel->m_playerController) {
        m_videoPanel->m_playerController->ResizeEmbeddedWindow(w, h);
      }
    }
  });
}
