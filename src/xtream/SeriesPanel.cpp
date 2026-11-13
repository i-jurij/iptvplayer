#include "xtream/SeriesPanel.h"

#include "ConfigManager.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Playlist.h"
#include "PlaylistManager.h"
#include "Utils_Platform.h"
#include "Utils_UI.h"
#include "xtream/XtreamApiClient.h"
#include "xtream/XtreamDatabase.h"

#include <rapidjson/document.h>

#include <wx/button.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
#include <wx/weakref.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <thread>
#include <unordered_set>

namespace {

constexpr int kPageSize = 200;

int64_t ParseSeriesMediaKey(const std::string &key) {
  if (key.size() <= 7 || key.compare(0, 7, "series:") != 0)
    return 0;
  try {
    return std::stoll(key.substr(7));
  } catch (...) {
    return 0;
  }
}

std::unordered_set<std::string>
LoadListSet(xtream::XtreamDatabase *db, xtream::XtreamDatabase::ListType t) {
  std::unordered_set<std::string> out;
  if (!db)
    return out;
  const auto v = db->GetList(t);
  out.reserve(v.size());
  for (const auto &k : v)
    out.insert(k);
  return out;
}

wxString FormatEpisodeDuration(const std::string &infoJson) {
  if (infoJson.empty())
    return wxString();
  rapidjson::Document d;
  if (d.Parse(infoJson.c_str()).HasParseError() || !d.IsObject())
    return wxString();
  if (!d.HasMember("duration"))
    return wxString();
  const auto &v = d["duration"];
  if (!v.IsString())
    return wxString();
  return wxString::FromUTF8(v.GetString());
}

wxString FormatEpisodeDate(int64_t addedUtc) {
  if (addedUtc <= 0)
    return wxString();
  return wxString::FromUTF8(
      FormatLocalTime(static_cast<time_t>(addedUtc), "%Y-%m-%d"));
}

} // namespace

SeriesPanel::SeriesPanel(wxWindow *parent, MainFrame *mainFrame)
    : wxPanel(parent), m_mainFrame(mainFrame) {
  BuildUi();
  if (m_mainFrame) {
    m_mainFrame->m_onPlaybackEnded = [this]() { OnPlaybackEnded(); };
  }
}

SeriesPanel::~SeriesPanel() = default;

void SeriesPanel::BuildUi() {
  auto *sizer = new wxBoxSizer(wxVERTICAL);

  m_book = new wxSimplebook(this, wxID_ANY);

  BuildSeriesListPage();
  BuildSeasonsPage();
  BuildEpisodesPage();

  m_book->ChangeSelection(0);

  sizer->Add(m_book, 1, wxEXPAND);
  SetSizer(sizer);

  Bind(wxEVT_CHAR_HOOK, &SeriesPanel::OnEscapeKey, this);
}

