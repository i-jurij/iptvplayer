// MainFrame_Filters.cpp
#include "Application.h"
#include "FavoritesManager.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Profiler.h"
#include <algorithm>
#include <set>

void MainFrame::FillFilterChoices(const std::vector<Channel> &channels) {
  if (!m_groupChoice || !m_countryChoice || !m_langChoice)
    return;

  // Preserve current selection text if possible
  wxString selGroup = m_groupChoice->GetStringSelection();
  wxString selCountry = m_countryChoice->GetStringSelection();
  wxString selLang = m_langChoice->GetStringSelection();

  m_groupChoice->Clear();
  m_countryChoice->Clear();
  m_langChoice->Clear();

  m_groupChoice->Append("All groups");
  m_countryChoice->Append("All countries");
  m_langChoice->Append("All languages");

  std::set<std::string> groups, countries, langs;
  for (const auto &c : channels) {
    std::string g = c.getGroupTitle();
    std::string co = c.getCountry();
    std::string l = c.getLanguage();
    if (!g.empty())
      groups.insert(g);
    if (!co.empty())
      countries.insert(co);
    if (!l.empty())
      langs.insert(l);
  }

  for (const auto &g : groups)
    m_groupChoice->Append(wxString::FromUTF8(g));
  for (const auto &co : countries)
    m_countryChoice->Append(wxString::FromUTF8(co));
  for (const auto &l : langs)
    m_langChoice->Append(wxString::FromUTF8(l));

  // Try to restore previous selection if present
  int idx;
  idx = m_groupChoice->FindString(selGroup);
  m_groupChoice->SetSelection(idx == wxNOT_FOUND ? 0 : idx);
  idx = m_countryChoice->FindString(selCountry);
  m_countryChoice->SetSelection(idx == wxNOT_FOUND ? 0 : idx);
  idx = m_langChoice->FindString(selLang);
  m_langChoice->SetSelection(idx == wxNOT_FOUND ? 0 : idx);
}

void MainFrame::ApplyFiltersAndSort(bool incremental) {
  PROFILE_SCOPE("MainFrame::ApplyFiltersAndSort");
  // Если инкрементальное обновление — обновляем только UI без перезагрузки
  if (incremental) {
    LOG_DEBUG("ApplyFiltersAndSort(inc): start");
    if (m_channelList && m_channelList->GetModel()) {
      LOG_DEBUG("ApplyFiltersAndSort(inc): refreshing list");
      m_channelList->Refresh();
      LOG_DEBUG("ApplyFiltersAndSort(inc): list refreshed");
    }
    if (m_channelCards) {
      LOG_DEBUG("ApplyFiltersAndSort(inc): refreshing cards");
      m_channelCards->RefreshCards();
      LOG_DEBUG("ApplyFiltersAndSort(inc): cards refreshed");
    }
    // Обновляем заголовок
    wxString header = wxString::Format("Playlist: %s / Channels: %zu",
                                       wxString::FromUTF8(m_loadedPlaylistName),
                                       m_allChannels.size());
    m_channelsHeader->SetLabel(header);
    LOG_DEBUG("ApplyFiltersAndSort(inc): header updated");
    return;
  }

  // --- Полная перестройка
  // If no original channels, nothing to do
  if (m_allChannels.empty()) {
    // Clear views
    if (m_channelList)
      m_channelList->loadChannelsAsync({}, "");
    if (m_channelCards)
      m_channelCards->SetChannels({}, "");
    return;
  }

  // Read UI state
  wxString selGroup =
      m_groupChoice ? m_groupChoice->GetStringSelection() : "All groups";
  wxString selCountry =
      m_countryChoice ? m_countryChoice->GetStringSelection() : "All countries";
  wxString selLang =
      m_langChoice ? m_langChoice->GetStringSelection() : "All languages";
  wxString sort = m_sortChoice ? m_sortChoice->GetStringSelection() : "Name ▲";
  bool favFirst = m_favFirst ? m_favFirst->GetValue() : false;

  // Build filtered list
  std::vector<Channel> out;
  out.reserve(m_allChannels.size());

  for (const auto &c : m_allChannels) {
    // Name filter: none (TypeAheadSearch handles incremental search)
    // Group
    if (selGroup != "All groups") {
      if (wxString::FromUTF8(c.getGroupTitle()) != selGroup)
        continue;
    }
    // Country
    if (selCountry != "All countries") {
      if (wxString::FromUTF8(c.getCountry()) != selCountry)
        continue;
    }
    // Language
    if (selLang != "All languages") {
      if (wxString::FromUTF8(c.getLanguage()) != selLang)
        continue;
    }
    out.push_back(c);
  }

  // Sorting
  auto &fm = getApplication()->getFavoritesManager();
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

  std::sort(out.begin(), out.end(), [&](const Channel &a, const Channel &b) {
    if (favFirst) {
      bool fa = fm.isFavorite(a);
      bool fb = fm.isFavorite(b);
      if (fa != fb)
        return fa > fb;
    }

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

    return cmpNameAsc(a, b);
  });

  // ⭐ MEMORY RESET on filters (medium)
  if (m_channelCards)
    m_channelCards->ClearAllCaches(true, true);

  // Apply to views
  wxString playlistName = wxString::FromUTF8(m_loadedPlaylistName);
  // Pause -> Clear global cache -> set/append channels -> warm up visible ->
  // resume
  if (m_channelList) {
    // 1) Pause loading and clear pending keys in view
    m_channelList->PauseLogoLoading();

    // 2) Clear global logo cache (if you use a different API name, replace it)
    LogoCache::ClearMemory();

    // 3) Set initial batch asynchronously 
    m_channelList->loadChannelsAsync(out, playlistName.ToStdString());

    // 4) After model is set/append CallAfter
    wxTheApp->CallAfter([this]() {
      if (!m_channelList || m_channelList->m_closing.load())
        return;

      // Resume logo loading (will start timer if model has items)
      m_channelList->ResumeLogoLoading();
    });
  } else {
    // If no list, still update cards
    if (m_channelCards)
      m_channelCards->SetChannels(out, m_loadedPlaylistName);
  }

  if (m_channelCards)
    m_channelCards->SetChannels(out, m_loadedPlaylistName);
}
