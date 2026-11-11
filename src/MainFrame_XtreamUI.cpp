#include "MainFrame.h"

#include "Application.h"
#include "ConfigManager.h"
#include "LogControl.h"
#include "Playlist.h"
#include "PlaylistManager.h"
#include "Utils_Platform.h"
#include "Utils_UI.h"
#include "xtream/XtreamApiClient.h"
#include "xtream/XtreamDatabase.h"
#include "xtream/XtreamInfo.h"

#include <wx/activityindicator.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
#include <wx/weakref.h>

#include <atomic>
#include <thread>
#include <unordered_set>

namespace {

constexpr double kResumeMinPositionSec = 10.0;

std::unordered_set<std::string>
LoadListSet(xtream::XtreamDatabase *db,
            xtream::XtreamDatabase::ListType type) {
  std::unordered_set<std::string> out;
  if (!db)
    return out;
  const auto v = db->GetList(type);
  out.reserve(v.size());
  for (const auto &k : v)
    out.insert(k);
  return out;
}

int64_t ParseVodMediaKey(const std::string &key) {
  if (key.size() <= 4 || key.compare(0, 4, "vod:") != 0)
    return 0;
  try {
    return std::stoll(key.substr(4));
  } catch (...) {
    return 0;
  }
}

} // namespace

void MainFrame::EnsureMoviesPanel() {
  if (m_moviesPanelCreated)
    return;
  if (!m_notebook)
    return;

  m_moviesPanelCreated = true;

  m_moviesPage = new wxPanel(m_notebook, wxID_ANY);
  m_notebook->AddPage(m_moviesPage, "");
  m_moviesPageIdx = m_notebook->FindPage(m_moviesPage);

  auto *mainSizer = new wxBoxSizer(wxVERTICAL);

  // Line 1: заголовок + информация об аккаунте
  auto *titleSizer = new wxBoxSizer(wxHORIZONTAL);

  m_moviesHeader = new wxStaticText(m_moviesPage, wxID_ANY, _U("Movies"));
  {
    wxFont f = m_moviesHeader->GetFont();
    f.SetPointSize(12);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    m_moviesHeader->SetFont(f);
  }
  titleSizer->Add(m_moviesHeader, 0, wxALIGN_CENTER_VERTICAL | wxLEFT,
                  FromDIP(8));

  m_moviesInfoLabel = new wxStaticText(m_moviesPage, wxID_ANY, "");
  {
    wxFont f = m_moviesInfoLabel->GetFont();
    f.SetPointSize(f.GetPointSize() - 1);
    m_moviesInfoLabel->SetFont(f);
  }
  titleSizer->Add(m_moviesInfoLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT,
                  FromDIP(8));
  titleSizer->AddStretchSpacer(1);

  titleSizer->SetMinSize(wxSize(-1, GetHeaderRowHeight()));
  mainSizer->Add(titleSizer, 0, wxEXPAND | wxTOP, FromDIP(8));

  // Line 2: фильтры + поиск + сортировка + refresh
  auto *filterSizer = new wxBoxSizer(wxHORIZONTAL);

  m_moviesCategoryChoice = new wxChoice(m_moviesPage, wxID_ANY);
  m_moviesCategoryChoice->Bind(wxEVT_CHOICE,
                               &MainFrame::OnMoviesCategoryChanged, this);
  filterSizer->Add(m_moviesCategoryChoice, 0,
                   wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(4));

  m_moviesModeAllBtn = new wxToggleButton(m_moviesPage, wxID_ANY, _U("All"));
  m_moviesModeFavBtn =
      new wxToggleButton(m_moviesPage, wxID_ANY,
                         wxString::FromUTF8("\xE2\x98\x85 ") + _U("Favorites"));
  m_moviesModeWatchBtn = new wxToggleButton(
      m_moviesPage, wxID_ANY,
      wxString::FromUTF8("\xF0\x9F\x94\x96 ") + _U("Watchlist"));

  m_moviesModeAllBtn->SetValue(true);
  m_moviesModeAllBtn->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) {
    SetMoviesListMode(MoviesListMode::All);
  });
  m_moviesModeFavBtn->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) {
    SetMoviesListMode(MoviesListMode::Favorites);
  });
  m_moviesModeWatchBtn->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) {
    SetMoviesListMode(MoviesListMode::Watchlist);
  });

  filterSizer->Add(m_moviesModeAllBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(4));
  filterSizer->Add(m_moviesModeFavBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(4));
  filterSizer->Add(m_moviesModeWatchBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(8));

  m_moviesSearch = new wxSearchCtrl(m_moviesPage, wxID_ANY);
  m_moviesSearch->ShowCancelButton(true);
  m_moviesSearch->SetDescriptiveText(_U("Search movies"));
  m_moviesSearch->SetMinSize(wxSize(FromDIP(320), -1));
  m_moviesSearch->Bind(wxEVT_TEXT, &MainFrame::OnMoviesSearchChanged, this);
  filterSizer->Add(m_moviesSearch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(8));

  m_moviesSearchDebounce.SetOwner(this);
  Bind(wxEVT_TIMER, &MainFrame::OnMoviesSearchDebounce, this,
       m_moviesSearchDebounce.GetId());

  m_moviesSortChoice = new wxChoice(m_moviesPage, wxID_ANY);
  m_moviesSortChoice->Append(_U("Default"));
  m_moviesSortChoice->Append(_U("Name A-Z"));
  m_moviesSortChoice->Append(_U("Name Z-A"));
  m_moviesSortChoice->Append(_U("Rating \xE2\x86\x93"));
  m_moviesSortChoice->SetSelection(0);
  m_moviesSortChoice->Bind(wxEVT_CHOICE, &MainFrame::OnMoviesSortChanged, this);
  filterSizer->Add(m_moviesSortChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(8));

  filterSizer->AddStretchSpacer(1);

  m_moviesActivity = new wxActivityIndicator(m_moviesPage, wxID_ANY);
  m_moviesActivity->Hide();
  filterSizer->Add(m_moviesActivity, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(8));

  m_moviesRefreshBtn =
      new wxButton(m_moviesPage, wxID_ANY, _U("Refresh catalog"));
  m_moviesRefreshBtn->Bind(wxEVT_BUTTON, &MainFrame::OnMoviesRefreshCatalog,
                           this);
  filterSizer->Add(m_moviesRefreshBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   FromDIP(8));

  mainSizer->Add(filterSizer, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(8));

  auto *splitter =
      new wxSplitterWindow(m_moviesPage, wxID_ANY, wxDefaultPosition,
                           wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
  splitter->SetMinimumPaneSize(FromDIP(200));

  m_moviesCards = new PosterCards(splitter);

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (pl && pl->isXtream())
    m_moviesCards->SetPlaylistName(pl->getTitle());

  m_moviesCards->SetSelectCallback([this](const std::string &key, size_t idx) {
    OnMovieSelected(key, idx);
  });

  m_moviesCards->SetFavoriteToggleCallback(
      [this](const std::string &mediaKey) {
        const int64_t id = ParseVodMediaKey(mediaKey);
        if (id <= 0)
          return;
        ToggleMovieInList(id, /*favorites=*/true);
        ReloadMoviesView();
      });

  m_moviesCards->SetWatchlistToggleCallback(
      [this](const std::string &mediaKey) {
        const int64_t id = ParseVodMediaKey(mediaKey);
        if (id <= 0)
          return;
        ToggleMovieInList(id, /*favorites=*/false);
        ReloadMoviesView();
      });

  m_moviesInfo = new VodInfoPanel(splitter);
  m_moviesInfo->SetPlayCallback(
      [this](int64_t id, bool fromBeginning) {
        OnPlayVod(id, fromBeginning);
      });

  splitter->SplitVertically(m_moviesCards, m_moviesInfo);

  CallAfter([splitter]() {
    const int w = splitter->GetClientSize().GetWidth();
    if (w > 0)
      splitter->SetSashPosition(w * 2 / 3);
  });

  mainSizer->Add(splitter, 1, wxEXPAND | wxALL, FromDIP(4));

  m_moviesLoadMoreBtn = new wxButton(m_moviesPage, wxID_ANY, _U("Load more"));
  m_moviesLoadMoreBtn->Bind(wxEVT_BUTTON, &MainFrame::OnMoviesLoadMore, this);
  m_moviesLoadMoreBtn->Hide();
  mainSizer->Add(m_moviesLoadMoreBtn, 0, wxALIGN_CENTER | wxALL, FromDIP(6));

  m_moviesPage->SetSizer(mainSizer);
  m_moviesPage->Layout();

  PopulateMoviesCategories();

  if (!m_moviesCategoryIds.empty()) {
    m_currentVodCategoryId = m_moviesCategoryIds[0];
    LoadVodPage(true);
  }
}