void SeriesPanel::BuildSeriesListPage() {
  m_seriesListPage = new wxPanel(m_book, wxID_ANY);

  auto *mainSizer = new wxBoxSizer(wxVERTICAL);

  // Line 1: заголовок + информация об аккаунте
  auto *titleSizer = new wxBoxSizer(wxHORIZONTAL);

  m_header = new wxStaticText(m_seriesListPage, wxID_ANY, _U("Series"));
  {
    wxFont f = m_header->GetFont();
    f.SetPointSize(12);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    m_header->SetFont(f);
  }
  titleSizer->Add(m_header, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

  m_infoLabel = new wxStaticText(m_seriesListPage, wxID_ANY, "");
  {
    wxFont f = m_infoLabel->GetFont();
    f.SetPointSize(f.GetPointSize() - 1);
    m_infoLabel->SetFont(f);
  }
  titleSizer->Add(m_infoLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT,
                  FromDIP(8));
  titleSizer->AddStretchSpacer(1);

  const int headerH =
      m_mainFrame ? m_mainFrame->GetHeaderRowHeight() : FromDIP(28);
  titleSizer->SetMinSize(wxSize(-1, headerH));
  mainSizer->Add(titleSizer, 0, wxEXPAND | wxTOP, FromDIP(8));

  // Line 2: фильтры + поиск + сортировка + refresh
  auto *filterSizer = new wxBoxSizer(wxHORIZONTAL);

  m_categoryChoice = new wxChoice(m_seriesListPage, wxID_ANY);
  m_categoryChoice->Bind(wxEVT_CHOICE, &SeriesPanel::OnCategoryChanged, this);
  filterSizer->Add(m_categoryChoice, 0,
                   wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(4));

  m_modeAllBtn = new wxToggleButton(m_seriesListPage, wxID_ANY, _U("All"));
  m_modeFavBtn =
      new wxToggleButton(m_seriesListPage, wxID_ANY,
                         wxString::FromUTF8("\xE2\x98\x85 ") + _U("Favorites"));
  m_modeWatchBtn = new wxToggleButton(m_seriesListPage, wxID_ANY,
                                      wxString::FromUTF8("\xF0\x9F\x94\x96 ") +
                                          _U("Watchlist"));

  m_modeAllBtn->SetValue(true);
  m_modeAllBtn->Bind(wxEVT_TOGGLEBUTTON, &SeriesPanel::OnModeAll, this);
  m_modeFavBtn->Bind(wxEVT_TOGGLEBUTTON, &SeriesPanel::OnModeFavorites, this);
  m_modeWatchBtn->Bind(wxEVT_TOGGLEBUTTON, &SeriesPanel::OnModeWatchlist, this);

  filterSizer->Add(m_modeAllBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(4));
  filterSizer->Add(m_modeFavBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(4));
  filterSizer->Add(m_modeWatchBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(8));

  m_search = new wxSearchCtrl(m_seriesListPage, wxID_ANY);
  m_search->ShowCancelButton(true);
  m_search->SetDescriptiveText(_U("Search series"));
  m_search->SetMinSize(wxSize(FromDIP(320), -1));
  m_search->Bind(wxEVT_TEXT, &SeriesPanel::OnSearchChanged, this);
  filterSizer->Add(m_search, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

  m_searchDebounce.SetOwner(this);
  Bind(wxEVT_TIMER, &SeriesPanel::OnSearchDebounce, this,
       m_searchDebounce.GetId());

  m_sortChoice = new wxChoice(m_seriesListPage, wxID_ANY);
  m_sortChoice->Append(_U("Default"));
  m_sortChoice->Append(_U("Name A-Z"));
  m_sortChoice->Append(_U("Name Z-A"));
  m_sortChoice->Append(_U("Rating \xE2\x86\x93"));
  m_sortChoice->SetSelection(0);
  m_sortChoice->Bind(wxEVT_CHOICE, &SeriesPanel::OnSortChanged, this);
  filterSizer->Add(m_sortChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(8));

  filterSizer->AddStretchSpacer(1);

  m_activity = new wxActivityIndicator(m_seriesListPage, wxID_ANY);
  m_activity->Hide();
  filterSizer->Add(m_activity, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(8));

  m_refreshBtn =
      new wxButton(m_seriesListPage, wxID_ANY, _U("Refresh catalog"));
  m_refreshBtn->Bind(wxEVT_BUTTON, &SeriesPanel::OnRefreshCatalog, this);
  filterSizer->Add(m_refreshBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(8));

  mainSizer->Add(filterSizer, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(8));

  m_cards = new PosterCards(m_seriesListPage);

  Playlist *pl = GetCurrentPlaylist();
  if (pl && pl->isXtream())
    m_cards->SetPlaylistName(pl->getTitle());

  m_cards->SetSelectCallback([this](const std::string &key, size_t idx) {
    OnSeriesSelected(key, idx);
  });

  m_cards->SetFavoriteToggleCallback([this](const std::string &mediaKey) {
    const int64_t id = ParseSeriesMediaKey(mediaKey);
    if (id <= 0)
      return;
    ToggleSeriesInList(id, /*favorites=*/true);
    ReloadSeriesView();
  });

  m_cards->SetWatchlistToggleCallback([this](const std::string &mediaKey) {
    const int64_t id = ParseSeriesMediaKey(mediaKey);
    if (id <= 0)
      return;
    ToggleSeriesInList(id, /*favorites=*/false);
    ReloadSeriesView();
  });

  m_cards->SetContextCallback(
      [this](const std::string &mediaKey, const wxPoint &screenPos) {
        OnSeriesShowContextMenu(mediaKey, screenPos);
      });

  mainSizer->Add(m_cards, 1, wxEXPAND | wxALL, FromDIP(4));

  m_loadMoreBtn = new wxButton(m_seriesListPage, wxID_ANY, _U("Load more"));
  m_loadMoreBtn->Bind(wxEVT_BUTTON, &SeriesPanel::OnLoadMore, this);
  m_loadMoreBtn->Hide();
  mainSizer->Add(m_loadMoreBtn, 0, wxALIGN_CENTER | wxALL, FromDIP(6));

  m_seriesListPage->SetSizer(mainSizer);
  m_seriesListPage->Layout();

  m_book->AddPage(m_seriesListPage, "Series");
}

void SeriesPanel::BuildSeasonsPage() {
  m_seasonsPage = new wxPanel(m_book, wxID_ANY);

  auto *sizer = new wxBoxSizer(wxVERTICAL);

  auto *header = new wxBoxSizer(wxHORIZONTAL);

  m_seasonsBackBtn =
      new wxButton(m_seasonsPage, wxID_ANY,
                   wxString::FromUTF8("\xE2\x86\x90 ") + _U("Back"));
  m_seasonsBackBtn->Bind(wxEVT_BUTTON, &SeriesPanel::OnBackClicked, this);
  header->Add(m_seasonsBackBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT,
              FromDIP(8));

  m_seasonsBreadcrumbSizer = new wxBoxSizer(wxHORIZONTAL);
  header->Add(m_seasonsBreadcrumbSizer, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT,
              FromDIP(8));

  m_seasonsActivity = new wxActivityIndicator(m_seasonsPage, wxID_ANY);
  m_seasonsActivity->Hide();
  header->Add(m_seasonsActivity, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
              FromDIP(8));

  sizer->Add(header, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));

  m_seasonsCards = new PosterCards(m_seasonsPage);
  m_seasonsCards->SetSelectCallback([this](const std::string &key, size_t idx) {
    OnSeasonSelected(key, idx);
  });

  m_seasonsCards->SetFavoriteToggleCallback([this](const std::string &) {
    if (m_currentSeriesId <= 0)
      return;
    ToggleSeriesInList(m_currentSeriesId, /*favorites=*/true);
    PopulateSeasonsCards();
  });

  m_seasonsCards->SetWatchlistToggleCallback([this](const std::string &) {
    if (m_currentSeriesId <= 0)
      return;
    ToggleSeriesInList(m_currentSeriesId, /*favorites=*/false);
    PopulateSeasonsCards();
  });

  sizer->Add(m_seasonsCards, 1, wxEXPAND | wxALL, FromDIP(4));

  m_seasonsPage->SetSizer(sizer);
  m_seasonsPage->Layout();

  m_book->AddPage(m_seasonsPage, "Seasons");
}

void SeriesPanel::BuildEpisodesPage() {
  m_episodesPage = new wxPanel(m_book, wxID_ANY);

  auto *sizer = new wxBoxSizer(wxVERTICAL);

  auto *header = new wxBoxSizer(wxHORIZONTAL);

  m_episodesBackBtn =
      new wxButton(m_episodesPage, wxID_ANY,
                   wxString::FromUTF8("\xE2\x86\x90 ") + _U("Back"));
  m_episodesBackBtn->Bind(wxEVT_BUTTON, &SeriesPanel::OnBackClicked, this);
  header->Add(m_episodesBackBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT,
              FromDIP(8));

  m_episodesBreadcrumbSizer = new wxBoxSizer(wxHORIZONTAL);
  header->Add(m_episodesBreadcrumbSizer, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT,
              FromDIP(8));

  sizer->Add(header, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));

  m_episodesList =
      new wxDataViewListCtrl(m_episodesPage, wxID_ANY, wxDefaultPosition,
                             wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);

  m_episodesList->AppendTextColumn(_U("#"), wxDATAVIEW_CELL_INERT, FromDIP(60),
                                   wxALIGN_RIGHT);
  m_episodesList->AppendTextColumn(_U("Title"), wxDATAVIEW_CELL_INERT,
                                   FromDIP(400), wxALIGN_LEFT);
  m_episodesList->AppendTextColumn(_U("Duration"), wxDATAVIEW_CELL_INERT,
                                   FromDIP(120), wxALIGN_LEFT);
  m_episodesList->AppendTextColumn(_U("Date"), wxDATAVIEW_CELL_INERT,
                                   FromDIP(120), wxALIGN_LEFT);

  m_episodesList->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,
                       &SeriesPanel::OnEpisodeActivated, this);

  sizer->Add(m_episodesList, 1, wxEXPAND | wxALL, FromDIP(4));

  m_episodesPage->SetSizer(sizer);
  m_episodesPage->Layout();

  m_book->AddPage(m_episodesPage, "Episodes");
}

Playlist *SeriesPanel::GetCurrentPlaylist() const {
  if (!m_mainFrame)
    return nullptr;
  return m_mainFrame->GetLoadedPlaylist();
}

xtream::XtreamDatabase *SeriesPanel::GetDb() const {
  Playlist *pl = GetCurrentPlaylist();
  if (!pl || !pl->isXtream())
    return nullptr;
  auto *mgr = m_mainFrame->getPlaylistManager();
  if (!mgr)
    return nullptr;
  return mgr->GetXtreamDb(*pl);
}

std::shared_ptr<xtream::XtreamDatabase> SeriesPanel::GetDbShared() const {
  Playlist *pl = GetCurrentPlaylist();
  if (!pl || !pl->isXtream())
    return nullptr;
  auto *mgr = m_mainFrame->getPlaylistManager();
  if (!mgr)
    return nullptr;
  return mgr->GetXtreamDbShared(*pl);
}

