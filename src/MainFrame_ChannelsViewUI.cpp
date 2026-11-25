#include "ChannelCards.h"
#include "ChannelList.h"
#include "ConfigManager.h"
#include "EventIDs.h"
#include "IconManager.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Profiler.h"
#include "Utils.h"
#include "VP_SvgIcon.h"

#include <wx/simplebook.h>
#include <wx/sizer.h>

void MainFrame::createChannelsView() {
  PROFILE_SCOPE("MainFrame::createChannelsView");

  if (!m_channelsPage)
    return;

  m_channelsPage->DestroyChildren();

  wxBoxSizer *channelsSizer = new wxBoxSizer(wxVERTICAL);

  // Заголовок
  wxBoxSizer *channelHeaderSizer = new wxBoxSizer(wxHORIZONTAL);
  m_channelsHeader = new wxStaticText(m_channelsPage, wxID_ANY, _U("Channels"));
  {
    wxFont hFont = m_channelsHeader->GetFont();
    hFont.SetPointSize(12);
    hFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_channelsHeader->SetFont(hFont);
  }
  channelHeaderSizer->Add(m_channelsHeader, 0, wxALIGN_CENTER_VERTICAL | wxLEFT,
                          FromDIP(6));

  m_channelsInfoLabel = new wxStaticText(m_channelsPage, wxID_ANY, "");
  {
    wxFont f = m_channelsInfoLabel->GetFont();
    f.SetPointSize(f.GetPointSize() - 1);
    m_channelsInfoLabel->SetFont(f);
  }
  channelHeaderSizer->Add(m_channelsInfoLabel, 0,
                          wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
  channelHeaderSizer->AddStretchSpacer(1);

  // Toolbar
#if defined(__WXMSW__)
  m_viewToolBar = new wxAuiToolBar(m_channelsPage, wxID_ANY,
                                   wxDefaultPosition, wxDefaultSize,
                                   wxAUI_TB_DEFAULT_STYLE);
  const auto radioKind = wxITEM_CHECK;
#else
  m_viewToolBar = new wxToolBar(m_channelsPage, wxID_ANY, wxDefaultPosition,
                                wxDefaultSize, wxTB_HORIZONTAL | wxNO_BORDER);
  const auto radioKind = wxITEM_RADIO;
#endif
  wxBitmapBundle iconList = LoadSvgIcon("list", this);
  wxBitmapBundle iconGrid = LoadSvgIcon("grid", this);

  m_viewToolBar->AddTool(ID_VIEW_LIST, _U("List"),
                         iconList.IsOk() ? iconList : wxNullBitmap,
                         _U("List view"), radioKind);
  m_viewToolBar->AddTool(ID_VIEW_GRID, _U("Cards"),
                         iconGrid.IsOk() ? iconGrid : wxNullBitmap,
                         _U("Cards view"), radioKind);
  m_viewToolBar->Realize();
  m_viewToolBar->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

  channelHeaderSizer->Add(m_viewToolBar, 0, wxALL, 0);

  channelHeaderSizer->SetMinSize(wxSize(-1, GetHeaderRowHeight()));
  channelsSizer->Add(channelHeaderSizer, 0, wxEXPAND | wxALL, FromDIP(8));

  // Фильтры
  createChannelsFilterPanel();
  if (m_filterPanel) {
    m_filterPanel->Reparent(m_channelsPage);
    channelsSizer->Add(m_filterPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                       FromDIP(8));
    UpdateFilterPanelVisibility();
  }

  // Splitter
  wxSplitterWindow *channelsSplitter =
      new wxSplitterWindow(m_channelsPage, wxID_ANY, wxDefaultPosition,
                           wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
  channelsSplitter->SetMinimumPaneSize(FromDIP(200));

  m_channelViewBook = new wxSimplebook(channelsSplitter, wxID_ANY);
  m_channelViewBook->SetBackgroundStyle(wxBG_STYLE_PAINT);
  m_channelViewBook->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});

  m_channelList = new ChannelList(m_channelViewBook, wxID_ANY);
  m_channelList->SetSelectCallback(
      [this](const Channel &ch, size_t index, const wxRect &rect) {
        this->onChannelSelected(ch, index, rect);
      });

  m_channelCards = new ChannelCards(m_channelViewBook);
  m_channelCards->SetSelectCallback(
      [this](const Channel &ch, size_t index, const wxRect &rect) {
        this->onChannelSelected(ch, index, rect);
      });

  auto *cfg = getConfigManager();
  std::string mode =
      cfg ? cfg->getSetting("channels_view_mode", "grid") : "grid";
  bool startInGrid = (mode != "list");

  m_channelViewBook->AddPage(m_channelList, "List");
  m_channelViewBook->AddPage(m_channelCards, "Cards");
  m_channelViewBook->ChangeSelection(startInGrid ? 1 : 0);

  ApplyInitialViewMode();
  UpdateFilterPanelVisibility();

  m_channelViewBook->Bind(wxEVT_COMMAND_BOOKCTRL_PAGE_CHANGED,
                          [this](wxBookCtrlEvent &evt) {
                            UpdateFilterPanelVisibility();

                            if (m_channelViewBook->GetSelection() == 0) {
                              if (m_channelList) {
                                m_channelList->SetFocusFromKbd();
                              }
                            } else {
                              if (m_channelCards) {
                                CallAfter([this]() {
                                  if (m_channelCards)
                                    m_channelCards->SetFocusIgnoringChildren();
                                });
                              }
                            }
                            evt.Skip();
                          });

  m_epgChannels = new EPGPanel(channelsSplitter, this);
  channelsSplitter->SplitVertically(m_channelViewBook, m_epgChannels);

  // --- Восстановление позиции сплиттера (корректные методы ConfigManager) ---
  bool userAdjusted = false;
  if (cfg) {
    std::string val = cfg->getSetting("channels_split_user_adjusted", "false");
    userAdjusted = (val == "true");
  }

  if (cfg && userAdjusted) {
    int savedPos = cfg->getInt("channels_split_pos", -1);
    if (savedPos > 0) {
      channelsSplitter->SetSashPosition(savedPos);
    }
  } else {
    CallAfter([channelsSplitter]() {
      int totalWidth = channelsSplitter->GetClientSize().GetWidth();
      if (totalWidth > 0)
        channelsSplitter->SetSashPosition(totalWidth / 2);
    });
  }

  // Автокоррекция при ресайзе (если пользователь не трогал)
  channelsSplitter->Bind(wxEVT_SIZE, [channelsSplitter, cfg](wxSizeEvent &evt) {
    if (cfg) {
      bool userAdjusted =
          (cfg->getSetting("channels_split_user_adjusted", "false") == "true");
      if (!userAdjusted) {
        int totalWidth = channelsSplitter->GetClientSize().GetWidth();
        if (totalWidth > 0)
          channelsSplitter->SetSashPosition(totalWidth / 2);
      }
    }
    evt.Skip();
  });

  // Сохранение позиции при ручном изменении
  channelsSplitter->Bind(
      wxEVT_SPLITTER_SASH_POS_CHANGED, [cfg](wxSplitterEvent &evt) {
        if (cfg) {
          cfg->setInt("channels_split_pos", evt.GetSashPosition());
          cfg->setSetting("channels_split_user_adjusted", "true");
        }
        evt.Skip();
      });

  channelsSizer->Add(channelsSplitter, 1, wxEXPAND | wxALL, FromDIP(4));
  m_channelsPage->SetSizer(channelsSizer);
  m_channelsPage->Layout();

#if defined(__WXMSW__)
  // wxAuiToolBar может получить фокус на первом tool, из-за чего кнопка
  // выглядит активной. Передаём фокус рабочему виджету.
  CallAfter([this]() {
    if (!m_channelViewBook)
      return;
    if (m_channelViewBook->GetSelection() == 0 && m_channelList) {
      m_channelList->SetFocusFromKbd();
    } else if (m_channelCards) {
      m_channelCards->SetFocusIgnoringChildren();
    }
  });
#endif
}

