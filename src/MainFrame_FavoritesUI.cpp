// src/MainFrame_FavoritesUI.cpp
#include "EventIDs.h"
#include "FavoritesCards.h"
#include "FavoritesList.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "VP_SvgIcon.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/toolbar.h>

#include <set>

void MainFrame::createFavoritesUI() {
  wxPanel *favoritesPage = new wxPanel(m_notebook, wxID_ANY);
  favoritesPage->SetBackgroundStyle(wxBG_STYLE_PAINT);
  favoritesPage->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});

  auto *favSizer = new wxBoxSizer(wxVERTICAL);

  // --- HEADER ---
  auto *favHeaderSizer = new wxBoxSizer(wxHORIZONTAL);
  m_favHeader =
      new wxStaticText(favoritesPage, wxID_ANY, "Favorites: 0 channels");

  auto favFont = m_favHeader->GetFont();
  favFont.SetPointSize(12);
  favFont.SetWeight(wxFONTWEIGHT_BOLD);
  m_favHeader->SetFont(favFont);

  favHeaderSizer->Add(m_favHeader, 1, wxALL, 0);
  favHeaderSizer->AddStretchSpacer(1);

  // --- TOOLBAR ---
  m_favToolBar = new wxToolBar(favoritesPage, wxID_ANY);
  wxBitmapBundle iconList = LoadSvgIcon("list", this);
  wxBitmapBundle iconGrid = LoadSvgIcon("grid", this);

  m_favToolBar->AddTool(ID_FAV_VIEW_LIST, "List",
                        iconList.IsOk() ? iconList : wxNullBitmap, "List view",
                        wxITEM_RADIO);

  m_favToolBar->AddTool(ID_FAV_VIEW_GRID, "Grid",
                        iconGrid.IsOk() ? iconGrid : wxNullBitmap, "Grid view",
                        wxITEM_RADIO);

  m_favToolBar->Realize();
  favHeaderSizer->Add(m_favToolBar, 0, wxALL, 0);

  favSizer->Add(favHeaderSizer, 0, wxEXPAND | wxALL, 12);

  // --- FILTER PANEL ---
  m_favFilterPanel = new wxPanel(favoritesPage, wxID_ANY);
  auto *favFilterSizer = new wxBoxSizer(wxHORIZONTAL);

  m_favGroupChoice = new wxChoice(m_favFilterPanel, wxID_ANY);
  m_favCountryChoice = new wxChoice(m_favFilterPanel, wxID_ANY);
  m_favLangChoice = new wxChoice(m_favFilterPanel, wxID_ANY);
  m_favSortChoice = new wxChoice(m_favFilterPanel, wxID_ANY);

  m_favGroupChoice->Append("All groups");
  m_favCountryChoice->Append("All countries");
  m_favLangChoice->Append("All languages");

  m_favSortChoice->Append("Name ▲");
  m_favSortChoice->Append("Name ▼");
  m_favSortChoice->Append("Group ▲");
  m_favSortChoice->Append("Group ▼");
  m_favSortChoice->Append("Country ▲");
  m_favSortChoice->Append("Country ▼");

  m_favGroupChoice->SetSelection(0);
  m_favCountryChoice->SetSelection(0);
  m_favLangChoice->SetSelection(0);
  m_favSortChoice->SetSelection(0);

  favFilterSizer->Add(m_favGroupChoice, 0, wxRIGHT, FromDIP(8));
  favFilterSizer->Add(m_favCountryChoice, 0, wxRIGHT, FromDIP(8));
  favFilterSizer->Add(m_favLangChoice, 0, wxRIGHT, FromDIP(8));
  favFilterSizer->Add(new wxStaticText(m_favFilterPanel, wxID_ANY, "Sort:"), 0,
                      wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(6));
  favFilterSizer->Add(m_favSortChoice, 0, wxRIGHT, FromDIP(8));
  favFilterSizer->AddStretchSpacer(1);
  m_favFilterResetBtn = new wxButton(m_favFilterPanel, wxID_ANY, "Reset");
  favFilterSizer->Add(m_favFilterResetBtn, 0, wxLEFT, FromDIP(8));

  m_favFilterPanel->SetSizer(favFilterSizer);
  favSizer->Add(m_favFilterPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

  // --- VIEW BOOK ---
  m_favViewBook = new wxSimplebook(favoritesPage, wxID_ANY);

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

  favSizer->Add(m_favViewBook, 1, wxEXPAND | wxALL, 4);

  favoritesPage->SetSizer(favSizer);
  m_notebook->AddPage(favoritesPage, "Favorites");
  m_favoritesPageIdx = m_notebook->FindPage(favoritesPage);

  // --- RESTORE VIEW MODE ---
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

  // --- FILL FILTERS FROM FAVORITES ---
  auto &fm = getApplication()->getFavoritesManager();
  auto favList = fm.list();

  std::set<wxString> groups;
  std::set<wxString> countries;
  std::set<wxString> languages;

  for (const auto &c : favList) {
    groups.insert(wxString::FromUTF8(c.getGroupTitle()));
    countries.insert(wxString::FromUTF8(c.getCountry()));
    languages.insert(wxString::FromUTF8(c.getLanguage()));
  }

  // заполняем списки
  m_favGroupChoice->Clear();
  m_favGroupChoice->Append("All groups");
  for (auto &g : groups)
    m_favGroupChoice->Append(g);

  m_favCountryChoice->Clear();
  m_favCountryChoice->Append("All countries");
  for (auto &ct : countries)
    m_favCountryChoice->Append(ct);

  m_favLangChoice->Clear();
  m_favLangChoice->Append("All languages");
  for (auto &lg : languages)
    m_favLangChoice->Append(lg);

  // выставляем дефолт
  m_favGroupChoice->SetSelection(0);
  m_favCountryChoice->SetSelection(0);
  m_favLangChoice->SetSelection(0);

  // --- FILTER EVENTS ---
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

  CallAfter([this]() { refreshFavorites(); });
}