xtream::XtreamApiClient *SeriesPanel::GetClient() const {
  if (!m_mainFrame)
    return nullptr;
  auto *mgr = m_mainFrame->getPlaylistManager();
  return mgr ? mgr->GetXtreamClient() : nullptr;
}

void SeriesPanel::OnActivated() {
  Playlist *pl = GetCurrentPlaylist();
  LOG_DEBUG("SeriesPanel::OnActivated: pl=%p isXtream=%d hasSeries=%d "
            "activated=%d cardsNull=%d mediaKey0Empty=%d loading=%d "
            "categories=%zu currentCat=%d",
            (void *)pl, pl ? pl->isXtream() : 0, pl ? pl->hasSeries() : 0,
            m_activatedOnce ? 1 : 0, m_cards ? 0 : 1,
            (m_cards && m_cards->GetMediaKey(0).empty()) ? 1 : 0,
            m_loading ? 1 : 0, m_categoryIds.size(), m_currentCategoryId);

  if (!pl || !pl->isXtream() || !pl->hasSeries())
    return;

  if (!m_activatedOnce) {
    m_activatedOnce = true;
    m_navStack.clear();
  }

  // Симметрично Movies: при каждом входе, если список пуст и загрузка
  // не идёт — перезагружаем. Так ловятся случаи, когда БД заполнилась
  // асинхронно (loadPlaylistContent / Refresh) уже после первого входа.
  if (!m_cards || m_cards->GetMediaKey(0).empty()) {
    if (!m_loading) {
      PopulateCategories();
      LOG_DEBUG("SeriesPanel::OnActivated: after PopulateCategories, "
                "categories=%zu currentCat=%d",
                m_categoryIds.size(), m_currentCategoryId);
      if (!m_categoryIds.empty()) {
        const bool keepCurrent =
            m_currentCategoryId > 0 &&
            std::find(m_categoryIds.begin(), m_categoryIds.end(),
                      m_currentCategoryId) != m_categoryIds.end();
        if (!keepCurrent)
          m_currentCategoryId = m_categoryIds[0];
        LoadSeriesPage(true);
      }
    }
  }
}

void SeriesPanel::Reset() {
  m_level = Level::SeriesList;
  if (m_book)
    m_book->ChangeSelection(0);

  m_activatedOnce = false;
  m_loading = false;
  m_refreshing = false;

  if (m_cards) {
    m_cards->SetItems({});
    m_cards->SetPlaylistName("");
    m_cards->SetEmptyMessage(wxString());
  }
  if (m_categoryChoice)
    m_categoryChoice->Clear();

  if (m_modeAllBtn) {
    m_modeAllBtn->SetValue(true);
    m_modeAllBtn->Enable(true);
    m_modeFavBtn->SetValue(false);
    m_modeFavBtn->Enable(true);
    m_modeWatchBtn->SetValue(false);
    m_modeWatchBtn->Enable(true);
  }
  m_listMode = SeriesListMode::All;

  if (m_sortChoice) {
    m_sortChoice->SetSelection(0);
    m_sortChoice->Enable(true);
  }
  m_sortMode = SeriesSortMode::Default;

  m_categoryIds.clear();
  m_currentCategoryId = 0;
  m_lastSeriesId = 0;

  m_currentSeriesId = 0;
  m_currentSeriesName.clear();
  m_currentSeasonNumber = 0;
  m_pendingSeriesId = 0;
  m_currentSeasons.clear();
  m_currentEpisodes.clear();
  m_navStack.clear();
  ClearPlayingState();

  if (m_seasonsCards) {
    m_seasonsCards->SetItems({});
    m_seasonsCards->SetPlaylistName("");
    m_seasonsCards->SetEmptyMessage(wxString());
  }
  if (m_episodesList)
    m_episodesList->DeleteAllItems();

  if (m_header)
    m_header->SetLabel(_U("Series"));
  if (m_infoLabel)
    m_infoLabel->SetLabel("");
  if (m_seasonsBreadcrumbSizer)
    m_seasonsBreadcrumbSizer->Clear(true);
  if (m_episodesBreadcrumbSizer)
    m_episodesBreadcrumbSizer->Clear(true);
  if (m_loadMoreBtn)
    m_loadMoreBtn->Hide();
  if (m_activity) {
    m_activity->Stop();
    m_activity->Hide();
  }
  if (m_seasonsActivity) {
    m_seasonsActivity->Stop();
    m_seasonsActivity->Hide();
  }
  SetBusy(false);
}

void SeriesPanel::GoToLevel(Level level) {
  m_level = level;
  if (m_book)
    m_book->ChangeSelection(static_cast<int>(level));
  if (m_seasonsBackBtn)
    m_seasonsBackBtn->Enable(m_pendingSeriesId == 0);
}

bool SeriesPanel::GoBack() { return PopAndRestore(); }

void SeriesPanel::PushCurrentState() {
  SeriesNavState s;
  s.level = static_cast<int>(m_level);
  s.seriesId = m_currentSeriesId;
  s.seasonNumber = m_currentSeasonNumber;
  s.categoryId = m_currentCategoryId;
  s.seriesName = m_currentSeriesName;

  switch (m_level) {
  case Level::SeriesList:
    s.scrollY = m_cards ? m_cards->GetScrollY() : 0;
    break;
  case Level::Seasons:
    s.scrollY = m_seasonsCards ? m_seasonsCards->GetScrollY() : 0;
    break;
  case Level::Episodes:
    s.scrollY = 0;
    break;
  }
  m_navStack.push_back(s);
}

bool SeriesPanel::PopAndRestore() {
  if (m_navStack.empty())
    return false;

  const SeriesNavState s = m_navStack.back();
  m_navStack.pop_back();

  const Level target = static_cast<Level>(s.level);

  if (target == Level::SeriesList) {
    m_currentSeriesId = 0;
    m_currentSeriesName.clear();
    m_currentSeasonNumber = 0;
    m_currentSeasons.clear();
    m_currentEpisodes.clear();
    m_currentCategoryId = s.categoryId;
    if (m_categoryChoice) {
      for (size_t i = 0; i < m_categoryIds.size(); ++i) {
        if (m_categoryIds[i] == s.categoryId) {
          m_categoryChoice->SetSelection(static_cast<int>(i));
          break;
        }
      }
    }
  } else if (target == Level::Seasons) {
    m_currentSeriesId = s.seriesId;
    m_currentSeriesName = s.seriesName;
    m_currentSeasonNumber = 0;
    m_currentEpisodes.clear();
    PopulateSeasonsCards();
  }

  RebuildBreadcrumbs();
  GoToLevel(target);

  const int restoreY = s.scrollY;
  if (target == Level::SeriesList && m_cards) {
    CallAfter([this, restoreY]() {
      if (m_cards && m_level == Level::SeriesList)
        m_cards->SetScrollY(restoreY);
    });
  } else if (target == Level::Seasons && m_seasonsCards) {
    CallAfter([this, restoreY]() {
      if (m_seasonsCards && m_level == Level::Seasons)
        m_seasonsCards->SetScrollY(restoreY);
    });
  }
  return true;
}

