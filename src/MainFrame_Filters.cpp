#include "Application.h"
#include "FavoritesManager.h"
#include "LogControl.h"
#include "MainFrame.h"
#include "Profiler.h"
#include "Utils.h"

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

  // Индекс 0 = "показать все". Логика фильтрации читает GetSelection() (см. ApplyFiltersAndSort).
  m_groupChoice->Append(_U("All groups"));
  m_countryChoice->Append(_U("All countries"));
  m_langChoice->Append(_U("All languages"));

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
    wxString header = wxString::Format(_U("Playlist: %s / Channels: %zu"),
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

  // Read UI state by index. Index 0 = "все"; индексы > 0 = реальные
  // значения каналов. Нельзя сравнивать с отображаемой строкой: после
  // локализации она зависит от языка и фильтр молча сломается.
  int selGroup   = m_groupChoice   ? m_groupChoice->GetSelection()   : 0;
  int selCountry = m_countryChoice ? m_countryChoice->GetSelection() : 0;
  int selLang    = m_langChoice    ? m_langChoice->GetSelection()    : 0;
  int sortIdx    = m_sortChoice    ? m_sortChoice->GetSelection()    : 0;
  const bool favFirst = m_favFirst ? m_favFirst->GetValue() : false;

  // Normalize wxNOT_FOUND (-1).
  if (selGroup   < 0) selGroup   = 0;
  if (selCountry < 0) selCountry = 0;
  if (selLang    < 0) selLang    = 0;
  if (sortIdx    < 0 ||
      sortIdx >= static_cast<int>(std::size(kSortOrder)))
    sortIdx = 0;

  // Pre-resolve real filter values (once, not per channel).
  wxString groupFilter, countryFilter, langFilter;
  if (m_groupChoice   && selGroup   > 0)
    groupFilter   = m_groupChoice->GetString(selGroup);
  if (m_countryChoice && selCountry > 0)
    countryFilter = m_countryChoice->GetString(selCountry);
  if (m_langChoice    && selLang    > 0)
    langFilter    = m_langChoice->GetString(selLang);

  // Build filtered list
  std::vector<Channel> out;
  out.reserve(m_allChannels.size());

  for (const auto &c : m_allChannels) {
    // Group
    if (selGroup > 0 &&
        wxString::FromUTF8(c.getGroupTitle()) != groupFilter)
      continue;
    // Country
    if (selCountry > 0 &&
        wxString::FromUTF8(c.getCountry()) != countryFilter)
      continue;
    // Language
    if (selLang > 0 &&
        wxString::FromUTF8(c.getLanguage()) != langFilter)
      continue;
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
  auto cmpLangAsc = [](const Channel &a, const Channel &b) {
    return a.getLanguage() < b.getLanguage();
  };
  auto cmpLangDesc = [](const Channel &a, const Channel &b) {
    return a.getLanguage() > b.getLanguage();
  };

  const SortMode mode = kSortOrder[sortIdx];

  std::sort(out.begin(), out.end(), [&](const Channel &a, const Channel &b) {
    if (favFirst) {
      bool fa = fm.isFavorite(a);
      bool fb = fm.isFavorite(b);
      if (fa != fb)
        return fa > fb;
    }

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