void MainFrame::PopulateMoviesCategories() {
  if (!m_moviesCategoryChoice)
    return;

  m_moviesCategoryChoice->Clear();
  m_moviesCategoryIds.clear();

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  if (!db)
    return;

  m_moviesCategoryChoice->Append(_U("Recently Added"));
  m_moviesCategoryIds.push_back(kRecentlyAddedCategoryId);

  const auto cats = db->GetCategories("vod");
  for (const auto &c : cats) {
    m_moviesCategoryIds.push_back(c.id);
    m_moviesCategoryChoice->Append(wxString::FromUTF8(c.name));
  }

  if (!m_moviesCategoryIds.empty())
    m_moviesCategoryChoice->SetSelection(0);

  UpdateMoviesInfoLabel();
}

void MainFrame::OnMoviesCategoryChanged(wxCommandEvent &) {
  const int sel = m_moviesCategoryChoice->GetSelection();
  if (sel < 0 || sel >= static_cast<int>(m_moviesCategoryIds.size()))
    return;

  m_currentVodCategoryId = m_moviesCategoryIds[sel];
  m_moviesInfo->Clear();
  LoadVodPage(true);
}

void MainFrame::LoadVodPage(bool reset) {
  if (m_currentVodCategoryId == kRecentlyAddedCategoryId) {
    LoadMoviesRecentlyAdded();
    return;
  }
  if (m_vodLoading)
    return;
  if (m_currentVodCategoryId <= 0)
    return;

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  if (!db)
    return;

  if (reset) {
    m_lastVodStreamId = 0;
    m_moviesCards->SetItems({});
    m_moviesCards->SetPlaylistName(pl->getTitle());
    m_moviesInfo->Clear();
    m_moviesLoadMoreBtn->Hide();
  }

  m_vodLoading = true;
  if (m_moviesActivity) {
    m_moviesActivity->Show();
    m_moviesActivity->Start();
  }
  m_moviesLoadMoreBtn->Disable();

  constexpr int kPageSize = 200;
  const auto page =
      db->GetVodPage(m_currentVodCategoryId, m_lastVodStreamId, kPageSize);

  if (!page.empty())
    m_lastVodStreamId = page.back().streamId;

  const auto favSet = LoadListSet(
      db, xtream::XtreamDatabase::ListType::Favorites);
  const auto watchSet = LoadListSet(
      db, xtream::XtreamDatabase::ListType::Watchlist);

  std::vector<PosterItem> items;
  items.reserve(page.size());
  for (const auto &v : page) {
    PosterItem item;
    item.mediaKey = "vod:" + std::to_string(v.streamId);
    item.name = v.name;
    item.posterUrl = v.posterUrl;
    item.rating = v.rating;
    item.added = v.added;
    item.isFavorite = favSet.count(item.mediaKey) > 0;
    item.isWatchlist = watchSet.count(item.mediaKey) > 0;
    items.push_back(std::move(item));
  }

  if (reset)
    m_moviesCards->SetItems(std::move(items));
  else
    m_moviesCards->AppendItems(items);
  ApplyMoviesSort();

  if (reset) {
    m_moviesCards->SetEmptyMessage(m_moviesCards->GetMediaKey(0).empty()
                                       ? _U("Empty category")
                                       : wxString());
  }

  UpdateMoviesInfoLabel();

  m_moviesLoadMoreBtn->Show(page.size() == kPageSize);
  m_moviesLoadMoreBtn->Enable(true);

  m_vodLoading = false;
  if (m_moviesActivity) {
    m_moviesActivity->Stop();
    m_moviesActivity->Hide();
  }

  m_moviesPage->Layout();
}