void SeriesPanel::SetBusy(bool busy) {
  if (m_activity) {
    if (busy) {
      m_activity->Show();
      m_activity->Start();
    } else {
      m_activity->Stop();
      m_activity->Hide();
    }
  }
  if (m_refreshBtn)
    m_refreshBtn->Enable(!busy);
  if (m_categoryChoice)
    m_categoryChoice->Enable(!busy && m_level == Level::SeriesList &&
                             m_searchQuery.empty());
  if (m_search)
    m_search->Enable(!busy);
  if (m_sortChoice)
    m_sortChoice->Enable(!busy);
  if (m_loadMoreBtn && busy)
    m_loadMoreBtn->Disable();

  if (!busy && m_searchQuery.empty() && m_modeAllBtn) {
    m_modeAllBtn->Enable(true);
    m_modeFavBtn->Enable(true);
    m_modeWatchBtn->Enable(true);
  }
}

void SeriesPanel::PopulateCategories() {
  if (!m_categoryChoice)
    return;

  m_categoryChoice->Clear();
  m_categoryIds.clear();

  auto *db = GetDb();
  if (!db)
    return;

  m_categoryChoice->Append(_U("Recently Added"));
  m_categoryIds.push_back(kRecentlyAddedCategoryId);

  const auto cats = db->GetCategories("series");
  for (const auto &c : cats) {
    m_categoryIds.push_back(c.id);
    m_categoryChoice->Append(wxString::FromUTF8(c.name));
  }

  if (!m_categoryIds.empty())
    m_categoryChoice->SetSelection(0);

  UpdateInfoLabel();
}

void SeriesPanel::LoadSeriesPage(bool reset) {
  if (m_currentCategoryId == kRecentlyAddedCategoryId) {
    LoadRecentlyAdded();
    return;
  }
  if (m_loading)
    return;
  if (m_currentCategoryId <= 0)
    return;

  Playlist *pl = GetCurrentPlaylist();
  auto *db = GetDb();
  if (!pl || !db)
    return;

  if (reset) {
    m_lastSeriesId = 0;
    if (m_cards) {
      m_cards->SetItems({});
      m_cards->SetPlaylistName(pl->getTitle());
      m_cards->SetEmptyMessage(wxString());
    }
    if (m_loadMoreBtn)
      m_loadMoreBtn->Hide();
  }

  m_loading = true;
  SetBusy(true);

  const auto page =
      db->GetSeriesPage(m_currentCategoryId, m_lastSeriesId, kPageSize);

  if (!page.empty())
    m_lastSeriesId = page.back().seriesId;

  const auto favSet =
      LoadListSet(db, xtream::XtreamDatabase::ListType::Favorites);
  const auto watchSet =
      LoadListSet(db, xtream::XtreamDatabase::ListType::Watchlist);

  std::vector<PosterItem> items;
  items.reserve(page.size());
  for (const auto &s : page) {
    PosterItem item;
    item.mediaKey = "series:" + std::to_string(s.seriesId);
    item.name = s.name;
    item.posterUrl = s.coverUrl;
    item.rating = s.rating;
    item.added = s.lastModified;
    item.isFavorite = favSet.count(item.mediaKey) > 0;
    item.isWatchlist = watchSet.count(item.mediaKey) > 0;
    items.push_back(std::move(item));
  }

  if (m_cards) {
    if (reset)
      m_cards->SetItems(std::move(items));
    else
      m_cards->AppendItems(items);

    ApplySort();

    if (reset) {
      m_cards->SetEmptyMessage(
          m_cards->GetMediaKey(0).empty() ? _U("Empty category") : wxString());
    }
  }

  UpdateInfoLabel();

  if (m_loadMoreBtn) {
    m_loadMoreBtn->Show(page.size() == kPageSize);
    m_loadMoreBtn->Enable(true);
  }

  m_loading = false;
  SetBusy(false);

  m_seriesListPage->Layout();
}

void SeriesPanel::LoadRecentlyAdded() {
  if (m_loading)
    return;

  Playlist *pl = GetCurrentPlaylist();
  auto *db = GetDb();
  if (!pl || !db)
    return;

  m_loading = true;
  SetBusy(true);

  const auto list = db->GetRecentlyAddedSeries(100);

  const auto favSet =
      LoadListSet(db, xtream::XtreamDatabase::ListType::Favorites);
  const auto watchSet =
      LoadListSet(db, xtream::XtreamDatabase::ListType::Watchlist);

  std::vector<PosterItem> items;
  items.reserve(list.size());
  for (const auto &s : list) {
    PosterItem item;
    item.mediaKey = "series:" + std::to_string(s.seriesId);
    item.name = s.name;
    item.posterUrl = s.coverUrl;
    item.rating = s.rating;
    item.added = s.lastModified;
    item.isFavorite = favSet.count(item.mediaKey) > 0;
    item.isWatchlist = watchSet.count(item.mediaKey) > 0;
    items.push_back(std::move(item));
  }

  if (m_cards) {
    m_cards->SetItems(std::move(items));
    m_cards->SetPlaylistName(pl->getTitle());
    ApplySort();
    m_cards->SetEmptyMessage(m_cards->GetMediaKey(0).empty() ? _U("No series")
                                                             : wxString());
  }

  UpdateInfoLabel();

  if (m_loadMoreBtn)
    m_loadMoreBtn->Hide();

  m_loading = false;
  SetBusy(false);

  m_seriesListPage->Layout();
}

void SeriesPanel::OnCategoryChanged(wxCommandEvent &) {
  const int sel = m_categoryChoice ? m_categoryChoice->GetSelection() : -1;
  if (sel < 0 || sel >= static_cast<int>(m_categoryIds.size()))
    return;

  m_currentCategoryId = m_categoryIds[sel];
  LoadSeriesPage(true);
}

void SeriesPanel::OnLoadMore(wxCommandEvent &) { LoadSeriesPage(false); }

void SeriesPanel::OnSeriesSelected(const std::string &mediaKey,
                                   size_t /*index*/) {
  const int64_t seriesId = ParseSeriesMediaKey(mediaKey);
  if (seriesId <= 0)
    return;

  auto *db = GetDb();
  if (!db)
    return;

  const auto series = db->GetSeries(seriesId);
  const wxString seriesName =
      series.name.empty() ? _U("(Untitled)") : wxString::FromUTF8(series.name);

  DrillIntoSeries(seriesId, seriesName);
}

