#include "EventIDs.h"
#include "FavoritesCards.h"
#include "FavoritesList.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Profiler.h"
#include "Utils.h"
#include "VP_SvgIcon.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/toolbar.h>

void MainFrame::createFavoritesUI() {
  PROFILE_SCOPE("MainFrame::createFavoritesUI");

  if (!m_favoritesPanel)
    return;

  m_favoritesPanel->DestroyChildren();

  wxBoxSizer *favSizer = new wxBoxSizer(wxVERTICAL);

  // Заголовок
  wxBoxSizer *favHeaderSizer = new wxBoxSizer(wxHORIZONTAL);
  m_favHeader =
      new wxStaticText(m_favoritesPanel, wxID_ANY, _U("Favorites: 0 channels"));
  wxFont favFont = m_favHeader->GetFont();
  favFont.SetPointSize(12);
  favFont.SetWeight(wxFONTWEIGHT_BOLD);
  m_favHeader->SetFont(favFont);
  favHeaderSizer->Add(m_favHeader, 1, wxALL, 0);
  favHeaderSizer->AddStretchSpacer(1);

#if defined(__WXMSW__)
  m_favToolBar = new wxAuiToolBar(m_favoritesPanel, wxID_ANY,
                                  wxDefaultPosition, wxDefaultSize,
                                  wxAUI_TB_DEFAULT_STYLE);
  const auto favRadioKind = wxITEM_CHECK;
#else
  m_favToolBar = new wxToolBar(m_favoritesPanel, wxID_ANY);
  const auto favRadioKind = wxITEM_RADIO;
#endif
  wxBitmapBundle iconFavList = LoadSvgIcon("list", this);
  wxBitmapBundle iconFavGrid = LoadSvgIcon("grid", this);

  m_favToolBar->AddTool(ID_FAV_VIEW_LIST, _U("List"),
                        iconFavList.IsOk() ? iconFavList : wxNullBitmap,
                        _U("List view"), favRadioKind);
  m_favToolBar->AddTool(ID_FAV_VIEW_GRID, _U("Grid"),
                        iconFavGrid.IsOk() ? iconFavGrid : wxNullBitmap,
                        _U("Grid view"), favRadioKind);
  m_favToolBar->Realize();
  m_favToolBar->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
  favHeaderSizer->Add(m_favToolBar, 0, wxALL, 0);

  favSizer->Add(favHeaderSizer, 0, wxEXPAND | wxALL, 12);

  // Фильтры избранного
  wxPanel *favFilterPanel = new wxPanel(m_favoritesPanel, wxID_ANY);
  favFilterPanel->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
  wxBoxSizer *favFilterSizer = new wxBoxSizer(wxHORIZONTAL);

  m_favGroupChoice = new wxChoice(favFilterPanel, wxID_ANY);
  m_favCountryChoice = new wxChoice(favFilterPanel, wxID_ANY);
  m_favLangChoice = new wxChoice(favFilterPanel, wxID_ANY);
  m_favSortChoice = new wxChoice(favFilterPanel, wxID_ANY);

  // Индекс 0 = "показать все". Логика фильтрации читает GetSelection()
  // (см. ApplyFavoritesFiltersAndSort).
  m_favGroupChoice->Append(_U("All groups"));
  m_favCountryChoice->Append(_U("All countries"));
  m_favLangChoice->Append(_U("All languages"));

  // Порядок и подписи задаются kSortOrder и SortModeLabel() — те же,
  // что в Channels. Здесь ничего про конкретные значения знать не нужно.
  for (SortMode m : kSortOrder)
    m_favSortChoice->Append(SortModeLabel(m));

  m_favGroupChoice->SetSelection(0);
  m_favCountryChoice->SetSelection(0);
  m_favLangChoice->SetSelection(0);
  m_favSortChoice->SetSelection(0);

  favFilterSizer->Add(m_favGroupChoice, 0, wxRIGHT, FromDIP(8));
  favFilterSizer->Add(m_favCountryChoice, 0, wxRIGHT, FromDIP(8));
  favFilterSizer->Add(m_favLangChoice, 0, wxRIGHT, FromDIP(8));
  favFilterSizer->Add(new wxStaticText(favFilterPanel, wxID_ANY, _U("Sort:")), 0,
                      wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(6));
  favFilterSizer->Add(m_favSortChoice, 0, wxRIGHT, FromDIP(8));
  m_favFilterResetBtn = new wxButton(favFilterPanel, wxID_ANY, _U("Reset"));
  favFilterSizer->Add(m_favFilterResetBtn, 0, wxLEFT, FromDIP(8));

  favFilterPanel->SetSizer(favFilterSizer);
  favFilterPanel->Layout();
  favSizer->Add(favFilterPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                FromDIP(8));

  // Splitter
  wxSplitterWindow *favSplitter =
      new wxSplitterWindow(m_favoritesPanel, wxID_ANY, wxDefaultPosition,
                           wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
  favSplitter->SetMinimumPaneSize(200);

  m_favViewBook = new wxSimplebook(favSplitter, wxID_ANY);
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

  auto *cfg = getConfigManager();
  bool favStartGrid = m_startInGrid;

  m_favViewBook->AddPage(m_favList, "List");
  m_favViewBook->AddPage(m_favCards, "Cards");
  m_favViewBook->ChangeSelection(favStartGrid ? 1 : 0);

  m_favToolBar->ToggleTool(ID_FAV_VIEW_GRID, favStartGrid);
  m_favToolBar->ToggleTool(ID_FAV_VIEW_LIST, !favStartGrid);
#if defined(__WXMSW__)
  m_favToolBar->Refresh();
#endif

  m_epgFavorites = new EPGPanel(favSplitter, this);
  favSplitter->SplitVertically(m_favViewBook, m_epgFavorites);

  // --- Восстановление позиции сплиттера (корректные методы ConfigManager) ---
  bool userAdjustedFav = false;
  if (cfg) {
    std::string val = cfg->getSetting("favorites_split_user_adjusted", "false");
    userAdjustedFav = (val == "true");
  }

  if (cfg && userAdjustedFav) {
    int savedPos = cfg->getInt("favorites_split_pos", -1);
    if (savedPos > 0) {
      favSplitter->SetSashPosition(savedPos);
    }
  } else {
    CallAfter([favSplitter]() {
      int totalWidth = favSplitter->GetClientSize().GetWidth();
      if (totalWidth > 0)
        favSplitter->SetSashPosition(totalWidth / 2);
    });
  }

  favSplitter->Bind(wxEVT_SIZE, [favSplitter, cfg](wxSizeEvent &evt) {
    if (cfg) {
      bool userAdjusted =
          (cfg->getSetting("favorites_split_user_adjusted", "false") == "true");
      if (!userAdjusted) {
        int totalWidth = favSplitter->GetClientSize().GetWidth();
        if (totalWidth > 0)
          favSplitter->SetSashPosition(totalWidth / 2);
      }
    }
    evt.Skip();
  });

  favSplitter->Bind(
      wxEVT_SPLITTER_SASH_POS_CHANGED, [cfg](wxSplitterEvent &evt) {
        if (cfg) {
          cfg->setInt("favorites_split_pos", evt.GetSashPosition());
          cfg->setSetting("favorites_split_user_adjusted", "true");
        }
        evt.Skip();
      });

  favSizer->Add(favSplitter, 1, wxEXPAND | wxALL, 4);
  m_favoritesPanel->SetSizer(favSizer);
  m_favoritesPanel->Layout();

  // Привязка событий фильтров
  auto bindFavChoice = [&](wxChoice *c) {
    if (!c)
      return;
    c->Bind(wxEVT_CHOICE,
            [this](wxCommandEvent &) { ApplyFavoritesFiltersAndSort(); });
  };
  bindFavChoice(m_favGroupChoice);
  bindFavChoice(m_favCountryChoice);
  bindFavChoice(m_favLangChoice);
  bindFavChoice(m_favSortChoice);

  m_favFilterResetBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    if (m_favGroupChoice)
      m_favGroupChoice->SetSelection(0);
    if (m_favCountryChoice)
      m_favCountryChoice->SetSelection(0);
    if (m_favLangChoice)
      m_favLangChoice->SetSelection(0);
    if (m_favSortChoice)
      m_favSortChoice->SetSelection(0);
    ApplyFavoritesFiltersAndSort();
  });

  CallAfter([this]() {
    refreshFavorites();
#if defined(__WXMSW__)
    if (!m_favViewBook)
      return;
    if (m_favViewBook->GetSelection() == 0 && m_favList) {
      m_favList->SetFocusFromKbd();
    } else if (m_favCards) {
      m_favCards->SetFocusIgnoringChildren();
    }
#endif
  });
}

