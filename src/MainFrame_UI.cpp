// src/MainFrame_UI.cpp
#include "ConfigManager.h"
#include "EventIDs.h"
#include "IconManager.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Playlist.h"
#include "PlaylistManager.h"
#include "Profiler.h"
#include "Utils.h"
#include "VP_SvgIcon.h"
#include "VideoPanel.h"

#include <thread>
#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/gauge.h>
#include <wx/sizer.h>

namespace {
  constexpr int TITLE_FONT_SIZE = 12;
} // namespace

static wxGauge *makeGauge(wxWindow *parent) {
  return new wxGauge(parent, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize,
                     wxGA_HORIZONTAL | wxGA_SMOOTH);
}

void MainFrame::ToggleHeaderGroup(wxToggleButton *active) {
  wxToggleButton *buttons[] = {m_btnPlaylists, m_btnChannels, m_btnFavorites,
                               m_btnVideo, m_btnEpg};

  for (auto *b : buttons) {
    if (!b)
      continue;
    b->SetValue(b == active);
  }
}

void MainFrame::ToggleHeaderGroup(int index) {
  switch (index) {
  case 0:
    ToggleHeaderGroup(m_btnPlaylists);
    break;
  case 1:
    ToggleHeaderGroup(m_btnChannels);
    break;
  case 2:
    ToggleHeaderGroup(m_btnFavorites);
    break;
  case 3:
    ToggleHeaderGroup(m_btnVideo);
    break;
  case 4:
    ToggleHeaderGroup(m_btnEpg);
    break;
  default:
    break;
  }
}

void MainFrame::ToggleHeaderGroup(const wxString &name) {
  if (name == "playlists")
    ToggleHeaderGroup(m_btnPlaylists);
  else if (name == "channels")
    ToggleHeaderGroup(m_btnChannels);
  else if (name == "favorites")
    ToggleHeaderGroup(m_btnFavorites);
  else if (name == "video")
    ToggleHeaderGroup(m_btnVideo);
  else if (name == "program")
    ToggleHeaderGroup(m_btnEpg);
}