void MainFrame::HandleChannelPageChanged(int sel) {
  if (sel == m_channelsPageIdx) {
    if (m_loadedPlaylistIndex >= 0) {
      Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
      if (pl) {
        EPGManager *epg = getApplication()->GetEPGManager();
        if (epg) {
          std::string playlistId = pl->getUniqueId();
          if (!playlistId.empty()) {
            if (playlistId == m_lastMappingLoadedPlaylistId) {
              LOG_DEBUG("HandleChannelPageChanged: mapping for %s already "
                        "loaded, skipping",
                        playlistId.c_str());
            } else {
              epg->LoadMappingForPlaylist(playlistId, pl->getChannels());
              m_lastMappingLoadedPlaylistId = playlistId;
              LOG_DEBUG("HandleChannelPageChanged: Loaded mapping for %s",
                        playlistId.c_str());
            }
          }
        }
      }
    }
    if (m_epgChannels)
      m_epgChannels->SetActive(true);
    if (m_epgFavorites)
      m_epgFavorites->SetActive(false);

    if (m_channelViewBook) {
      int activeView = m_channelViewBook->GetSelection();
      if (activeView == 0 && m_channelList) {
        m_channelList->ResumeLogoLoading();
      } else if (activeView == 1 && m_channelCards) {
        m_channelCards->ResumeLogoLoading();
      }
    }
    if (m_channelViewBook)
      m_channelViewBook->ChangeSelection(m_startInGrid ? 1 : 0);
    if (m_viewToolBar) {
      m_viewToolBar->ToggleTool(ID_VIEW_GRID, m_startInGrid);
      m_viewToolBar->ToggleTool(ID_VIEW_LIST, !m_startInGrid);
    }

    if (m_channelViewBook->GetSelection() == 0 && m_channelList) {
      m_channelList->SetFocusFromKbd();
    } else if (m_channelViewBook->GetSelection() == 1 && m_channelCards) {
      m_channelCards->SetFocusIgnoringChildren();
    }
    RestoreChannelSelection();

    // lazy Live refresh для Xtream при первом входе на Channels.
    TriggerLiveLazyRefreshIfNeeded();
  } else {
    if (m_epgChannels)
      m_epgChannels->SetActive(false);
    if (m_channelList)
      m_channelList->PauseLogoLoading();
    if (m_channelCards)
      m_channelCards->PauseLogoLoading();
  }
}