void MainFrame::OnMoviesLoadMore(wxCommandEvent &) { LoadVodPage(false); }

void MainFrame::OnMovieSelected(const std::string &mediaKey, size_t /*index*/) {
  const int64_t streamId = ParseVodMediaKey(mediaKey);
  if (streamId <= 0)
    return;

  m_currentVodStreamId = streamId;

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  if (!db)
    return;

  const auto vod = db->GetVod(streamId);
  const std::string infoJson = db->GetVodInfo(streamId);
  const auto playback =
      db->GetPlaybackPosition("vod:" + std::to_string(streamId));

  if (!infoJson.empty()) {
    m_moviesInfo->SetVod(vod, infoJson, pl->getTitle(), playback);
    return;
  }

  m_moviesInfo->SetVod(vod, "", pl->getTitle(), playback);

  auto *client = mgr->GetXtreamClient();
  if (!client)
    return;

  const std::string plUuid = pl->getUniqueId();
  const std::string plTitle = pl->getTitle();
  const Playlist plCopy = *pl;

  wxWeakRef<MainFrame> weakThis(this);
  std::thread([weakThis, db, client, plCopy, streamId, plUuid, plTitle]() {
    std::atomic<bool> cancel{false};
    std::string info;
    const bool ok = client->FetchVodInfo(plCopy, streamId, *db, info, cancel);
    if (!ok || info.empty())
      return;

    auto *self = weakThis.get();
    if (!self)
      return;

    wxTheApp->CallAfter([self, db, streamId, info, plUuid, plTitle]() {
      Playlist *cur = self->GetPlaylistByIndex(self->m_loadedPlaylistIndex);
      if (!cur || cur->getUniqueId() != plUuid)
        return;
      if (self->m_currentVodStreamId != streamId)
        return;
      if (!self->m_moviesInfo)
        return;

      const auto vod2 = db->GetVod(streamId);
      const auto playback2 =
          db->GetPlaybackPosition("vod:" + std::to_string(streamId));
      self->m_moviesInfo->SetVod(vod2, info, plTitle, playback2);
    });
  }).detach();
}

void MainFrame::OnPlayVod(int64_t streamId, bool fromBeginning) {
  if (streamId <= 0)
    return;

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  auto *client = mgr->GetXtreamClient();
  if (!db || !client)
    return;

  const auto vod = db->GetVod(streamId);
  const std::string url = client->BuildVodUrl(*pl, streamId, vod.containerExt);
  if (url.empty())
    return;

  const std::string mediaKey = "vod:" + std::to_string(streamId);
  const auto savedPos = db->GetPlaybackPosition(mediaKey);

  double startPosition = 0.0;
  if (!fromBeginning && savedPos.hasPosition() && !savedPos.finished &&
      savedPos.position >= kResumeMinPositionSec) {
    int rewind = 15;
    if (auto *cfg = getConfigManager())
      rewind = cfg->getInt("xtream_resume_rewind_sec", 15);
    if (rewind < 5)
      rewind = 5;
    if (rewind > 30)
      rewind = 30;

    startPosition = savedPos.position - static_cast<double>(rewind);
    if (startPosition < 0.0)
      startPosition = 0.0;

    LOG_DEBUG("OnPlayVod: resume %s from %.1f (saved %.1f, rewind %d)",
              mediaKey.c_str(), startPosition, savedPos.position, rewind);
  }

  Channel ch;
  ch.setName(vod.name);
  ch.setUrl(url);
  ch.setLogo(vod.posterUrl);
  ch.setPlaylistName(pl->getTitle());
  ch.setMediaType(MediaType::Vod);
  ch.setStreamId(streamId);

  ClearPlaybackPosition();
  PlayChannel(ch, startPosition);
}