void SeriesPanel::DrillIntoSeries(int64_t seriesId,
                                  const wxString &seriesName) {
  m_currentSeriesId = seriesId;
  m_currentSeriesName = seriesName;
  m_currentSeasonNumber = 0;
  m_currentSeasons.clear();
  m_currentEpisodes.clear();

  auto db = GetDbShared();
  if (!db)
    return;

  const auto existing = db->GetSeasons(seriesId);
  if (!existing.empty()) {
    PushCurrentState();
    m_currentSeasons = existing;
    PopulateSeasonsCards();
    RebuildBreadcrumbs();
    GoToLevel(Level::Seasons);
    return;
  }

  // Не в БД — async fetch.
  auto *client = GetClient();
  Playlist *pl = GetCurrentPlaylist();
  if (!client || !pl) {
    return;
  }

  m_pendingSeriesId = seriesId;
  if (m_seasonsActivity) {
    m_seasonsActivity->Show();
    m_seasonsActivity->Start();
  }
  if (m_seasonsBackBtn)
    m_seasonsBackBtn->Disable();

  const Playlist plCopy = *pl;
  wxWeakRef<SeriesPanel> weakThis(this);

  std::thread([weakThis, db, client, plCopy, seriesId]() {
    std::atomic<bool> cancel{false};
    const bool ok =
        client->FetchSeriesInfo(plCopy, seriesId, *db, nullptr, cancel);

    auto *self = weakThis.get();
    if (!self)
      return;

    wxTheApp->CallAfter([weakThis, db, seriesId, ok]() {
      auto *s = weakThis.get();
      if (!s || s->IsBeingDeleted())
        return;
      if (s->m_pendingSeriesId != seriesId)
        return;

      s->m_pendingSeriesId = 0;
      if (s->m_seasonsActivity) {
        s->m_seasonsActivity->Stop();
        s->m_seasonsActivity->Hide();
      }
      if (s->m_seasonsBackBtn)
        s->m_seasonsBackBtn->Enable(true);

      if (!ok) {
        LOG_WARN("SeriesPanel: FetchSeriesInfo failed for %lld",
                 static_cast<long long>(seriesId));
        if (s->m_mainFrame && !s->m_mainFrame->IsBeingDeleted())
          s->m_mainFrame->SetStatusText(_U("Failed to load series info."), 0);
        return;
      }

      s->PushCurrentState();
      s->m_currentSeasons = db->GetSeasons(seriesId);
      s->PopulateSeasonsCards();
      s->RebuildBreadcrumbs();
      s->GoToLevel(Level::Seasons);
    });
  }).detach();
}

void SeriesPanel::PopulateSeasonsCards() {
  if (!m_seasonsCards)
    return;

  Playlist *pl = GetCurrentPlaylist();
  const std::string plName = pl ? pl->getTitle() : std::string();

  auto *db = GetDb();
  std::string seriesCover;
  if (db) {
    const auto s = db->GetSeries(m_currentSeriesId);
    seriesCover = s.coverUrl;
  }

  const auto favSet = LoadListSet(
      db, xtream::XtreamDatabase::ListType::Favorites);
  const auto watchSet = LoadListSet(
      db, xtream::XtreamDatabase::ListType::Watchlist);

  std::vector<PosterItem> items;
  items.reserve(m_currentSeasons.size());
  for (const auto &s : m_currentSeasons) {
    PosterItem item;
    item.mediaKey = "season:" + std::to_string(s.seasonNumber);
    if (!s.name.empty())
      item.name = s.name;
    else if (s.seasonNumber == 0)
      item.name = "Specials";
    else
      item.name = "Season " + std::to_string(s.seasonNumber);
    item.posterUrl = !s.coverUrl.empty() ? s.coverUrl : seriesCover;

    const std::string favKey =
        "series:" + std::to_string(m_currentSeriesId);
    item.isFavorite = favSet.count(favKey) > 0;
    item.isWatchlist = watchSet.count(favKey) > 0;

    items.push_back(std::move(item));
  }

  m_seasonsCards->SetItems(std::move(items));
  m_seasonsCards->SetPlaylistName(plName);
  m_seasonsCards->SetEmptyMessage(
      m_seasonsCards->GetMediaKey(0).empty() ? _U("No seasons") : wxString());

  m_seasonsPage->Layout();
}

void SeriesPanel::OnSeasonSelected(const std::string &mediaKey,
                                   size_t /*index*/) {
  if (mediaKey.size() <= 7 || mediaKey.compare(0, 7, "season:") != 0)
    return;
  int seasonNumber = 0;
  try {
    seasonNumber = std::stoi(mediaKey.substr(7));
  } catch (...) {
    return;
  }

  PushCurrentState();
  m_currentSeasonNumber = seasonNumber;
  PopulateEpisodesList(seasonNumber);
  RebuildBreadcrumbs();
  GoToLevel(Level::Episodes);
}

void SeriesPanel::PopulateEpisodesList(int seasonNumber) {
  if (!m_episodesList)
    return;

  m_episodesList->DeleteAllItems();
  m_currentEpisodes.clear();

  auto *db = GetDb();
  if (!db)
    return;

  m_currentEpisodes = db->GetEpisodes(m_currentSeriesId, seasonNumber);

  for (size_t i = 0; i < m_currentEpisodes.size(); ++i) {
    const auto &e = m_currentEpisodes[i];

    const wxString numStr =
        wxString::Format("%d", e.episodeNum > 0 ? e.episodeNum : (int)i + 1);
    const wxString title =
        e.title.empty() ? _U("(Untitled)") : wxString::FromUTF8(e.title);
    const wxString duration = FormatEpisodeDuration(e.infoJson);
    const wxString date = FormatEpisodeDate(e.added);

    wxVector<wxVariant> row;
    row.push_back(wxVariant(numStr));
    row.push_back(wxVariant(title));
    row.push_back(wxVariant(duration));
    row.push_back(wxVariant(date));

    m_episodesList->AppendItem(row, static_cast<wxUIntPtr>(i));
  }

  m_episodesPage->Layout();
}

void SeriesPanel::OnEpisodeActivated(wxDataViewEvent &evt) {
  wxDataViewItem item = evt.GetItem();
  if (!item.IsOk())
    return;

  const wxUIntPtr data = m_episodesList->GetItemData(item);
  const size_t idx = static_cast<size_t>(data);
  if (idx >= m_currentEpisodes.size())
    return;

  PlayEpisode(m_currentEpisodes[idx]);
}

void SeriesPanel::PlayEpisode(
    const xtream::XtreamDatabase::Episode &episode) {
  auto *pl = GetCurrentPlaylist();
  auto *client = GetClient();
  auto *db = GetDb();
  if (!pl || !client || !m_mainFrame)
    return;

  const std::string url =
      client->BuildSeriesEpisodeUrl(*pl, episode.id, episode.containerExt);
  if (url.empty()) {
    if (!m_mainFrame->IsBeingDeleted())
      m_mainFrame->SetStatusText(_U("Cannot build episode URL."), 0);
    return;
  }

  int64_t streamId = 0;
  try {
    streamId = std::stoll(episode.id);
  } catch (...) {
    streamId = 0;
  }

  const std::string mediaKey = "episode:" + episode.id;
  double startPosition = 0.0;
  if (db) {
    const auto saved = db->GetPlaybackPosition(mediaKey);
    if (saved.hasPosition() && !saved.finished && saved.position >= 10.0) {
      int rewind = 15;
      if (auto *cfg = m_mainFrame->getConfigManager())
        rewind = cfg->getInt("xtream_resume_rewind_sec", 15);
      if (rewind < 5)
        rewind = 5;
      if (rewind > 30)
        rewind = 30;
      startPosition = saved.position - static_cast<double>(rewind);
      if (startPosition < 0.0)
        startPosition = 0.0;
      LOG_DEBUG("SeriesPanel: resume %s from %.1f (saved %.1f)",
                mediaKey.c_str(), startPosition, saved.position);
    }
  }

  m_playingSeriesId = m_currentSeriesId;
  m_playingSeason = m_currentSeasonNumber;
  m_playingEpisodeId = episode.id;

  Channel ch;
  ch.setName(episode.title.empty()
                 ? ("Episode " + std::to_string(episode.episodeNum))
                 : episode.title);
  ch.setUrl(url);
  ch.setPlaylistName(pl->getTitle());
  ch.setMediaType(MediaType::Episode);
  ch.setStreamId(streamId);

  m_mainFrame->PlayChannel(ch, startPosition, mediaKey);
}

