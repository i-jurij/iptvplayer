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
                               m_btnVideo};

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
  wxButton *btnMenu = new wxButton(headerPanel, wxID_ANY, " Menu");
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
  wxPanel *channelsPage = new wxPanel(m_notebook, wxID_ANY);
  channelsPage->SetBackgroundStyle(wxBG_STYLE_PAINT);
  channelsPage->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});

  m_channelsPage = channelsPage;

  auto *channelsSizer = new wxBoxSizer(wxVERTICAL);

  auto *channelHeaderSizer = new wxBoxSizer(wxHORIZONTAL);
  m_channelsHeader =
      new wxStaticText(channelsPage, wxID_ANY, "Playlist: - / Channels: 0");
  auto hFont = m_channelsHeader->GetFont();
  hFont.SetPointSize(TITLE_FONT_SIZE);
  hFont.SetWeight(wxFONTWEIGHT_BOLD);
  m_channelsHeader->SetFont(hFont);
  channelHeaderSizer->Add(m_channelsHeader, 1, wxALL, 0);

  m_viewToolBar = new wxToolBar(channelsPage, wxID_ANY, wxDefaultPosition,
                                wxDefaultSize, wxTB_HORIZONTAL | wxNO_BORDER);
  wxBitmapBundle iconList = LoadSvgIcon("list", this);
  wxBitmapBundle iconGrid = LoadSvgIcon("grid", this);
  wxBitmapBundle iconLogo = LoadSvgIcon("showlogo", this);

  m_viewToolBar->AddTool(ID_VIEW_LIST, "List",
                         iconList.IsOk() ? iconList : wxNullBitmap, "List view",
                         wxITEM_RADIO);

  m_viewToolBar->AddTool(ID_VIEW_GRID, "Cards",
                         iconGrid.IsOk() ? iconGrid : wxNullBitmap,
                         "Cards view", wxITEM_RADIO);

  m_viewToolBar->AddSeparator();

  m_viewToolBar->AddTool(ID_SHOW_LOGO, "Logo",
                         iconLogo.IsOk() ? iconLogo : wxNullBitmap,
                         "Show channel logo", wxITEM_CHECK);

  m_viewToolBar->Realize();
  m_viewToolBar->ToggleTool(ID_SHOW_LOGO, !m_channelsNoLogo);
  m_viewToolBar->SetToolShortHelp(ID_SHOW_LOGO, "Show channel logo");

  channelHeaderSizer->Add(m_viewToolBar, 0, wxALL, 0);

  auto *channelHeaderOuterSizer = new wxBoxSizer(wxVERTICAL);
  channelHeaderOuterSizer->Add(channelHeaderSizer, 0, wxEXPAND, 0);

  createChannelsFilterPanel();
  if (m_filterPanel) {
    m_filterPanel->Reparent(m_channelsPage);
    channelHeaderOuterSizer->Add(m_filterPanel, 0,
                                 wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 8);
    UpdateFilterPanelVisibility();
  }

  channelsSizer->Add(channelHeaderOuterSizer, 0, wxEXPAND | wxALL, 12);

  createChannelsView();
  if (m_channelViewBook) {
    channelsSizer->Add(m_channelViewBook, 1, wxEXPAND | wxALL, 4);
  }

  channelsPage->SetSizer(channelsSizer);
  channelsPage->Layout();

  m_notebook->AddPage(channelsPage, "Channels");
  m_channelsPageIdx = m_notebook->FindPage(channelsPage);

  // -------------------------------
  // FAVORITES PAGE
  // -------------------------------
  wxPanel *favoritesPage = new wxPanel(m_notebook, wxID_ANY);
  favoritesPage->SetBackgroundStyle(wxBG_STYLE_PAINT);
  favoritesPage->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});

  auto *favSizer = new wxBoxSizer(wxVERTICAL);

  auto *favHeaderSizer = new wxBoxSizer(wxHORIZONTAL);
  m_favHeader =
      new wxStaticText(favoritesPage, wxID_ANY, "Favorites: 0 channels");
  auto favFont = m_favHeader->GetFont();
  favFont.SetPointSize(TITLE_FONT_SIZE);
  favFont.SetWeight(wxFONTWEIGHT_BOLD);
  m_favHeader->SetFont(favFont);
  favHeaderSizer->Add(m_favHeader, 1, wxALL, 0);
  favHeaderSizer->AddStretchSpacer(1);

  m_favToolBar = new wxToolBar(favoritesPage, wxID_ANY, wxDefaultPosition,
                               wxDefaultSize, wxTB_HORIZONTAL | wxNO_BORDER);

  m_favToolBar->AddTool(ID_FAV_VIEW_LIST, "List",
                        iconList.IsOk() ? iconList : wxNullBitmap, "List view",
                        wxITEM_RADIO);

  m_favToolBar->AddTool(ID_FAV_VIEW_GRID, "Grid",
                        iconGrid.IsOk() ? iconGrid : wxNullBitmap, "Grid view",
                        wxITEM_RADIO);

  m_favToolBar->Realize();

  favHeaderSizer->Add(m_favToolBar, 0, wxALL, 0);
  favSizer->Add(favHeaderSizer, 0, wxEXPAND | wxALL, 12);

  m_favViewBook = new wxSimplebook(favoritesPage, wxID_ANY);
  m_favViewBook->SetBackgroundStyle(wxBG_STYLE_PAINT);
  m_favViewBook->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});

  m_favList = new FavoritesList(m_favViewBook, wxID_ANY);
  m_favList->SetSelectCallback(
      [this](const Channel &ch, size_t index, const wxRect &rect) {
        this->onFavoriteSelected(ch, index, rect);
      });
  m_favCards = new FavoritesCards(m_favViewBook);
  m_favCards->SetSelectCallback(
      [this](const Channel &ch, size_t index, const wxRect &rect) {
        this->onFavoriteSelected(ch, index, rect);
      });

  m_favViewBook->AddPage(m_favList, "List");
  m_favViewBook->AddPage(m_favCards, "Cards");

  auto *cfgFav = getConfigManager();
  std::string favModeStr = cfgFav->getSetting("favorites_view_mode", "grid");
  wxString favMode = wxString::FromUTF8(favModeStr);

  if (favMode == "grid") {
    m_favViewBook->ChangeSelection(1);
    m_favToolBar->ToggleTool(ID_FAV_VIEW_GRID, true);
  } else {
    m_favViewBook->ChangeSelection(0);
    m_favToolBar->ToggleTool(ID_FAV_VIEW_LIST, true);
  }

  favSizer->Add(m_favViewBook, 1, wxEXPAND | wxALL, 4);
  favoritesPage->SetSizer(favSizer);
  m_notebook->AddPage(favoritesPage, "Favorites");
  m_favoritesPageIdx = m_notebook->FindPage(favoritesPage);
  CallAfter([this]() { refreshFavorites(); });

  // -------------------------------
  // VIDEO PAGE
  // -------------------------------
  m_videoPanel = new VideoPanel(m_notebook);
  m_videoPanel->SetUIElementsToHide(headerPanel, m_gaugeTop);

  m_videoPanel->m_onRequestTabSwitch = [this](int index) {
    LOG_DEBUG("m_onRequestTabSwitch: requested index=%d", index);

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

  m_videoPageIdx = m_notebook->AddPage(m_videoPanel, "Video");
  
  auto *cfg = getConfigManager();
  std::vector<std::string> recentRaw = cfg->getRecentFiles();

  std::vector<wxString> recentWx;
  recentWx.reserve(recentRaw.size());
  for (auto &s : recentRaw)
    recentWx.push_back(wxString::FromUTF8(s));

  m_videoPanel->SetRecentFiles(recentWx);
  
  m_videoPanel->m_onPlayerState = [this](const wxString &state) {
    wxTheApp->CallAfter([this, state]() {
      if (!GetStatusBar())
        return;

      SetStatusText(state, 0);
      GetStatusBar()->Refresh();
      GetStatusBar()->Update();
    });
  };
 
  m_videoPanel->m_onStreamInfo = [this](const wxString &info) {
    SetStatusText(info, 1);
  };


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

  btnMenu->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    if (m_videoPanel)
      m_videoPanel->SetTabActive(false);

    wxMenu menu;
    menu.Append(ID_MENU_SETTINGS, "Settings");
    menu.Append(ID_MENU_ABOUT, "About");
    menu.Append(ID_MENU_EXIT, "Quit");
    PopupMenu(&menu);
  });

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

            MainFrame *mf = this;
            if (delChk->GetValue()) {
              ChannelCards *cards = mf->GetChannelCards();
              std::thread([mf, cards]() {
                if (cards) {
                  cards->PauseLogoLoading();
                  cards->ClearAllCaches(true, true);
                }

                ErrorCode ec = IconManager::DeleteAllIcons();

                if (ec == ErrorCode::OK) {
                  LogoCache::ClearAll();
                  wxLogInfo("Background: icon files removed and in-memory "
                            "cache cleared.");
                } else if (ec == ErrorCode::FileNotFound) {
                  wxLogInfo("Background: no icon files found to remove.");
                } else {
                  wxLogError("Background: failed to remove icon files, code=%d",
                             static_cast<int>(ec));
                }

                wxTheApp->CallAfter([mf, ec]() {
                  if (mf) {
                    ChannelCards *c = mf->GetChannelCards();
                    if (c) {
                      c->ResumeLogoLoading();
                      c->InvalidateAll();
                    }

                    if (ec == ErrorCode::OK) {
                      mf->SetStatusText("Logo cache deleted (disk and memory).",
                                        0);
                    } else if (ec == ErrorCode::FileNotFound) {
                      mf->SetStatusText("Logo cache: nothing to delete.", 0);
                    } else if (ec == ErrorCode::UnsafePath) {
                      mf->SetStatusText(
                          "Logo cache deletion refused (unsafe path).", 0);
                      wxMessageBox("Refused to delete logo files: unsafe path "
                                   "detected. See log for details.",
                                   "Error", wxOK | wxICON_ERROR, mf);
                    } else {
                      mf->SetStatusText("Failed to delete logo cache. See log.",
                                        0);
                      wxMessageBox(
                          "Failed to delete logo files. See log for details.",
                          "Error", wxOK | wxICON_ERROR, mf);
                    }
                  }
                });
              }).detach();
            } else {
              if (mf)
                mf->SetStatusText("Channel logos disabled (cache preserved).",
                                  0);
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
  mainSizer->Add(headerPanel, 0, wxEXPAND | wxALL, 6);
  mainSizer->Add(m_notebook, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  m_mainPanel->SetSizer(mainSizer);

  ToggleHeaderGroup(m_btnPlaylists);

  CallAfter([this]() { RestoreLastOpenedPlaylist(); });
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