void MainFrame::OnMoviesToggle(wxCommandEvent &) {
  if (!m_btnMovies || !m_btnMovies->IsShown())
    return;

  EnsureMoviesPanel();

  if (m_videoPanel)
    m_videoPanel->SetTabActive(false);

  ToggleHeaderGroup(m_btnMovies);

  if (m_moviesPageIdx != wxNOT_FOUND)
    m_notebook->SetSelection(m_moviesPageIdx);
}

void MainFrame::HandleMoviesPageChanged(int sel) {
  if (m_moviesPageIdx == wxNOT_FOUND || sel != m_moviesPageIdx)
    return;
  if (!m_moviesPanelCreated)
    return;

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream() || !pl->hasVod())
    return;

  if (m_moviesCards && m_moviesCards->GetMediaKey(0).empty()) {
    PopulateMoviesCategories();
    if (!m_moviesCategoryIds.empty()) {
      m_currentVodCategoryId = m_moviesCategoryIds[0];
      LoadVodPage(true);
    }
  }
}

void MainFrame::ResetMoviesState() {
  if (m_moviesInfo)
    m_moviesInfo->Clear();
  if (m_moviesCards) {
    m_moviesCards->SetItems({});
    m_moviesCards->SetPlaylistName("");
    m_moviesCards->SetEmptyMessage(wxString());
  }
  if (m_moviesCategoryChoice)
    m_moviesCategoryChoice->Clear();

  if (m_moviesSearch) {
    m_moviesSearch->ChangeValue("");
    m_moviesSearch->Enable(true);
  }
  m_moviesSearchDebounce.Stop();
  m_moviesSearchQuery.clear();

  if (m_moviesSortChoice) {
    m_moviesSortChoice->SetSelection(0);
    m_moviesSortChoice->Enable(true);
  }
  m_moviesSortMode = MoviesSortMode::Default;

  if (m_moviesModeAllBtn) {
    m_moviesModeAllBtn->SetValue(true);
    m_moviesModeAllBtn->Enable(true);
    m_moviesModeFavBtn->SetValue(false);
    m_moviesModeFavBtn->Enable(true);
    m_moviesModeWatchBtn->SetValue(false);
    m_moviesModeWatchBtn->Enable(true);
  }
  m_moviesListMode = MoviesListMode::All;

  if (m_moviesActivity) {
    m_moviesActivity->Stop();
    m_moviesActivity->Hide();
  }
  if (m_moviesRefreshBtn)
    m_moviesRefreshBtn->Enable(true);

  m_moviesRefreshing = false;

  m_moviesCategoryIds.clear();
  m_currentVodCategoryId = 0;
  m_lastVodStreamId = 0;
  m_currentVodStreamId = 0;
  m_vodLoading = false;

  if (m_moviesLoadMoreBtn)
    m_moviesLoadMoreBtn->Hide();
  if (m_moviesHeader)
    m_moviesHeader->SetLabel(_U("Movies"));
  if (m_moviesInfoLabel)
    m_moviesInfoLabel->SetLabel("");
}

void MainFrame::UpdateMoviesTabVisibility() {
  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  const bool show = pl && pl->isXtream() && pl->hasVod();

  if (m_btnMovies) {
    m_btnMovies->Show(show);
    if (wxWindow *parent = m_btnMovies->GetParent())
      parent->Layout();
  }

  if (!show) {
    if (m_btnMovies && m_btnMovies->GetValue()) {
      ToggleHeaderGroup(m_btnChannels);
      if (m_channelsPageIdx != wxNOT_FOUND)
        m_notebook->SetSelection(m_channelsPageIdx);
    }
    if (m_moviesPanelCreated)
      ResetMoviesState();
  }
}

void MainFrame::OnMoviesRefreshCatalog(wxCommandEvent &) {
  if (m_moviesRefreshing)
    return;

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  auto *client = mgr->GetXtreamClient();
  if (!db || !client)
    return;

  const auto cats = db->GetCategories("vod");
  if (cats.empty())
    return;

  m_moviesRefreshing = true;
  if (m_moviesRefreshBtn)
    m_moviesRefreshBtn->Disable();
  if (m_moviesActivity) {
    m_moviesActivity->Show();
    m_moviesActivity->Start();
  }

  const std::string plUuid = pl->getUniqueId();
  const Playlist plCopy = *pl;

  wxWeakRef<MainFrame> weakThis(this);
  std::thread([weakThis, db, client, plCopy, cats, plUuid]() {
    std::atomic<bool> cancel{false};
    const size_t total = cats.size();

    for (size_t i = 0; i < total; ++i) {
      auto *self = weakThis.get();
      if (!self)
        return;

      client->RefreshVodCategory(plCopy, cats[i].id, *db, nullptr, cancel);

      auto *selfAfter = weakThis.get();
      if (!selfAfter)
        return;
      wxTheApp->CallAfter([selfAfter, i, total]() {
        selfAfter->SetStatusText(
            wxString::Format(_U("Movies: refreshing %zu/%zu"), i + 1, total),
            0);
      });
    }

    auto *self = weakThis.get();
    if (!self)
      return;

    wxTheApp->CallAfter([self, plUuid]() {
      Playlist *cur = self->GetPlaylistByIndex(self->m_loadedPlaylistIndex);
      if (!cur || cur->getUniqueId() != plUuid)
        return;

      self->m_moviesRefreshing = false;
      if (self->m_moviesRefreshBtn)
        self->m_moviesRefreshBtn->Enable(true);
      if (self->m_moviesActivity) {
        self->m_moviesActivity->Stop();
        self->m_moviesActivity->Hide();
      }
      self->SetStatusText(_U("Movies: catalog refreshed."), 0);

      self->PopulateMoviesCategories();
      if (!self->m_moviesCategoryIds.empty()) {
        self->m_currentVodCategoryId = self->m_moviesCategoryIds[0];
        self->LoadVodPage(true);
      }
    });
  }).detach();
}