void SeriesPanel::OnBackClicked(wxCommandEvent &) {
  GoBack();
}

void SeriesPanel::RebuildBreadcrumbs() {
  RebuildSeasonsBreadcrumb();
  RebuildEpisodesBreadcrumb();
}

void SeriesPanel::RebuildSeasonsBreadcrumb() {
  if (!m_seasonsBreadcrumbSizer)
    return;

  m_seasonsBreadcrumbSizer->Clear(true);

  auto *nameLabel =
      new wxStaticText(m_seasonsPage, wxID_ANY, m_currentSeriesName);
  {
    wxFont f = nameLabel->GetFont();
    f.SetPointSize(f.GetPointSize() + 2);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    nameLabel->SetFont(f);
  }
  nameLabel->SetCursor(wxCURSOR_HAND);
  nameLabel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { GoBack(); });
  m_seasonsBreadcrumbSizer->Add(nameLabel, 0, wxALIGN_CENTER_VERTICAL);

  m_seasonsPage->Layout();
}

void SeriesPanel::RebuildEpisodesBreadcrumb() {
  if (!m_episodesBreadcrumbSizer)
    return;

  m_episodesBreadcrumbSizer->Clear(true);

  auto *nameLabel =
      new wxStaticText(m_episodesPage, wxID_ANY, m_currentSeriesName);
  {
    wxFont f = nameLabel->GetFont();
    f.SetPointSize(f.GetPointSize() + 2);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    nameLabel->SetFont(f);
  }
  nameLabel->SetCursor(wxCURSOR_HAND);
  nameLabel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { GoBack(); });
  m_episodesBreadcrumbSizer->Add(nameLabel, 0, wxALIGN_CENTER_VERTICAL);

  wxString seasonLabel =
      (m_currentSeasonNumber == 0)
          ? _U("Specials")
          : wxString::Format(_U("Season %d"), m_currentSeasonNumber);

  auto *sep = new wxStaticText(m_episodesPage, wxID_ANY,
                               wxString::FromUTF8(" \xC2\xB7 "));
  {
    wxFont f = sep->GetFont();
    f.SetPointSize(f.GetPointSize() + 2);
    sep->SetFont(f);
    sep->SetForegroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  }
  m_episodesBreadcrumbSizer->Add(sep, 0, wxALIGN_CENTER_VERTICAL);

  auto *seasonText =
      new wxStaticText(m_episodesPage, wxID_ANY, seasonLabel);
  {
    wxFont f = seasonText->GetFont();
    f.SetPointSize(f.GetPointSize() + 2);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    seasonText->SetFont(f);
  }
  m_episodesBreadcrumbSizer->Add(seasonText, 0, wxALIGN_CENTER_VERTICAL);

  m_episodesPage->Layout();
}

void SeriesPanel::OnRefreshCatalog(wxCommandEvent &) {
  if (m_refreshing)
    return;

  Playlist *pl = GetCurrentPlaylist();
  auto db = GetDbShared();
  auto *client = GetClient();
  if (!pl || !db || !client)
    return;

  m_refreshing = true;
  SetBusy(true);

  const std::string plUuid = pl->getUniqueId();
  const Playlist plCopy = *pl;
  wxWeakRef<SeriesPanel> weakThis(this);
  MainFrame *mf = m_mainFrame;

  std::thread([weakThis, mf, db, client, plCopy, plUuid]() {
    std::atomic<bool> cancel{false};
    const bool ok = client->RefreshAllSeries(plCopy, *db, nullptr, cancel);

    wxTheApp->CallAfter([weakThis, mf, plUuid, ok]() {
      auto *s = weakThis.get();
      if (!s || s->IsBeingDeleted())
        return;
      Playlist *cur = s->GetCurrentPlaylist();
      if (!cur || cur->getUniqueId() != plUuid)
        return;

      s->m_refreshing = false;
      s->SetBusy(false);

      if (mf && !mf->IsBeingDeleted()) {
        mf->SetStatusText(ok ? _U("Series: catalog refreshed.")
                             : _U("Series: catalog refresh failed."),
                          0);
      }

      if (!ok)
        return;

      s->PopulateCategories();
      if (!s->m_categoryIds.empty()) {
        s->m_currentCategoryId = s->m_categoryIds[0];
        s->LoadSeriesPage(true);
      }
    });
  }).detach();
}

void SeriesPanel::OnModeAll(wxCommandEvent &) {
  SetSeriesListMode(SeriesListMode::All);
}

void SeriesPanel::OnModeFavorites(wxCommandEvent &) {
  SetSeriesListMode(SeriesListMode::Favorites);
}

void SeriesPanel::OnModeWatchlist(wxCommandEvent &) {
  SetSeriesListMode(SeriesListMode::Watchlist);
}

void SeriesPanel::SetSeriesListMode(SeriesListMode mode) {
  m_listMode = mode;

  if (m_modeAllBtn) {
    m_modeAllBtn->SetValue(mode == SeriesListMode::All);
    m_modeFavBtn->SetValue(mode == SeriesListMode::Favorites);
    m_modeWatchBtn->SetValue(mode == SeriesListMode::Watchlist);
    m_modeAllBtn->Enable(true);
    m_modeFavBtn->Enable(true);
    m_modeWatchBtn->Enable(true);
  }

  if (m_categoryChoice)
    m_categoryChoice->Enable(mode == SeriesListMode::All &&
                             m_searchQuery.empty());

  switch (mode) {
  case SeriesListMode::All:
    LoadSeriesPage(true);
    break;
  case SeriesListMode::Favorites:
    LoadSeriesFavorites();
    break;
  case SeriesListMode::Watchlist:
    LoadSeriesWatchlist();
    break;
  }
}