void MainFrame::ApplyFavoritesFiltersAndSort() {
  auto &fm = getApplication()->getFavoritesManager();
  auto favList = fm.list();

  std::vector<Channel> out;
  out.reserve(favList.size());

  wxString selGroup =
      m_favGroupChoice ? m_favGroupChoice->GetStringSelection() : "All groups";
  wxString selCountry = m_favCountryChoice
                            ? m_favCountryChoice->GetStringSelection()
                            : "All countries";
  wxString selLang =
      m_favLangChoice ? m_favLangChoice->GetStringSelection() : "All languages";
  wxString sort =
      m_favSortChoice ? m_favSortChoice->GetStringSelection() : "Name ▲";

  // --- FILTERING ---
  for (const auto &c : favList) {
    if (selGroup != "All groups" &&
        wxString::FromUTF8(c.getGroupTitle()) != selGroup)
      continue;

    if (selCountry != "All countries" &&
        wxString::FromUTF8(c.getCountry()) != selCountry)
      continue;

    if (selLang != "All languages" &&
        wxString::FromUTF8(c.getLanguage()) != selLang)
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

  std::stable_sort(out.begin(), out.end(),
                   [&](const Channel &a, const Channel &b) {
                     if (sort == "Name ▲")
                       return cmpNameAsc(a, b);
                     if (sort == "Name ▼")
                       return cmpNameDesc(a, b);
                     if (sort == "Group ▲")
                       return cmpGroupAsc(a, b);
                     if (sort == "Group ▼")
                       return cmpGroupDesc(a, b);
                     if (sort == "Country ▲")
                       return cmpCountryAsc(a, b);
                     if (sort == "Country ▼")
                       return cmpCountryDesc(a, b);
                     if (sort == "Language ▲")
                       return cmpLangAsc(a, b);
                     if (sort == "Language ▼")
                       return cmpLangDesc(a, b);

                     return cmpNameAsc(a, b);
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

  // --- APPLY SORTING TO LIST MODEL (correct columns: 2/4/5/6) ---
  if (m_favList && m_favList->GetModel()) {
    int col = 2;
    bool asc = true;

    if (sort == "Name ▲") {
      col = 2;
      asc = true;
    } else if (sort == "Name ▼") {
      col = 2;
      asc = false;
    } else if (sort == "Group ▲") {
      col = 4;
      asc = true;
    } else if (sort == "Group ▼") {
      col = 4;
      asc = false;
    } else if (sort == "Country ▲") {
      col = 6;
      asc = true;
    } else if (sort == "Country ▼") {
      col = 6;
      asc = false;
    } else if (sort == "Language ▲") {
      col = 5;
      asc = true;
    } else if (sort == "Language ▼") {
      col = 5;
      asc = false;
    }

    m_favList->GetModel()->SetSorting(col, asc);
  }
}

void MainFrame::HandleFavPageChanged(int sel) {
  if (sel == m_favoritesPageIdx) {
    // Возобновляем загрузку для активного представления избранного
    auto *cfg = getConfigManager();
    std::string mode = cfg->getSetting("favorites_view_mode", "grid");
    bool grid = (mode == "grid");

    if (grid && m_favCards) {
      m_favCards->ResumeLogoLoading();
    } else if (!grid && m_favList) {
      m_favList->ResumeLogoLoading();
    }

    // Существующая логика смены вкладки и фокуса (без изменений)
    CallAfter([this, grid]() {
      LOG_DEBUG(
          "HandleFavPageChanged(CallAfter): applying fav view mode grid=%d",
          (int)grid);
      m_favViewBook->ChangeSelection(grid ? 1 : 0);

      if (grid) {
        if (m_favCards)
          m_favCards->SetFocusIgnoringChildren();
      } else {
        if (m_favList)
          m_favList->SetFocusFromKbd();
      }
    });
  } else {
    // Приостанавливаем загрузку для обоих представлений избранного
    if (m_favList)
      m_favList->PauseLogoLoading();
    if (m_favCards)
      m_favCards->PauseLogoLoading();
  }
}