void MainFrame::SetMoviesListMode(MoviesListMode mode) {
  m_moviesListMode = mode;

  if (m_moviesModeAllBtn) {
    m_moviesModeAllBtn->SetValue(mode == MoviesListMode::All);
    m_moviesModeFavBtn->SetValue(mode == MoviesListMode::Favorites);
    m_moviesModeWatchBtn->SetValue(mode == MoviesListMode::Watchlist);
    m_moviesModeAllBtn->Enable(true);
    m_moviesModeFavBtn->Enable(true);
    m_moviesModeWatchBtn->Enable(true);
  }

  if (m_moviesCategoryChoice)
    m_moviesCategoryChoice->Enable(mode == MoviesListMode::All);

  m_moviesInfo->Clear();
  m_currentVodStreamId = 0;

  switch (mode) {
  case MoviesListMode::All:
    m_lastVodStreamId = 0;
    if (!m_moviesCategoryIds.empty() &&
        (m_currentVodCategoryId <= 0 ||
         std::find(m_moviesCategoryIds.begin(), m_moviesCategoryIds.end(),
                   m_currentVodCategoryId) == m_moviesCategoryIds.end())) {
      m_currentVodCategoryId = m_moviesCategoryIds[0];
    }
    LoadVodPage(true);
    break;
  case MoviesListMode::Favorites:
    LoadMoviesFavorites();
    break;
  case MoviesListMode::Watchlist:
    LoadMoviesWatchlist();
    break;
  }
}

void MainFrame::LoadMoviesFavorites() {
  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  if (!db)
    return;

  const auto list =
      db->GetVodByList(xtream::XtreamDatabase::ListType::Favorites, 1000);

  const auto watchSet =
      LoadListSet(db, xtream::XtreamDatabase::ListType::Watchlist);

  std::vector<PosterItem> items;
  items.reserve(list.size());
  for (const auto &v : list) {
    PosterItem item;
    item.mediaKey = "vod:" + std::to_string(v.streamId);
    item.name = v.name;
    item.posterUrl = v.posterUrl;
    item.rating = v.rating;
    item.added = v.added;
    item.isFavorite = true;
    item.isWatchlist = watchSet.count(item.mediaKey) > 0;
    items.push_back(std::move(item));
  }

  m_moviesCards->SetItems(std::move(items));
  m_moviesCards->SetPlaylistName(pl->getTitle());
  ApplyMoviesSort();
  m_moviesCards->SetEmptyMessage(
      m_moviesCards->GetMediaKey(0).empty() ? _U("No favorites")
                                            : wxString());

  UpdateMoviesInfoLabel();

  if (m_moviesLoadMoreBtn)
    m_moviesLoadMoreBtn->Hide();

  m_moviesPage->Layout();
}

void MainFrame::LoadMoviesWatchlist() {
  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  if (!db)
    return;

  const auto list =
      db->GetVodByList(xtream::XtreamDatabase::ListType::Watchlist, 1000);

  const auto favSet =
      LoadListSet(db, xtream::XtreamDatabase::ListType::Favorites);

  std::vector<PosterItem> items;
  items.reserve(list.size());
  for (const auto &v : list) {
    PosterItem item;
    item.mediaKey = "vod:" + std::to_string(v.streamId);
    item.name = v.name;
    item.posterUrl = v.posterUrl;
    item.rating = v.rating;
    item.added = v.added;
    item.isFavorite = favSet.count(item.mediaKey) > 0;
    item.isWatchlist = true;
    items.push_back(std::move(item));
  }

  m_moviesCards->SetItems(std::move(items));
  m_moviesCards->SetPlaylistName(pl->getTitle());
  ApplyMoviesSort();
  m_moviesCards->SetEmptyMessage(
      m_moviesCards->GetMediaKey(0).empty() ? _U("Watchlist is empty")
                                            : wxString());

  UpdateMoviesInfoLabel();

  if (m_moviesLoadMoreBtn)
    m_moviesLoadMoreBtn->Hide();

  m_moviesPage->Layout();
}

