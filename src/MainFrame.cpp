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

MainFrame::MainFrame(Application* app)
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

        // Если уходим с Video
        if (m_videoPanel && m_videoPageIdx != wxNOT_FOUND &&
            sel != m_videoPageIdx) {
          m_videoPanel->SetTabActive(false);
        }

        // Если возвращаемся на Video — проверяем, что текущая страница
        // действительно тот самый m_videoPanel
        if (m_videoPanel && m_videoPageIdx != wxNOT_FOUND &&
            sel == m_videoPageIdx) {
          // дополнительная защита: убедимся, что notebook действительно хранит
          // тот же объект на этой позиции
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

        HandleChannelPageChanged(sel);
        HandleFavPageChanged(sel);
        HandlePlaylistPageChanged(sel);
      });
}

PlaylistManager *MainFrame::getPlaylistManager() const {
  return m_application ? m_application->getPlaylistManager() : nullptr;
}

ConfigManager *MainFrame::getConfigManager() const {
  return m_application ? m_application->getConfigManager() : nullptr;
}

ChannelCards *MainFrame::GetChannelCards() { return m_channelCards; }

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
    wxMilliSleep(100);
    wxYield();
  } catch (const std::exception &e) {
    wxMessageBox(
        wxString::Format("MainFrame dtor std::exception: %s", e.what()));
  } catch (...) {
    wxMessageBox("MainFrame dtor unknown exception");
  }
}

void MainFrame::ApplyFullscreen(bool fs) {
  ShowFullScreen(fs, wxFULLSCREEN_ALL);
}