void SeriesPanel::LoadSeriesFavorites() {
  Playlist *pl = GetCurrentPlaylist();
  auto *db = GetDb();
  if (!pl || !db || !m_cards)
    return;

  const auto list =
      db->GetSeriesByList(xtream::XtreamDatabase::ListType::Favorites, 1000);

  const auto watchSet =
      LoadListSet(db, xtream::XtreamDatabase::ListType::Watchlist);

  std::vector<PosterItem> items;
  items.reserve(list.size());
  for (const auto &s : list) {
    PosterItem item;
    item.mediaKey = "series:" + std::to_string(s.seriesId);
    item.name = s.name;
    item.posterUrl = s.coverUrl;
    item.rating = s.rating;
    item.added = s.lastModified;
    item.isFavorite = true;
    item.isWatchlist = watchSet.count(item.mediaKey) > 0;
    items.push_back(std::move(item));
  }

  m_cards->SetItems(std::move(items));
  m_cards->SetPlaylistName(pl->getTitle());
  ApplySort();
  m_cards->SetEmptyMessage(m_cards->GetMediaKey(0).empty() ? _U("No favorites")
                                                           : wxString());

  UpdateInfoLabel();

  if (m_loadMoreBtn)
    m_loadMoreBtn->Hide();

  m_seriesListPage->Layout();
}

void SeriesPanel::LoadSeriesWatchlist() {
  Playlist *pl = GetCurrentPlaylist();
  auto *db = GetDb();
  if (!pl || !db || !m_cards)
    return;

  const auto list =
      db->GetSeriesByList(xtream::XtreamDatabase::ListType::Watchlist, 1000);

  const auto favSet =
      LoadListSet(db, xtream::XtreamDatabase::ListType::Favorites);

  std::vector<PosterItem> items;
  items.reserve(list.size());
  for (const auto &s : list) {
    PosterItem item;
    item.mediaKey = "series:" + std::to_string(s.seriesId);
    item.name = s.name;
    item.posterUrl = s.coverUrl;
    item.rating = s.rating;
    item.added = s.lastModified;
    item.isFavorite = favSet.count(item.mediaKey) > 0;
    item.isWatchlist = true;
    items.push_back(std::move(item));
  }

  m_cards->SetItems(std::move(items));
  m_cards->SetPlaylistName(pl->getTitle());
  ApplySort();
  m_cards->SetEmptyMessage(
      m_cards->GetMediaKey(0).empty() ? _U("Watchlist is empty") : wxString());

  UpdateInfoLabel();

  if (m_loadMoreBtn)
    m_loadMoreBtn->Hide();

  m_seriesListPage->Layout();
}

void SeriesPanel::ReloadSeriesView() {
  if (!m_cards)
    return;
  if (!m_searchQuery.empty())
    ApplySearch(m_searchQuery);
  else
    SetSeriesListMode(m_listMode);
}

void SeriesPanel::ToggleSeriesInList(int64_t seriesId, bool favorites) {
  if (seriesId <= 0)
    return;

  auto *db = GetDb();
  if (!db)
    return;

  const std::string mediaKey = "series:" + std::to_string(seriesId);
  const auto lt = favorites ? xtream::XtreamDatabase::ListType::Favorites
                            : xtream::XtreamDatabase::ListType::Watchlist;

  if (db->IsInList(mediaKey, lt))
    db->RemoveFromList(mediaKey, lt);
  else
    db->AddToList(mediaKey, lt);

  LOG_DEBUG("SeriesPanel: ToggleSeriesInList %s %s", mediaKey.c_str(),
            favorites ? "fav" : "watch");
}

void SeriesPanel::OnSeriesShowContextMenu(const std::string &mediaKey,
                                          const wxPoint &screenPos) {
  const int64_t seriesId = ParseSeriesMediaKey(mediaKey);
  if (seriesId <= 0)
    return;

  auto *db = GetDb();
  if (!db)
    return;

  const bool isFav =
      db->IsInList(mediaKey, xtream::XtreamDatabase::ListType::Favorites);
  const bool isWatch =
      db->IsInList(mediaKey, xtream::XtreamDatabase::ListType::Watchlist);

  wxMenu menu;
  int idOpen = wxNewId();
  int idFav = wxNewId();
  int idWatch = wxNewId();

  menu.Append(idOpen, _U("Open"));
  menu.AppendSeparator();
  menu.Append(idFav,
              isFav ? _U("Remove from favorites") : _U("Add to favorites"));
  menu.Append(idWatch,
              isWatch ? _U("Remove from watchlist") : _U("Add to watchlist"));

  menu.Bind(
      wxEVT_MENU,
      [this, mediaKey](wxCommandEvent &) { OnSeriesSelected(mediaKey, 0); },
      idOpen);

  menu.Bind(
      wxEVT_MENU,
      [this, seriesId](wxCommandEvent &) {
        ToggleSeriesInList(seriesId, /*favorites=*/true);
        ReloadSeriesView();
      },
      idFav);

  menu.Bind(
      wxEVT_MENU,
      [this, seriesId](wxCommandEvent &) {
        ToggleSeriesInList(seriesId, /*favorites=*/false);
        ReloadSeriesView();
      },
      idWatch);

  PopupMenu(&menu, ScreenToClient(screenPos));
}

void SeriesPanel::OnEscapeKey(wxKeyEvent &evt) {
  if (evt.GetKeyCode() != WXK_ESCAPE) {
    evt.Skip();
    return;
  }
  if (m_level == Level::SeriesList) {
    evt.Skip();
    return;
  }
  GoBack();
  evt.StopPropagation();
}

void SeriesPanel::OnSearchChanged(wxCommandEvent &evt) {
  evt.Skip();
  m_searchDebounce.Stop();
  m_searchDebounce.StartOnce(300);
}

void SeriesPanel::OnSearchDebounce(wxTimerEvent &) {
  if (!m_search)
    return;

  const wxString q = m_search->GetValue().Trim();
  const std::string qLower = std::string(q.Lower().utf8_str().data());

  if (qLower == m_searchQuery)
    return;
  m_searchQuery = qLower;

  if (qLower.empty()) {
    if (m_sortChoice)
      m_sortChoice->Enable(true);
    if (m_modeAllBtn) {
      m_modeAllBtn->Enable(true);
      m_modeFavBtn->Enable(true);
      m_modeWatchBtn->Enable(true);
    }
    SetSeriesListMode(m_listMode);
  } else {
    ApplySearch(qLower);
  }
}

void SeriesPanel::ApplySearch(const std::string &queryLower) {
  auto *db = GetDb();
  Playlist *pl = GetCurrentPlaylist();
  if (!db || !pl || !m_cards)
    return;

  const auto list = db->SearchSeries(queryLower, 200);

  const auto favSet =
      LoadListSet(db, xtream::XtreamDatabase::ListType::Favorites);
  const auto watchSet =
      LoadListSet(db, xtream::XtreamDatabase::ListType::Watchlist);

  std::vector<PosterItem> items;
  items.reserve(list.size());
  for (const auto &s : list) {
    PosterItem item;
    item.mediaKey = "series:" + std::to_string(s.seriesId);
    item.name = s.name;
    item.posterUrl = s.coverUrl;
    item.rating = s.rating;
    item.added = s.lastModified;
    item.isFavorite = favSet.count(item.mediaKey) > 0;
    item.isWatchlist = watchSet.count(item.mediaKey) > 0;
    items.push_back(std::move(item));
  }

  m_cards->SetItems(std::move(items));
  m_cards->SetPlaylistName(pl->getTitle());
  ApplySort();
  m_cards->SetEmptyMessage(m_cards->GetMediaKey(0).empty() ? _U("Nothing found")
                                                           : wxString());
  UpdateInfoLabel();

  if (m_loadMoreBtn)
    m_loadMoreBtn->Hide();
  if (m_categoryChoice)
    m_categoryChoice->Disable();
  if (m_modeAllBtn) {
    m_modeAllBtn->Disable();
    m_modeFavBtn->Disable();
    m_modeWatchBtn->Disable();
  }

  m_seriesListPage->Layout();
}