void MainFrame::OnMoviesShowContextMenu(const std::string &mediaKey,
                                        const wxPoint &screenPos) {
  const int64_t streamId = ParseVodMediaKey(mediaKey);
  if (streamId <= 0)
    return;

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  auto *client = mgr->GetXtreamClient();
  if (!db)
    return;

  const auto playback = db->GetPlaybackPosition(mediaKey);
  const bool hasResume =
      playback.hasPosition() && !playback.finished && playback.position >= 10.0;

  const bool isFav =
      db->IsInList(mediaKey, xtream::XtreamDatabase::ListType::Favorites);
  const bool isWatch =
      db->IsInList(mediaKey, xtream::XtreamDatabase::ListType::Watchlist);

  wxMenu menu;
  int idPlay = wxNewId();
  int idPlayFromBeginning = wxNewId();
  int idFavorite = wxNewId();
  int idWatchlist = wxNewId();
  int idInfo = wxNewId();
  int idCopyUrl = wxNewId();

  if (hasResume)
    menu.Append(idPlay, _U("Resume"));
  else
    menu.Append(idPlay, _U("Play"));

  if (hasResume)
    menu.Append(idPlayFromBeginning, _U("Play from beginning"));
  else
    menu.Append(idPlayFromBeginning, _U("Play from beginning"));

  menu.AppendSeparator();
  menu.Append(idFavorite,
              isFav ? _U("Remove from favorites") : _U("Add to favorites"));
  menu.Append(idWatchlist,
              isWatch ? _U("Remove from watchlist") : _U("Add to watchlist"));
  menu.AppendSeparator();
  menu.Append(idInfo, _U("Movie info"));
  menu.Append(idCopyUrl, _U("Copy URL"));

  menu.Bind(
      wxEVT_MENU,
      [this, streamId](wxCommandEvent &) { OnPlayVod(streamId, false); },
      idPlay);

  menu.Bind(
      wxEVT_MENU,
      [this, streamId](wxCommandEvent &) { OnPlayVod(streamId, true); },
      idPlayFromBeginning);

  menu.Bind(
      wxEVT_MENU,
      [this, streamId](wxCommandEvent &) {
        ToggleMovieInList(streamId, /*favorites=*/true);
        ReloadMoviesView();
      },
      idFavorite);

  menu.Bind(
      wxEVT_MENU,
      [this, streamId](wxCommandEvent &) {
        ToggleMovieInList(streamId, /*favorites=*/false);
        ReloadMoviesView();
      },
      idWatchlist);

  menu.Bind(
      wxEVT_MENU,
      [this, mediaKey](wxCommandEvent &) { OnMovieSelected(mediaKey, 0); },
      idInfo);

  menu.Bind(
      wxEVT_MENU,
      [pl, client, db, streamId](wxCommandEvent &) {
        if (!client)
          return;
        const auto vod = db->GetVod(streamId);
        const std::string url =
            client->BuildVodUrl(*pl, streamId, vod.containerExt);
        if (url.empty())
          return;
        if (wxTheClipboard->Open()) {
          wxTheClipboard->SetData(
              new wxTextDataObject(wxString::FromUTF8(url)));
          wxTheClipboard->Close();
        }
      },
      idCopyUrl);

  PopupMenu(&menu, ScreenToClient(screenPos));
}

void MainFrame::ToggleMovieInList(int64_t streamId, bool favorites) {
  if (streamId <= 0)
    return;

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  if (!db)
    return;

  const std::string mediaKey = "vod:" + std::to_string(streamId);
  const auto lt = favorites ? xtream::XtreamDatabase::ListType::Favorites
                            : xtream::XtreamDatabase::ListType::Watchlist;

  const bool wasIn = db->IsInList(mediaKey, lt);
  if (wasIn)
    db->RemoveFromList(mediaKey, lt);
  else
    db->AddToList(mediaKey, lt);

  LOG_DEBUG("ToggleMovieInList: %s %s %s", mediaKey.c_str(),
            favorites ? "fav" : "watch", wasIn ? "removed" : "added");
}

void MainFrame::OnMoviesSearchChanged(wxCommandEvent &evt) {
  evt.Skip();
  m_moviesSearchDebounce.Stop();
  m_moviesSearchDebounce.StartOnce(300);
}

void MainFrame::OnMoviesSearchDebounce(wxTimerEvent &) {
  if (!m_moviesSearch)
    return;

  const wxString q = m_moviesSearch->GetValue().Trim();
  const std::string qLower = std::string(q.Lower().utf8_str().data());

  if (qLower == m_moviesSearchQuery)
    return;
  m_moviesSearchQuery = qLower;

  if (qLower.empty()) {
    if (m_moviesCategoryChoice)
      m_moviesCategoryChoice->Enable(m_moviesListMode == MoviesListMode::All);
    if (m_moviesModeAllBtn) {
      m_moviesModeAllBtn->Enable(true);
      m_moviesModeFavBtn->Enable(true);
      m_moviesModeWatchBtn->Enable(true);
    }
    SetMoviesListMode(m_moviesListMode);
  } else {
    ApplyMoviesSearch(qLower);
  }
}

void MainFrame::ApplyMoviesSearch(const std::string &queryLower) {
  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  if (!db)
    return;

  const auto list = db->SearchVod(queryLower, 200);

  const auto favSet = LoadListSet(
      db, xtream::XtreamDatabase::ListType::Favorites);
  const auto watchSet = LoadListSet(
      db, xtream::XtreamDatabase::ListType::Watchlist);

  std::vector<PosterItem> items;
  items.reserve(list.size());
  for (const auto &v : list) {
    PosterItem item;
    item.mediaKey = "vod:" + std::to_string(v.streamId);
    item.name = v.name;
    item.posterUrl = v.posterUrl;
    item.rating = v.rating;
    item.added = v.added;
    item.isFavorite = favSet.count(item.mediaKey) > 0;
    item.isWatchlist = watchSet.count(item.mediaKey) > 0;
    items.push_back(std::move(item));
  }

  m_moviesCards->SetItems(std::move(items));
  m_moviesCards->SetPlaylistName(pl->getTitle());
  ApplyMoviesSort();
  m_moviesCards->SetEmptyMessage(
      m_moviesCards->GetMediaKey(0).empty() ? _U("Nothing found") : wxString());

  UpdateMoviesInfoLabel();

  if (m_moviesLoadMoreBtn)
    m_moviesLoadMoreBtn->Hide();

  if (m_moviesCategoryChoice)
    m_moviesCategoryChoice->Disable();
  if (m_moviesModeAllBtn) {
    m_moviesModeAllBtn->Disable();
    m_moviesModeFavBtn->Disable();
    m_moviesModeWatchBtn->Disable();
  }

  m_moviesInfo->Clear();
  m_currentVodStreamId = 0;

  m_moviesPage->Layout();
}