void MainFrame::createMainPanel() {
  // -------------------------------
  // MAIN PANEL
  // -------------------------------
  m_mainPanel = new wxPanel(this, wxID_ANY);
  m_mainPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
  m_mainPanel->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});

  auto *mainSizer = new wxBoxSizer(wxVERTICAL);

  // --- create top global gauge at the very top of client area ---
  m_gaugeTop = makeGauge(m_mainPanel);
  m_gaugeTop->Hide();
  // add gauge first so it stays above headerPanel in the layout
  m_mainSizerGaugeItem =
      mainSizer->Add(m_gaugeTop, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

  // -------------------------------
  // NOTEBOOK
  // -------------------------------
  m_notebook = new wxAuiNotebook(m_mainPanel, wxID_ANY, wxDefaultPosition,
                                 wxDefaultSize, wxAUI_NB_DEFAULT_STYLE);
  m_notebook->SetTabCtrlHeight(0);
  m_notebook->SetBackgroundStyle(wxBG_STYLE_PAINT);
  m_notebook->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});

  // -------------------------------
  // HEADER PANEL
  // -------------------------------
  wxPanel *headerPanel = new wxPanel(m_mainPanel, wxID_ANY);
  headerPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
  headerPanel->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});

  auto *headerSizer = new wxBoxSizer(wxHORIZONTAL);

  auto makeButton = [&](const wxString &label, const wxString &svgName) {
    wxToggleButton *btn =
        new wxToggleButton(headerPanel, wxID_ANY, " " + label,
                           wxDefaultPosition, wxDefaultSize, wxBU_LEFT);

    wxBitmapBundle icon = LoadSvgIcon(svgName, this);
    if (icon.IsOk())
      btn->SetBitmap(icon, wxLEFT);

    return btn;
  };

  m_btnPlaylists = makeButton("Playlists", "playlists");
  m_btnChannels = makeButton("Channels", "channels");
  m_btnFavorites = makeButton("Favorites", "favorites");
  m_btnVideo = makeButton("Video", "video");
  m_btnEpg = makeButton("Program", "program");
  wxButton *btnMenu = new wxButton(headerPanel, wxID_ANY, " Menu");
  m_menuButton = btnMenu;
  wxBitmapBundle menuIcon = LoadSvgIcon("menu", this);
  if (menuIcon.IsOk()) {
    btnMenu->SetBitmap(menuIcon, wxLEFT);
  }

  headerSizer->Add(m_btnPlaylists, 0, wxALL, 8);
  headerSizer->AddSpacer(12);
  headerSizer->Add(m_btnChannels, 0, wxALL, 8);
  headerSizer->AddSpacer(12);
  headerSizer->Add(m_btnFavorites, 0, wxALL, 8);
  headerSizer->AddSpacer(12);
  headerSizer->Add(m_btnVideo, 0, wxALL, 8);
  headerSizer->AddSpacer(12);
  headerSizer->Add(m_btnEpg, 0, wxALL, 8);
  headerSizer->AddStretchSpacer(1);
  headerSizer->Add(btnMenu, 0, wxALL, 8);

  headerPanel->SetSizer(headerSizer);

  // -------------------------------
  // PLAYLIST PAGE
  // -------------------------------
  createPlaylistPanel();
  m_notebook->AddPage(m_playlistPanel, "");

  // -------------------------------
  // CHANNELS PAGE
  // -------------------------------
  m_channelsPage = new wxPanel(m_notebook, wxID_ANY);
  m_notebook->AddPage(m_channelsPage, "Channels");
  m_channelsPageIdx = m_notebook->FindPage(m_channelsPage);
  createChannelsView();

  // -------------------------------
  // FAVORITES PAGE
  // -------------------------------
  m_favoritesPanel = new wxPanel(m_notebook, wxID_ANY);
  m_notebook->AddPage(m_favoritesPanel, "Favorites");
  m_favoritesPageIdx = m_notebook->FindPage(m_favoritesPanel);
  createFavoritesUI();

  // -------------------------------
  // VIDEO PAGE
  // -------------------------------
  m_videoPanel = new VideoPanel(m_notebook);
  m_videoPanel->SetUIElementsToHide(headerPanel, m_gaugeTop);

  m_videoPanel->m_onRequestTabSwitch = [this](int index) {
    LOG_DEBUG("MainFrame: m_onRequestTabSwitch requested index=%d, current "
              "token=%llu",
              index, (unsigned long long)m_showPanelToken.load());

    // Если уже идёт программное переключение — игнорируем новый запрос
    if (m_ignoreNotebookEvents.load()) {
      LOG_DEBUG("m_onRequestTabSwitch: ignored because m_ignoreNotebookEvents "
                "is set");
      return;
    }

    // Захватываем токен, чтобы отложённые/асинхронные вызовы могли быть
    // инвалидацированы
    uint64_t token = m_showPanelToken.load();

    // Поднимаем флаг, чтобы синхронные обработчики страницы не выполняли
    // побочную логику
    m_ignoreNotebookEvents.store(true);

    // Выполняем переключение вкладки
    m_notebook->SetSelection(index);
    LOG_DEBUG("m_onRequestTabSwitch: SetSelection(%d) done, current=%d", index,
              m_notebook->GetSelection());

    // Асинхронно выполняем пост‑действия и снимаем флаг; проверяем токен на
    // инвалидацию
    CallAfter([this, index, token]() {
      LOG_DEBUG("MainFrame(CallAfter): handling requestTabSwitch index=%d, "
                "token=%llu",
                index, (unsigned long long)token);

      // Если токен изменился — кто-то инвалидировал показ панели, пропускаем
      // пост‑действия
      if (token != m_showPanelToken.load()) {
        LOG_DEBUG("m_onRequestTabSwitch(CallAfter): token invalidated, "
                  "skipping post actions (index=%d)",
                  index);
        m_ignoreNotebookEvents.store(false);
        return;
      }

      // Вызываем те же обработчики, что обычно срабатывают на событие страницы
      HandleChannelPageChanged(index);
      HandleFavPageChanged(index);
      HandlePlaylistPageChanged(index);

      // Обновляем заголовок
      ToggleHeaderGroup(index);

      // Снимаем блокировку — теперь внешние события снова обрабатываются
      m_ignoreNotebookEvents.store(false);
      LOG_DEBUG("m_onRequestTabSwitch(CallAfter): finished for index=%d",
                index);
    });
  };

  m_notebook->AddPage(m_videoPanel, "Video");
  m_videoPageIdx = m_notebook->FindPage(m_videoPanel);
  LOG_DEBUG("MainFrame: added Video page; m_videoPageIdx=%d (m_videoPanel=%p)",
            m_videoPageIdx, (void *)m_videoPanel);

  auto *cfg = getConfigManager();
  std::vector<std::string> recentRaw = cfg->getRecentFiles();

  std::vector<wxString> recentWx;
  recentWx.reserve(recentRaw.size());
  for (auto &s : recentRaw)
    recentWx.push_back(wxString::FromUTF8(s));

  m_videoPanel->SetRecentFiles(recentWx);

  m_videoPanel->m_onPlayerState = [this](const wxString &state) {
    if (!GetStatusBar())
      return;

    SetStatusText(state, 0);
  };

  m_videoPanel->m_onStreamInfo = [this](const wxString &info) {
    if (!GetStatusBar())
      return;

    SetStatusText(info, 1);
  };

  // -------------------------------
  // EPG PANEL
  // -------------------------------
  m_epgAdminPanel = new EpgSourceManagerPanel(
      m_notebook, m_application->GetEPGManager(), true);
  m_epgAdminPanel->SetMainFrame(this);
  m_notebook->AddPage(m_epgAdminPanel, "Program");
  m_epgPageIdx = m_notebook->FindPage(m_epgAdminPanel);

  // -------------------------------
  // HEADER BUTTONS bindings
  // -------------------------------
  m_btnPlaylists->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) {
    if (m_videoPanel)
      m_videoPanel->SetTabActive(false);

    ToggleHeaderGroup(m_btnPlaylists);
    m_notebook->SetSelection(0);
    HighlightLoadedPlaylistInList();
  });

  m_btnChannels->Bind(wxEVT_TOGGLEBUTTON,
                      [this](wxCommandEvent &) {
                        if (m_loadedPlaylistIndex < 0) {
                          wxMessageBox("Please select a playlist first.",
                                       "Info", wxOK | wxICON_INFORMATION, this);
                          m_btnChannels->SetValue(false);
                          return;
                        }

                        if (m_playlistUpdated) {
                          LogoCache::ClearPlaylist(m_loadedPlaylistName);
                          m_playlistUpdated = false;
                        }

                        if (m_videoPanel)
                          m_videoPanel->SetTabActive(false);

                        ToggleHeaderGroup(m_btnChannels);
                        m_notebook->SetSelection(m_channelsPageIdx);
                      });

  m_btnFavorites->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) {
    if (m_videoPanel)
      m_videoPanel->SetTabActive(false);

    ToggleHeaderGroup(m_btnFavorites);
    m_notebook->SetSelection(2);
  });

  m_btnVideo->Bind(wxEVT_TOGGLEBUTTON,
                   [this](wxCommandEvent &) {
                     ToggleHeaderGroup(m_btnVideo);
                     m_notebook->SetSelection(3);
                   });

  m_btnEpg->Bind(wxEVT_TOGGLEBUTTON, &MainFrame::OnEpgToggle, this);

  btnMenu->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { ShowMainMenu(); });

  Bind(wxEVT_ICONIZE, [this](wxIconizeEvent &) {
    if (m_videoPanel)
      m_videoPanel->SetTabActive(false);
  });

  Bind(wxEVT_MENU, &MainFrame::onSettings, this, ID_MENU_SETTINGS);
  Bind(wxEVT_MENU, &MainFrame::onAbout, this, ID_MENU_ABOUT);
  Bind(wxEVT_MENU, &MainFrame::onQuit, this, ID_MENU_EXIT);

  // -------------------------------
  // SAVE VIEW MODE ON SWITCH (CHANNELS)
  // -------------------------------
  Bind(
      wxEVT_TOOL,
      [this](wxCommandEvent &evt) {
        auto *cfg = getConfigManager();
        bool isGrid = (evt.GetId() == ID_VIEW_GRID);
        std::string mode = isGrid ? "grid" : "list";

        if (isGrid) {
          TeardownListResources();
          InitializeGridResources();
          if (m_channelViewBook)
            m_channelViewBook->ChangeSelection(1);
        } else {
          TeardownGridResources();
          InitializeListResources();
          if (m_channelViewBook)
            m_channelViewBook->ChangeSelection(0);
        }

        cfg->setSetting("channels_view_mode", mode);
        cfg->saveSettings();

        if (m_favViewBook) {
          int favSel = isGrid ? 1 : 0;
          m_favViewBook->ChangeSelection(favSel);
          cfg->setSetting("favorites_view_mode", mode);
        }

        if (m_viewToolBar) {
          m_viewToolBar->ToggleTool(ID_VIEW_GRID, isGrid);
          m_viewToolBar->ToggleTool(ID_VIEW_LIST, !isGrid);
        }
        if (m_favToolBar) {
          m_favToolBar->ToggleTool(ID_FAV_VIEW_GRID, isGrid);
          m_favToolBar->ToggleTool(ID_FAV_VIEW_LIST, !isGrid);
        }

        CallAfter([this, isGrid]() {
          if (isGrid) {
            if (m_channelCards) {
              m_channelCards->RefreshCards();
              m_channelCards->SetFocusIgnoringChildren();
            }
          } else {
            if (m_channelList) {
              m_channelList->SetFocusFromKbd();
            }
          }
          UpdateFilterPanelVisibility();
        });

        evt.Skip();
      },
      ID_VIEW_LIST, ID_VIEW_GRID);

  // -------------------------------
  // LOGO hide show binding
  // -------------------------------
  m_viewToolBar->Bind(
      wxEVT_TOOL,
      [this](wxCommandEvent &evt) {
        if (evt.GetId() != ID_SHOW_LOGO)
          return;

        bool newShowState = m_viewToolBar->GetToolState(ID_SHOW_LOGO);

        const wxString warnTitle = "Channel logo warning";
        const wxString warnMsg =
            "Playlists larger than 1,000 channels may use a lot of RAM.\n"
            "Logo in list view wiil be show after restart.";

        auto *cfg = getConfigManager();

        if (newShowState) {
          wxMessageDialog dlg(this, warnMsg, warnTitle,
                              wxOK | wxCANCEL | wxICON_WARNING);
          int answer = dlg.ShowModal();
          if (answer == wxID_OK) {
            m_channelsNoLogo = false;
            if (cfg) {
              cfg->setSetting("nologo", "false");
              cfg->saveSettings();
            }
            ApplyChannelsNoLogoToViews();
          } else {
            m_viewToolBar->ToggleTool(ID_SHOW_LOGO, false);
          }
        } else {
          wxDialog dlg(this, wxID_ANY, "Delete logo cache?", wxDefaultPosition,
                       wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

          wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
          wxStaticText *msg =
              new wxStaticText(&dlg, wxID_ANY, "You disabled channel logos.");
          msg->Wrap(420);
          top->Add(msg, 0, wxALL | wxEXPAND, FromDIP(12));

          wxCheckBox *delChk = new wxCheckBox(
              &dlg, wxID_ANY, "Delete locally cached logos (disk and memory)");
          top->Add(delChk, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

          wxSizer *btns = dlg.CreateButtonSizer(wxOK | wxCANCEL);
          if (btns)
            top->Add(btns, 0, wxALL | wxALIGN_RIGHT, FromDIP(8));

          dlg.SetSizerAndFit(top);
          dlg.CentreOnParent();

          int res = dlg.ShowModal();

          if (res == wxID_OK) {
            m_channelsNoLogo = true;
            if (cfg) {
              cfg->setSetting("nologo", "true");
              cfg->saveSettings();
            }
            ApplyChannelsNoLogoToViews();

            if (delChk->GetValue()) {
              int winId = GetId(); // сохраняем ID
              CleanupFinishedTasks();
              m_backgroundTasks.push_back(
                  std::async(std::launch::async, [winId]() {
                    // Фоновая очистка файлов (без доступа к UI)
                    ErrorCode ec = IconManager::DeleteAllIcons();
                    if (ec == ErrorCode::OK) {
                      LogoCache::ClearAll();
                    }
                    // Возврат в UI поток через CallAfter с проверкой
                    // существования окна
                    wxTheApp->CallAfter([winId, ec]() {
                      wxWindow *w = wxWindow::FindWindowById(winId);
                      if (!w)
                        return;
                      auto *mf = dynamic_cast<MainFrame *>(w);
                      if (!mf || mf->m_closing)
                        return;

                      ChannelCards *cards = mf->GetChannelCards();
                      if (cards) {
                        cards->ResumeLogoLoading();
                        cards->InvalidateAll();
                      }
                      if (ec == ErrorCode::OK) {
                        mf->SetStatusText(
                            "Logo cache deleted (disk and memory).", 0);
                      } else if (ec == ErrorCode::FileNotFound) {
                        mf->SetStatusText("Logo cache: nothing to delete.", 0);
                      } else {
                        mf->SetStatusText(
                            "Failed to delete logo cache. See log.", 0);
                      }
                    });
                  }));
            } else {
              // просто отключаем логотипы без удаления кэша
              m_channelsNoLogo = true;
              if (cfg) {
                cfg->setSetting("nologo", "true");
                cfg->saveSettings();
              }
              ApplyChannelsNoLogoToViews();
              SetStatusText("Channel logos disabled (cache preserved).", 0);
            }
          } else {
            m_viewToolBar->ToggleTool(ID_SHOW_LOGO, true);
          }
        }
      },
      ID_SHOW_LOGO);

  // -------------------------------
  // FAVORITES VIEW SWITCH
  // -------------------------------
  Bind(
      wxEVT_TOOL,
      [this](wxCommandEvent &evt) {
        auto *cfg = getConfigManager();
        bool isGrid = (evt.GetId() == ID_FAV_VIEW_GRID);
        std::string mode = isGrid ? "grid" : "list";

        if (m_favViewBook)
          m_favViewBook->ChangeSelection(isGrid ? 1 : 0);

        if (m_channelViewBook) {
          int channelSel = isGrid ? 1 : 0;
          m_channelViewBook->ChangeSelection(channelSel);
        }

        if (isGrid) {
          TeardownListResources();
          InitializeGridResources();
        } else {
          TeardownGridResources();
          InitializeListResources();
        }

        cfg->setSetting("favorites_view_mode", mode);
        cfg->setSetting("channels_view_mode", mode);
        cfg->saveSettings();

        if (m_favToolBar) {
          m_favToolBar->ToggleTool(ID_FAV_VIEW_GRID, isGrid);
          m_favToolBar->ToggleTool(ID_FAV_VIEW_LIST, !isGrid);
        }
        if (m_viewToolBar) {
          m_viewToolBar->ToggleTool(ID_VIEW_GRID, isGrid);
          m_viewToolBar->ToggleTool(ID_VIEW_LIST, !isGrid);
        }

        CallAfter([this]() { refreshFavorites(); });

        evt.Skip();
      },
      ID_FAV_VIEW_LIST, ID_FAV_VIEW_GRID);

  // -------------------------------
  // FINAL LAYOUT
  // -------------------------------
  m_mainSizerHeaderItem = mainSizer->Add(headerPanel, 0, wxEXPAND | wxALL, 6);
  m_mainSizerNotebookItem =
      mainSizer->Add(m_notebook, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  m_mainPanel->SetSizer(mainSizer);

  // Добавляем панель в sizer самого фрейма
  wxBoxSizer *frameSizer = new wxBoxSizer(wxVERTICAL);
  frameSizer->Add(m_mainPanel, 1, wxEXPAND);
  SetSizer(frameSizer);
  Layout(); // Принудительно пересчитать размеры
  
  ToggleHeaderGroup(m_btnPlaylists);

  CallAfter([this]() {
    RestoreLastOpenedPlaylist();
    CheckAndSuggestPlaylist();
  });

  LOG_DEBUG("MainFrame: pages indices: playlist=?, channels=%d, favorites=%d, "
            "video=%d",
            m_channelsPageIdx, m_favoritesPageIdx, m_videoPageIdx);
  
  int videoIdxFound = m_notebook->FindPage(m_videoPanel);
  LOG_DEBUG("MainFrame: m_videoPageIdx=%d videoIdxFound=%d m_videoPanel=%p",
            m_videoPageIdx, videoIdxFound, (void *)m_videoPanel);
}

void MainFrame::createStatusBar() {
  CreateStatusBar(2);
  int widths[] = {-2, -3}; // левое : правое = 2 : 3
  SetStatusWidths(2, widths);
  SetStatusText("Ready", 0);
  SetStatusText("No playlist loaded", 1);
}

void MainFrame::RestoreLastOpenedPlaylist() {
  auto *cfg = getConfigManager();
  auto *mgr = getPlaylistManager();
  if (!cfg || !mgr)
    return;

  std::string lastOpened = cfg->getSetting("last_opened_playlist", "");
  if (lastOpened.empty())
    return;

  const auto &pls = mgr->getPlaylists();
  if (pls.empty())
    return;

  for (size_t i = 0; i < pls.size(); ++i) {
    Playlist *pl = pls[i].get();
    if (!pl)
      continue;

    if (pl->getTitle() == lastOpened) {

      m_loadedPlaylistIndex = static_cast<int>(i);
      m_loadedPlaylistName = lastOpened;
      m_selectedPlaylistIndex = static_cast<int>(i);

      // загружаем каналы
      loadPlaylistChannels(pl->getChannels(),
                           wxString::FromUTF8(pl->getTitle()));

      // переключаем вкладку Channels
      ToggleHeaderGroup(m_btnChannels);
      m_notebook->SetSelection(m_channelsPageIdx);

      SetStatusText(wxString::Format("Playlist selected: %s",
                                     wxString::FromUTF8(pl->getTitle())),
                    1);

      return;
    }
  }
}

void MainFrame::showPanel(wxWindow *child) {
  if (!child || !m_notebook)
    return;

  wxWindow *cur = child;
  while (cur && cur->GetParent() != m_notebook)
    cur = cur->GetParent();

  if (cur) {
    int idx = m_notebook->FindPage(cur);
    if (idx != wxNOT_FOUND) {
      m_notebook->SetSelection(idx);
    }
  }

  Layout();
}

uint64_t MainFrame::InvalidateShowPanelToken() { return ++m_showPanelToken; }