void SeriesPanel::OnSortChanged(wxCommandEvent &evt) {
  const int sel = m_sortChoice ? m_sortChoice->GetSelection() : 0;
  switch (sel) {
  case 0:
    m_sortMode = SeriesSortMode::Default;
    break;
  case 1:
    m_sortMode = SeriesSortMode::NameAsc;
    break;
  case 2:
    m_sortMode = SeriesSortMode::NameDesc;
    break;
  case 3:
    m_sortMode = SeriesSortMode::RatingDesc;
    break;
  default:
    m_sortMode = SeriesSortMode::Default;
    break;
  }

  if (m_sortMode == SeriesSortMode::Default) {
    if (m_searchQuery.empty())
      LoadSeriesPage(true);
    else
      ApplySearch(m_searchQuery);
    evt.Skip();
    return;
  }

  ApplySort();
  evt.Skip();
}

void SeriesPanel::ApplySort() {
  if (!m_cards)
    return;

  PosterCards::SortComparator cmp;
  switch (m_sortMode) {
  case SeriesSortMode::Default:
    return;
  case SeriesSortMode::NameAsc:
    cmp = [](const PosterItem &a, const PosterItem &b) {
      return wxString::FromUTF8(a.name).CmpNoCase(wxString::FromUTF8(b.name)) <
             0;
    };
    break;
  case SeriesSortMode::NameDesc:
    cmp = [](const PosterItem &a, const PosterItem &b) {
      return wxString::FromUTF8(a.name).CmpNoCase(wxString::FromUTF8(b.name)) >
             0;
    };
    break;
  case SeriesSortMode::RatingDesc:
    cmp = [](const PosterItem &a, const PosterItem &b) {
      return a.rating > b.rating;
    };
    break;
  }

  m_cards->SortItems(cmp);
}

void SeriesPanel::ClearPlayingState() {
  m_playingSeriesId = 0;
  m_playingSeason = 0;
  m_playingEpisodeId.clear();
}

void SeriesPanel::OnPlaybackEnded() {
  if (m_playingEpisodeId.empty() || m_playingSeriesId <= 0)
    return;

  PlayNextEpisode();
}

void SeriesPanel::PlayNextEpisode() {
  auto *db = GetDb();
  if (!db || m_playingSeriesId <= 0)
    return;

  auto currentEpisodes = db->GetEpisodes(m_playingSeriesId, m_playingSeason);

  // Найти текущий по id, взять следующий
  std::string nextId;
  int nextSeason = m_playingSeason;
  int nextEpNum = 0;

  size_t currentIdx = SIZE_MAX;
  for (size_t i = 0; i < currentEpisodes.size(); ++i) {
    if (currentEpisodes[i].id == m_playingEpisodeId) {
      currentIdx = i;
      break;
    }
  }

  if (currentIdx != SIZE_MAX && currentIdx + 1 < currentEpisodes.size()) {
    nextId = currentEpisodes[currentIdx + 1].id;
    nextEpNum = currentEpisodes[currentIdx + 1].episodeNum;
  } else {
    // Сезон кончился — взять первый эпизод следующего сезона.
    const auto seasons = db->GetSeasons(m_playingSeriesId);
    bool foundCurrent = false;
    for (const auto &s : seasons) {
      if (foundCurrent) {
        const auto eps = db->GetEpisodes(m_playingSeriesId, s.seasonNumber);
        if (!eps.empty()) {
          nextId = eps[0].id;
          nextSeason = s.seasonNumber;
          nextEpNum = eps[0].episodeNum;
          break;
        }
      }
      if (s.seasonNumber == m_playingSeason)
        foundCurrent = true;
    }
  }

  if (nextId.empty()) {
    LOG_INFO("SeriesPanel: no next episode after %s (season %d)",
             m_playingEpisodeId.c_str(), m_playingSeason);
    if (m_mainFrame && !m_mainFrame->IsBeingDeleted())
      m_mainFrame->SetStatusText(_U("Season finished."), 0);
    ClearPlayingState();
    return;
  }

  // Найти полный Episode объект
  const auto nextEps = db->GetEpisodes(m_playingSeriesId, nextSeason);
  const xtream::XtreamDatabase::Episode *nextEp = nullptr;
  for (const auto &e : nextEps) {
    if (e.id == nextId) {
      nextEp = &e;
      break;
    }
  }

  if (!nextEp) {
    LOG_WARN("SeriesPanel: next episode %s not found", nextId.c_str());
    ClearPlayingState();
    return;
  }

  LOG_INFO("SeriesPanel: auto-next %s -> %s (S%dE%d)",
           m_playingEpisodeId.c_str(), nextId.c_str(), nextSeason, nextEpNum);

  m_currentSeasonNumber = nextSeason;
  PopulateEpisodesList(nextSeason);
  RebuildBreadcrumbs();

  PlayEpisode(*nextEp);
}

void SeriesPanel::FocusSearch() {
  if (m_search) {
    m_search->SetFocus();
    m_search->SelectAll();
  }
}

void SeriesPanel::UpdateInfoLabel() {
  if (!m_infoLabel)
    return;

  Playlist *pl = GetCurrentPlaylist();
  if (!pl || !pl->isXtream()) {
    m_infoLabel->SetLabel("");
    return;
  }

  const size_t count = m_cards ? m_cards->GetChannels().size() : 0;

  wxString view;
  if (!m_searchQuery.empty()) {
    view = wxString::Format(_U("Search \"%s\""),
                            wxString::FromUTF8(m_searchQuery));
  } else if (m_listMode == SeriesListMode::Favorites) {
    view = _U("Favorites");
  } else if (m_listMode == SeriesListMode::Watchlist) {
    view = _U("Watchlist");
  } else if (m_currentCategoryId == kRecentlyAddedCategoryId) {
    view = _U("Recently Added");
  } else {
    int idx = -1;
    for (size_t i = 0; i < m_categoryIds.size(); ++i) {
      if (m_categoryIds[i] == m_currentCategoryId) {
        idx = static_cast<int>(i);
        break;
      }
    }
    if (idx >= 0 && m_categoryChoice &&
        idx < static_cast<int>(m_categoryChoice->GetCount()))
      view = m_categoryChoice->GetString(idx);
  }

  wxString info;
  if (!view.IsEmpty())
    info = wxString::Format(_U("%s (%zu)"), view, count);

  const wxString acc = MainFrame::FormatAccountInfoShort(pl);
  if (!acc.IsEmpty()) {
    if (!info.IsEmpty())
      info += _U(" \xC2\xB7 ");
    info += acc;
  }

  m_infoLabel->SetLabel(info);
  if (wxWindow *p = m_infoLabel->GetParent())
    p->Layout();
}