void MainFrame::LoadMoviesRecentlyAdded() {
  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream())
    return;

  auto *mgr = getPlaylistManager();
  if (!mgr)
    return;

  auto *db = mgr->GetXtreamDb(*pl);
  if (!db)
    return;

  const auto list = db->GetRecentlyAddedVod(100);

  const auto favSet = LoadListSet(
      db, xtream::XtreamDatabase::ListType::Favorites);
  const auto watchSet = LoadListSet(
      db, xtream::XtreamDatabase::ListType::Watchlist);

  std::vector<PosterItem> items;
  items.reserve(list.size());
  for (const auto &v : list) {
    PosterItem item;
    item.mediaKey = "vod:" + std::to_string(v.streamId);
    item.name = v.name;
    item.posterUrl = v.posterUrl;
    item.rating = v.rating;
    item.added = v.added;
    item.isFavorite = favSet.count(item.mediaKey) > 0;
    item.isWatchlist = watchSet.count(item.mediaKey) > 0;
    items.push_back(std::move(item));
  }

  m_moviesCards->SetItems(std::move(items));
  m_moviesCards->SetPlaylistName(pl->getTitle());
  ApplyMoviesSort();
  m_moviesCards->SetEmptyMessage(
      m_moviesCards->GetMediaKey(0).empty() ? _U("No movies") : wxString());

  UpdateMoviesInfoLabel();

  if (m_moviesLoadMoreBtn)
    m_moviesLoadMoreBtn->Hide();

  m_moviesInfo->Clear();
  m_currentVodStreamId = 0;
  m_lastVodStreamId = 0;

  m_moviesPage->Layout();
}

void MainFrame::ApplyMoviesSort() {
  if (!m_moviesCards)
    return;

  PosterCards::SortComparator cmp;
  switch (m_moviesSortMode) {
  case MoviesSortMode::Default:
    return;
  case MoviesSortMode::NameAsc:
    cmp = [](const PosterItem &a, const PosterItem &b) {
      return wxString::FromUTF8(a.name).CmpNoCase(wxString::FromUTF8(b.name)) <
             0;
    };
    break;
  case MoviesSortMode::NameDesc:
    cmp = [](const PosterItem &a, const PosterItem &b) {
      return wxString::FromUTF8(a.name).CmpNoCase(wxString::FromUTF8(b.name)) >
             0;
    };
    break;
  case MoviesSortMode::RatingDesc:
    cmp = [](const PosterItem &a, const PosterItem &b) {
      return a.rating > b.rating;
    };
    break;
  }

  m_moviesCards->SortItems(cmp);
}

void MainFrame::OnMoviesSortChanged(wxCommandEvent &evt) {
  const int sel = m_moviesSortChoice ? m_moviesSortChoice->GetSelection() : 0;
  switch (sel) {
  case 0:
    m_moviesSortMode = MoviesSortMode::Default;
    break;
  case 1:
    m_moviesSortMode = MoviesSortMode::NameAsc;
    break;
  case 2:
    m_moviesSortMode = MoviesSortMode::NameDesc;
    break;
  case 3:
    m_moviesSortMode = MoviesSortMode::RatingDesc;
    break;
  default:
    m_moviesSortMode = MoviesSortMode::Default;
    break;
  }

  if (m_moviesSortMode == MoviesSortMode::Default) {
    // Возврат к порядку из БД — перезагрузка текущего вида.
    if (m_moviesSearchQuery.empty()) {
      SetMoviesListMode(m_moviesListMode);
    } else {
      ApplyMoviesSearch(m_moviesSearchQuery);
    }
    return;
  }

  ApplyMoviesSort();
  evt.Skip();
}

void MainFrame::ReloadMoviesView() {
  if (!m_moviesCards)
    return;
  if (!m_moviesSearchQuery.empty())
    ApplyMoviesSearch(m_moviesSearchQuery);
  else
    SetMoviesListMode(m_moviesListMode);
}

void MainFrame::EnsureSeriesPanel() {
  if (m_seriesPanelCreated)
    return;
  if (!m_notebook)
    return;

  m_seriesPanelCreated = true;

  m_seriesPage = new wxPanel(m_notebook, wxID_ANY);
  m_notebook->AddPage(m_seriesPage, "");
  m_seriesPageIdx = m_notebook->FindPage(m_seriesPage);

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  m_seriesPanel = new SeriesPanel(m_seriesPage, this);
  sizer->Add(m_seriesPanel, 1, wxEXPAND);
  m_seriesPage->SetSizer(sizer);
  m_seriesPage->Layout();
}