void MainFrame::ApplyFavoritesFiltersAndSort() {
  auto &fm = getApplication()->getFavoritesManager();
  auto favList = fm.list();

  std::vector<Channel> out;
  out.reserve(favList.size());

  // Read UI state by index. Index 0 = "все"; индексы > 0 = реальные
  // значения каналов. Нельзя сравнивать с отображаемой строкой: после
  // локализации она зависит от языка и фильтр молча сломается.
  int selGroup   = m_favGroupChoice   ? m_favGroupChoice->GetSelection()   : 0;
  int selCountry = m_favCountryChoice ? m_favCountryChoice->GetSelection() : 0;
  int selLang    = m_favLangChoice    ? m_favLangChoice->GetSelection()    : 0;
  int sortIdx    = m_favSortChoice    ? m_favSortChoice->GetSelection()    : 0;

  // Normalize wxNOT_FOUND (-1).
  if (selGroup   < 0) selGroup   = 0;
  if (selCountry < 0) selCountry = 0;
  if (selLang    < 0) selLang    = 0;
  if (sortIdx    < 0 ||
      sortIdx >= static_cast<int>(std::size(kSortOrder)))
    sortIdx = 0;

  // Pre-resolve real filter values (once, not per channel).
  wxString groupFilter, countryFilter, langFilter;
  if (m_favGroupChoice   && selGroup   > 0)
    groupFilter   = m_favGroupChoice->GetString(selGroup);
  if (m_favCountryChoice && selCountry > 0)
    countryFilter = m_favCountryChoice->GetString(selCountry);
  if (m_favLangChoice    && selLang    > 0)
    langFilter    = m_favLangChoice->GetString(selLang);

  // --- FILTERING ---
  for (const auto &c : favList) {
    if (selGroup > 0 &&
        wxString::FromUTF8(c.getGroupTitle()) != groupFilter)
      continue;

    if (selCountry > 0 &&
        wxString::FromUTF8(c.getCountry()) != countryFilter)
      continue;

    if (selLang > 0 &&
        wxString::FromUTF8(c.getLanguage()) != langFilter)
      continue;

    out.push_back(c);
  }

  // --- SORTING (stable for cards) ---
  auto cmpNameAsc = [](const Channel &a, const Channel &b) {
    return a.getName() < b.getName();
  };
  auto cmpNameDesc = [](const Channel &a, const Channel &b) {
    return a.getName() > b.getName();
  };
  auto cmpGroupAsc = [](const Channel &a, const Channel &b) {
    return a.getGroupTitle() < b.getGroupTitle();
  };
  auto cmpGroupDesc = [](const Channel &a, const Channel &b) {
    return a.getGroupTitle() > b.getGroupTitle();
  };
  auto cmpCountryAsc = [](const Channel &a, const Channel &b) {
    return a.getCountry() < b.getCountry();
  };
  auto cmpCountryDesc = [](const Channel &a, const Channel &b) {
    return a.getCountry() > b.getCountry();
  };
  auto cmpLangAsc = [](const Channel &a, const Channel &b) {
    return a.getLanguage() < b.getLanguage();
  };
  auto cmpLangDesc = [](const Channel &a, const Channel &b) {
    return a.getLanguage() > b.getLanguage();
  };

  const SortMode mode = kSortOrder[sortIdx];

  std::stable_sort(out.begin(), out.end(),
                   [&](const Channel &a, const Channel &b) {
                     switch (mode) {
                       case SortMode::NameAsc:     return cmpNameAsc(a, b);
                       case SortMode::NameDesc:    return cmpNameDesc(a, b);
                       case SortMode::GroupAsc:    return cmpGroupAsc(a, b);
                       case SortMode::GroupDesc:   return cmpGroupDesc(a, b);
                       case SortMode::CountryAsc:  return cmpCountryAsc(a, b);
                       case SortMode::CountryDesc: return cmpCountryDesc(a, b);
                       case SortMode::LangAsc:     return cmpLangAsc(a, b);
                       case SortMode::LangDesc:    return cmpLangDesc(a, b);
                     }
                     return cmpNameAsc(a, b); // недостижимо, защита от -Wswitch
                   });

  // --- CLEAR CACHES (same as channels) ---
  if (m_favCards)
    m_favCards->ClearAllCaches(true, true);

  if (m_favList)
    m_favList->PauseLogoLoading();

  LogoCache::ClearMemory();

  // --- APPLY TO LIST ---
  if (m_favList) {
    m_favList->loadChannels(out);

    wxTheApp->CallAfter([this]() {
      if (m_favList)
        m_favList->ResumeLogoLoading();
    });
  }

  // --- APPLY TO CARDS ---
  if (m_favCards)
    m_favCards->SetChannels(out);

  // --- APPLY SORTING TO LIST MODEL ---
  // Колонки модели: 2 = Name, 4 = Group, 5 = Language, 6 = Country.
  if (m_favList && m_favList->GetModel()) {
    int col = 2;
    bool asc = true;

    switch (mode) {
      case SortMode::NameAsc:     col = 2; asc = true;  break;
      case SortMode::NameDesc:    col = 2; asc = false; break;
      case SortMode::GroupAsc:    col = 4; asc = true;  break;
      case SortMode::GroupDesc:   col = 4; asc = false; break;
      case SortMode::CountryAsc:  col = 6; asc = true;  break;
      case SortMode::CountryDesc: col = 6; asc = false; break;
      case SortMode::LangAsc:     col = 5; asc = true;  break;
      case SortMode::LangDesc:    col = 5; asc = false; break;
    }

    m_favList->GetModel()->SetSorting(col, asc);
  }
}

