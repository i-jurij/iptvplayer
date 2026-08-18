// src/MainFrame_ChannelsViewUI.cpp
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
  m_channelsHeader =
      new wxStaticText(m_channelsPage, wxID_ANY, "Playlist: - / Channels: 0");
  wxFont hFont = m_channelsHeader->GetFont();
  hFont.SetPointSize(12);
  hFont.SetWeight(wxFONTWEIGHT_BOLD);
  m_channelsHeader->SetFont(hFont);
  channelHeaderSizer->Add(m_channelsHeader, 1, wxALL, 0);

  // Toolbar
  m_viewToolBar = new wxToolBar(m_channelsPage, wxID_ANY, wxDefaultPosition,
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
  channelsSizer->Add(channelHeaderSizer, 0, wxEXPAND | wxALL, 12);

  // Фильтры
  createChannelsFilterPanel();
  if (m_filterPanel) {
    m_filterPanel->Reparent(m_channelsPage);
    channelsSizer->Add(m_filterPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    UpdateFilterPanelVisibility();
  }

  // Splitter
  wxSplitterWindow *channelsSplitter =
      new wxSplitterWindow(m_channelsPage, wxID_ANY, wxDefaultPosition,
                           wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
  channelsSplitter->SetMinimumPaneSize(200);

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

  m_epgChannels = new EPGPanel(channelsSplitter);
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

  channelsSizer->Add(channelsSplitter, 1, wxEXPAND | wxALL, 4);
  m_channelsPage->SetSizer(channelsSizer);
  m_channelsPage->Layout();
}

void MainFrame::HandleChannelPageChanged(int sel) {
  if (sel == m_channelsPageIdx) {
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
    if (m_channelViewBook->GetSelection() == 0 && m_channelList) {
      m_channelList->SetFocusFromKbd();
    } else if (m_channelViewBook->GetSelection() == 1 && m_channelCards) {
      m_channelCards->SetFocusIgnoringChildren();
    }
  } else {
    if (m_epgChannels)
      m_epgChannels->SetActive(false);
    if (m_channelList)
      m_channelList->PauseLogoLoading();
    if (m_channelCards)
      m_channelCards->PauseLogoLoading();
  }
}

void MainFrame::ApplyChannelsNoLogoToViews() {
  PROFILE_SCOPE("MainFrame::ApplyChannelsNoLogoToViews");

  ChannelCards *cards = GetChannelCards();
  if (cards) {
    cards->IncrementCacheVersion();
  }

  LogoCache::PauseLoading();
  IconManager::PauseLoading();
  if (cards) {
    cards->PauseLogoLoading();
  }

  if (m_channelList) {
    if (auto *bcl = dynamic_cast<BaseChannelList *>(m_channelList)) {
      bcl->PauseLogoLoading();
    }
  }
  if (m_favList) {
    if (auto *bfl = dynamic_cast<BaseChannelList *>(m_favList)) {
      bfl->PauseLogoLoading();
    }
  }

  if (cards) {
    cards->ClearAllCaches(true, true);
  }
  LogoCache::ClearMemory(true);
  IconManager::ClearMemory();

  if (cards) {
    cards->InvalidateAll();
    cards->RefreshCards();
  }
  if (m_channelList)
    m_channelList->Refresh();
  if (m_favCards) {
    m_favCards->InvalidateAll();
    m_favCards->RefreshCards();
  }
  if (m_favList)
    m_favList->Refresh();

  if (!m_channelsNoLogo) {
    LogoCache::ResumeLoading();
    IconManager::ResumeLoading();

    if (cards) {
      cards->ResumeLogoLoading();
      cards->WarmUpTiles();
    }

    if (m_channelList) {
      if (auto *bcl = dynamic_cast<BaseChannelList *>(m_channelList)) {
        bcl->ResumeLogoLoading();
      }
    }

    SetStatusText("Channel logos enabled.", 0);
  } else {
    SetStatusText("Channel logos disabled.", 0);
  }
}

void MainFrame::SetShowLogoFromSettings(bool show) {
  m_channelsNoLogo = !show;
  if (m_viewToolBar) {
    m_viewToolBar->ToggleTool(ID_SHOW_LOGO, show);
  }

  ApplyChannelsNoLogoToViews();
}

static void clearScaledCache(bool removeGrid = false, bool removeList = false) {
  std::vector<std::tuple<int, int, int>> remove;

  int normDpi = 96;
  if (wxTheApp && wxTheApp->GetTopWindow()) {
    normDpi = GetNormDPI(wxTheApp->GetTopWindow());
  }

  if (removeList) {
    int listLogoSize = GetDpiLogoSizeList(wxTheApp->GetTopWindow());
    if (listLogoSize > 0) {
      remove.emplace_back(listLogoSize, listLogoSize, normDpi);
    }
  }

  if (removeGrid) {
    auto [cardlogoW, cardlogoH] = ComputeLogoSizeForDPI(normDpi);
    if (cardlogoW > 0 && cardlogoH > 0) {
      remove.emplace_back(cardlogoW, cardlogoH, normDpi);
    }
  }

  if (remove.empty()) {
    return;
  }

  LogoCache::ClearScaledRemoveSizes(remove);
}

void MainFrame::ApplyInitialViewMode() {
  auto *cfg = getConfigManager();
  std::string mode =
      cfg ? cfg->getSetting("channels_view_mode", "grid") : "grid";
  bool startInGrid = (mode != "list");

  if (m_channelViewBook)
    m_channelViewBook->ChangeSelection(startInGrid ? 1 : 0);

  // Синхронизируем избранное
  if (m_favViewBook)
    m_favViewBook->ChangeSelection(startInGrid ? 1 : 0);

  if (startInGrid) {
    InitializeGridResources();
  } else {
    InitializeListResources();
  }

  // Обновляем toolbar'ы
  if (m_viewToolBar) {
    m_viewToolBar->ToggleTool(ID_VIEW_GRID, startInGrid);
    m_viewToolBar->ToggleTool(ID_VIEW_LIST, !startInGrid);
  }
  if (m_favToolBar) {
    m_favToolBar->ToggleTool(ID_FAV_VIEW_GRID, startInGrid);
    m_favToolBar->ToggleTool(ID_FAV_VIEW_LIST, !startInGrid);
  }
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
    clearScaledCache(false, true);
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

    clearScaledCache(true, false);

    m_channelCards->ClearAllCaches(/*clearLRU=*/true,
                                   /*clearTextLayout=*/false);
  }
}