void MainFrame::OnSeriesToggle(wxCommandEvent &) {
  if (!m_btnSeries || !m_btnSeries->IsShown())
    return;

  EnsureSeriesPanel();

  if (m_videoPanel)
    m_videoPanel->SetTabActive(false);

  ToggleHeaderGroup(m_btnSeries);

  if (m_seriesPageIdx != wxNOT_FOUND)
    m_notebook->SetSelection(m_seriesPageIdx);
}

void MainFrame::HandleSeriesPageChanged(int sel) {
  if (m_seriesPageIdx == wxNOT_FOUND || sel != m_seriesPageIdx)
    return;
  if (!m_seriesPanelCreated || !m_seriesPanel)
    return;

  m_seriesPanel->OnActivated();
}

void MainFrame::UpdateSeriesTabVisibility() {
  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  const bool show = pl && pl->isXtream() && pl->hasSeries();

  if (m_btnSeries) {
    m_btnSeries->Show(show);
    if (wxWindow *parent = m_btnSeries->GetParent())
      parent->Layout();
  }

  if (!show) {
    if (m_btnSeries && m_btnSeries->GetValue()) {
      ToggleHeaderGroup(m_btnChannels);
      if (m_channelsPageIdx != wxNOT_FOUND)
        m_notebook->SetSelection(m_channelsPageIdx);
    }
    if (m_seriesPanelCreated)
      ResetSeriesState();
  }
}

void MainFrame::ResetSeriesState() {
  if (m_seriesPanel)
    m_seriesPanel->Reset();
}

void MainFrame::OnXtreamCacheCleared() {
  const bool moviesVisible = m_moviesPanelCreated && m_notebook &&
                             m_moviesPageIdx != wxNOT_FOUND &&
                             m_notebook->GetSelection() == m_moviesPageIdx;

  const bool seriesVisible = m_seriesPanelCreated && m_notebook &&
                             m_seriesPageIdx != wxNOT_FOUND &&
                             m_notebook->GetSelection() == m_seriesPageIdx;

  if (m_moviesPanelCreated) {
    ResetMoviesState();
    if (moviesVisible) {
      PopulateMoviesCategories();
      if (!m_moviesCategoryIds.empty()) {
        m_currentVodCategoryId = m_moviesCategoryIds[0];
        LoadVodPage(true);
      }
    }
  }

  if (m_seriesPanelCreated) {
    ResetSeriesState();
    if (seriesVisible && m_seriesPanel) {
      m_seriesPanel->OnActivated();
    }
  }
}

wxString MainFrame::FormatAccountInfoShort(const Playlist *pl) {
  if (!pl || !pl->isXtream() || !pl->getXtreamMeta().hasUserInfo())
    return wxString();

  const auto ui = xtream::ParseUserInfo(pl->getXtreamMeta().userInfoJson);
  const wxString status = wxString::FromUTF8(xtream::HumanStatus(ui));

  wxString result;
  if (ui.isTrial)
    result = _U("Trial") + _U(" \xC2\xB7 ");
  result += status;

  if (ui.hasExpDate()) {
    result += _U(" \xC2\xB7 ");
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (xtream::IsExpiringSoon(ui.expDate, now, 3))
      result += wxString::FromUTF8("\xE2\x9A\xA0 ");
    result += wxString::Format(
        _U("Expires %s"),
        wxString::FromUTF8(xtream::FormatExpiryDate(ui.expDate)));
  } else {
    result += _U(" \xC2\xB7 ") + _U("Unlimited");
  }

  return result;
}

void MainFrame::UpdateMoviesInfoLabel() {
  if (!m_moviesInfoLabel)
    return;

  Playlist *pl = GetPlaylistByIndex(m_loadedPlaylistIndex);
  if (!pl || !pl->isXtream()) {
    m_moviesInfoLabel->SetLabel("");
    return;
  }

  const size_t count = m_moviesCards ? m_moviesCards->GetChannels().size() : 0;

  wxString view;
  if (!m_moviesSearchQuery.empty()) {
    view = wxString::Format(_U("Search \"%s\""),
                            wxString::FromUTF8(m_moviesSearchQuery));
  } else if (m_moviesListMode == MoviesListMode::Favorites) {
    view = _U("Favorites");
  } else if (m_moviesListMode == MoviesListMode::Watchlist) {
    view = _U("Watchlist");
  } else if (m_currentVodCategoryId == kRecentlyAddedCategoryId) {
    view = _U("Recently Added");
  } else {
    int idx = -1;
    for (size_t i = 0; i < m_moviesCategoryIds.size(); ++i) {
      if (m_moviesCategoryIds[i] == m_currentVodCategoryId) {
        idx = static_cast<int>(i);
        break;
      }
    }
    if (idx >= 0 && m_moviesCategoryChoice &&
        idx < static_cast<int>(m_moviesCategoryChoice->GetCount()))
      view = m_moviesCategoryChoice->GetString(idx);
  }

  wxString info;
  if (!view.IsEmpty())
    info = wxString::Format(_U("%s (%zu)"), view, count);

  const wxString acc = FormatAccountInfoShort(pl);
  if (!acc.IsEmpty()) {
    if (!info.IsEmpty())
      info += _U(" \xC2\xB7 ");
    info += acc;
  }

  m_moviesInfoLabel->SetLabel(info);
  if (wxWindow *p = m_moviesInfoLabel->GetParent())
    p->Layout();
}