void MainFrame::HandleFavPageChanged(int sel) {
  if (sel == m_favoritesPageIdx) {
    if (m_epgFavorites)
      m_epgFavorites->SetActive(true);
    if (m_epgChannels)
      m_epgChannels->SetActive(false);

    // --- Загрузка маппинга для избранных ---
    Application *app = static_cast<Application *>(wxTheApp);
    if (app) {
      EPGManager *epg = app->GetEPGManager();
      if (epg) {
        auto &fm = getApplication()->getFavoritesManager();
        auto favChannels = fm.list();
        if (!favChannels.empty()) {
          bool loaded = epg->LoadMappingForPlaylist(
              EPGManager::FAVORITES_PLAYLIST_ID, favChannels);
          // LoadMappingForPlaylist может вернуть true с ПУСТЫМ mapping
          // (metadata есть, строк нет). Проверяем содержимое БД напрямую.
          bool hasMapping = false;
          {
            auto favMap = epg->GetAllMappingsForPlaylist(
                EPGManager::FAVORITES_PLAYLIST_ID);
            hasMapping = !favMap.empty();
          }
          LOG_DEBUG("HandleFavPageChanged: loaded=%d hasMapping=%d",
                    static_cast<int>(loaded), static_cast<int>(hasMapping));
          if ((!loaded || !hasMapping) && epg->IsLoaded()) {
            LOG_DEBUG("HandleFavPageChanged: no favorites mapping, "
                      "triggering MatchFavoritesAsync(true)");
            epg->MatchFavoritesAsync(true);
          }
        }
      }
    }

    bool grid = m_startInGrid;

    if (m_favToolBar) {
      m_favToolBar->ToggleTool(ID_FAV_VIEW_GRID, grid);
      m_favToolBar->ToggleTool(ID_FAV_VIEW_LIST, !grid);
    }

    if (grid && m_favCards) {
      m_favCards->ResumeLogoLoading();
    } else if (!grid && m_favList) {
      m_favList->ResumeLogoLoading();
    }

    m_favViewBook->ChangeSelection(grid ? 1 : 0);
    if (grid) {
      if (m_favCards)
        m_favCards->SetFocusIgnoringChildren();
    } else {
      if (m_favList)
        m_favList->SetFocusFromKbd();
    }
    RestoreFavoriteSelection();

    // Явно перезагружаем EPG для текущего канала в избранном:
    //  - если маппинг изменился/очистился — Details сбросятся;
    //  - если данные обновились в фоне — обновятся.
    if (m_epgFavorites && m_epgFavorites->HasChannel()) {
      m_epgFavorites->LoadProgramsForChannel(
          m_epgFavorites->GetCurrentChannelId(),
          m_epgFavorites->GetCurrentDate());
    }
  } else {
    if (m_epgFavorites)
      m_epgFavorites->SetActive(false);
    if (m_favList)
      m_favList->PauseLogoLoading();
    if (m_favCards)
      m_favCards->PauseLogoLoading();
  }
}

void MainFrame::RestoreFavoriteSelection() {
  if (!m_hasLastSelectedFavorite)
    return;

  bool restored = false;
  if (m_favViewBook) {
    int sel = m_favViewBook->GetSelection();
    if (sel == 0 && m_favList) {
      restored = m_favList->SelectChannel(m_lastSelectedFavorite);
    } else if (sel == 1 && m_favCards) {
      const auto &channels = m_favCards->GetChannels();
      for (size_t i = 0; i < channels.size(); ++i) {
        if (channels[i].getName() == m_lastSelectedFavorite.getName() &&
            channels[i].getPlaylistName() ==
                m_lastSelectedFavorite.getPlaylistName()) {
          m_favCards->SelectCard((int)i);
          restored = true;
          break;
        }
      }
    }
  }

  if (restored && m_epgFavorites && m_epgFavorites->IsActive()) {
    m_epgFavorites->SetChannel(m_lastSelectedFavorite);
  }
}