void MainFrame::ApplyInitialViewMode() {
  bool startInGrid = m_startInGrid;

  if (m_channelViewBook)
    m_channelViewBook->ChangeSelection(startInGrid ? 1 : 0);

  if (startInGrid) {
    InitializeGridResources();
  } else {
    InitializeListResources();
  }

  if (m_viewToolBar) {
    m_viewToolBar->ToggleTool(ID_VIEW_GRID, startInGrid);
    m_viewToolBar->ToggleTool(ID_VIEW_LIST, !startInGrid);
  }

  ApplyLogosState();
}

void MainFrame::InitializeListResources() {
  if (m_listState == ChannelsViewState::Ready ||
      m_listState == ChannelsViewState::Initializing)
    return;

  m_listState = ChannelsViewState::Initializing;

  LogoCache::ResumeLoading();
  IconManager::ResumeLoading();

  m_listState = ChannelsViewState::Ready;

  if (m_channelList) {
    m_channelList->ResumeLogoLoading();
  }
}

void MainFrame::InitializeGridResources() {
  if (m_gridState == ChannelsViewState::Ready ||
      m_gridState == ChannelsViewState::Initializing)
    return;

  m_gridState = ChannelsViewState::Initializing;

  LogoCache::ResumeLoading();
  IconManager::ResumeLoading();

  m_gridState = ChannelsViewState::Ready;

  if (m_channelCards) {
    m_channelCards->InitLRULimits();
    m_channelCards->ResumeLogoLoading();
  }
}

void MainFrame::TeardownListResources() {
  if (m_listState == ChannelsViewState::Uninitialized ||
      m_listState == ChannelsViewState::Paused)
    return;

  m_listState = ChannelsViewState::Paused;

  if (m_channelList) {
    m_channelList->PauseLogoLoading();
    LogoCache::PauseLoading();
    IconManager::PauseLoading();
    LogoCache::ClearScaled();
  }
}

void MainFrame::TeardownGridResources() {
  if (m_gridState == ChannelsViewState::Uninitialized ||
      m_gridState == ChannelsViewState::Paused)
    return;

  m_gridState = ChannelsViewState::Paused;

  if (m_channelCards) {
    m_channelCards->PauseLogoLoading();
    m_channelCards->StopWarmUp();
    m_channelCards->IncrementCacheVersion();
    LogoCache::PauseLoading();
    IconManager::PauseLoading();
    LogoCache::ClearScaled();

    m_channelCards->ClearAllCaches(/*clearLRU=*/true,
                                   /*clearTextLayout=*/false);
  }
}

void MainFrame::RestoreChannelSelection() {
  if (!m_hasLastSelectedChannel) return;

  bool restored = false;
  if (m_channelViewBook) {
    int sel = m_channelViewBook->GetSelection();
    if (sel == 0) restored = RestoreListSelection(m_channelList, m_lastSelectedChannel);
    else if (sel == 1) restored = RestoreCardsSelection(m_channelCards, m_lastSelectedChannel);
  }

  if (restored && m_epgChannels && m_epgChannels->IsActive())
    m_epgChannels->SetChannel(m_lastSelectedChannel);
}
