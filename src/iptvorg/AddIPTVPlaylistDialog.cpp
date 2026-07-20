#include "AddIPTVPlaylistDialog.h"
#include "../LogControl.h"
#include "../PlaylistManager.h"
#include "IPTVOrgMetadataManager.h"
#include <chrono>
#include <thread>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

wxBEGIN_EVENT_TABLE(AddIPTVPlaylistDialog, wxDialog) EVT_CHOICE(
    ID_FILTER_TYPE_CHOICE, AddIPTVPlaylistDialog::OnFilterTypeChanged)
    EVT_BUTTON(ID_REFRESH_BTN, AddIPTVPlaylistDialog::OnRefresh)
        EVT_BUTTON(wxID_OK, AddIPTVPlaylistDialog::OnOK)
            EVT_BUTTON(wxID_CANCEL, AddIPTVPlaylistDialog::OnCancel)
                EVT_TEXT(ID_SEARCH_CTRL, AddIPTVPlaylistDialog::OnSearchText)
                    EVT_DATAVIEW_SELECTION_CHANGED(
                        wxID_ANY, AddIPTVPlaylistDialog::OnSelectionChanged)
                        wxEND_EVENT_TABLE()

                            AddIPTVPlaylistDialog::AddIPTVPlaylistDialog(
                                wxWindow *parent, PlaylistManager *playlistMgr)
    : wxDialog(parent, wxID_ANY, "Add Playlist from IPTV-Org",
               wxDefaultPosition, wxSize(600, 450),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_playlistMgr(playlistMgr),
      m_metadataMgr(new IPTVOrgMetadataManager(playlistMgr)) {
  InitializeUI();
}

AddIPTVPlaylistDialog::~AddIPTVPlaylistDialog() {
  m_cancelled = true;
  while (m_loading) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  delete m_metadataMgr;
}

void AddIPTVPlaylistDialog::InitializeUI() {
  auto *mainSizer = new wxBoxSizer(wxVERTICAL);

  // ------ Тип фильтра ------
  auto *filterTypeSizer = new wxBoxSizer(wxHORIZONTAL);
  filterTypeSizer->Add(new wxStaticText(this, wxID_ANY, "Filter by:"), 0,
                       wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  m_filterTypeChoice = new wxChoice(this, ID_FILTER_TYPE_CHOICE);
  filterTypeSizer->Add(m_filterTypeChoice, 0, wxEXPAND);
  mainSizer->Add(filterTypeSizer, 0, wxEXPAND | wxALL, 10);

  // ------ Поле поиска (wxTextCtrl) ------
  m_searchCtrl =
      new wxTextCtrl(this, ID_SEARCH_CTRL, wxEmptyString, wxDefaultPosition,
                     wxDefaultSize, wxTE_PROCESS_ENTER);
#if wxCHECK_VERSION(3, 1, 0)
  m_searchCtrl->SetHint("Type to search...");
#else
  m_searchCtrl->SetToolTip("Type to search...");
#endif
  mainSizer->Add(m_searchCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

  // ------ Список (wxDataViewListCtrl) ------
  m_dataViewList = new wxDataViewListCtrl(this, wxID_ANY);
  m_dataViewList->AppendTextColumn("Name", wxDATAVIEW_CELL_INERT, 400,
                                   wxALIGN_LEFT);
  mainSizer->Add(m_dataViewList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

  // ------ Кнопки ------
  auto *btnSizer = new wxBoxSizer(wxHORIZONTAL);
  m_refreshBtn = new wxButton(this, ID_REFRESH_BTN, "Refresh List");
  btnSizer->Add(m_refreshBtn, 0, wxRIGHT, 5);
  btnSizer->AddStretchSpacer(1);
  btnSizer->Add(CreateButtonSizer(wxOK | wxCANCEL));
  mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 10);

  SetSizerAndFit(mainSizer);
  SetMinSize(wxSize(600, 400));

  PopulateFilterTypes();
  m_filterTypeChoice->SetSelection(0);
  StartAsyncFetch(m_filterTypeChoice->GetStringSelection());
}

void AddIPTVPlaylistDialog::PopulateFilterTypes() {
  m_filterTypeChoice->Append("Country");
  m_filterTypeChoice->Append("Language");
  m_filterTypeChoice->Append("Category");
  m_filterTypeChoice->SetSelection(0);
}

// ------------------------------------------------------------------
// Асинхронная загрузка
// ------------------------------------------------------------------
void AddIPTVPlaylistDialog::StartAsyncFetch(const wxString &filterType) {
  if (m_loading) {
    LOG_DEBUG("AddIPTVPlaylistDialog: Already loading, ignoring request");
    return;
  }

  LOG_DEBUG("StartAsyncFetch: started for %s", filterType.ToUTF8().data());

  // Очищаем список и показываем "Loading..."
  m_allDisplayItems.clear();
  m_allCodes.clear();
  UpdateList(wxEmptyString);

  m_cancelled = false;
  m_loading = true;

  IPTVOrgMetadataManager *metadataMgr = m_metadataMgr;

  std::thread([this, filterType, metadataMgr]() {
    auto start = std::chrono::steady_clock::now();

    std::vector<Country> countries;
    std::vector<Language> languages;
    std::vector<Category> categories;
    bool success = false;

    if (filterType == "Country") {
      success = metadataMgr->FetchCountries(countries);
    } else if (filterType == "Language") {
      success = metadataMgr->FetchLanguages(languages);
    } else if (filterType == "Category") {
      success = metadataMgr->FetchCategories(categories);
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();
    LOG_DEBUG("Thread: fetch+parse for %s took %lld ms",
              filterType.ToUTF8().data(), elapsed);

    if (m_cancelled) {
      m_loading = false;
      return;
    }

    wxTheApp->CallAfter([this, filterType, success,
                         countries = std::move(countries),
                         languages = std::move(languages),
                         categories = std::move(categories)]() {
      if (m_cancelled) {
        m_loading = false;
        return;
      }
      OnDataLoaded(filterType, success, countries, languages, categories);
      m_loading = false;
    });
  }).detach();
}

// ------------------------------------------------------------------
// Обработка загруженных данных
// ------------------------------------------------------------------
void AddIPTVPlaylistDialog::OnDataLoaded(
    const wxString &filterType, bool success,
    const std::vector<Country> &countries,
    const std::vector<Language> &languages,
    const std::vector<Category> &categories) {

  if (m_cancelled)
    return;

  auto uiStart = std::chrono::steady_clock::now();

  m_allDisplayItems.clear();
  m_allCodes.clear();

  if (!success) {
    m_allDisplayItems.push_back("Failed to load data");
    m_allCodes.push_back("");
  } else {
    if (filterType == "Country") {
      for (const auto &c : countries) {
        m_allDisplayItems.push_back(wxString::FromUTF8(c.name) + " (" +
                                    wxString::FromUTF8(c.code) + ")");
        m_allCodes.push_back(wxString::FromUTF8(c.code));
      }
    } else if (filterType == "Language") {
      for (const auto &l : languages) {
        m_allDisplayItems.push_back(wxString::FromUTF8(l.name) + " (" +
                                    wxString::FromUTF8(l.code) + ")");
        m_allCodes.push_back(wxString::FromUTF8(l.code));
      }
    } else if (filterType == "Category") {
      for (const auto &c : categories) {
        m_allDisplayItems.push_back(wxString::FromUTF8(c.name) + " (" +
                                    wxString::FromUTF8(c.id) + ")");
        m_allCodes.push_back(wxString::FromUTF8(c.id));
      }
    }
  }

  // Обновляем список с учётом текущего текста поиска
  wxString currentFilter = m_searchCtrl->GetValue();
  UpdateList(currentFilter);

  auto uiEnd = std::chrono::steady_clock::now();
  auto uiElapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(uiEnd - uiStart)
          .count();
  LOG_DEBUG("OnDataLoaded: UI update for %s took %lld ms",
            filterType.ToUTF8().data(), uiElapsed);
}

// ------------------------------------------------------------------
// Обновление списка (фильтрация)
// ------------------------------------------------------------------
void AddIPTVPlaylistDialog::UpdateList(const wxString &filterText) {
  if (m_updating)
    return;
  m_updating = true;

  m_dataViewList->DeleteAllItems();

  wxString lowerFilter = filterText.Lower();

  for (size_t i = 0; i < m_allDisplayItems.size(); ++i) {
    if (filterText.IsEmpty() ||
        m_allDisplayItems[i].Lower().Contains(lowerFilter)) {
      wxVector<wxVariant> row;
      row.push_back(wxVariant(m_allDisplayItems[i]));
      m_dataViewList->AppendItem(row, (wxUIntPtr)i);
    }
  }

  // Выбираем первую строку, если есть
  if (m_dataViewList->GetItemCount() > 0) {
    m_dataViewList->SelectRow(0);
    // Обновим m_selectedCode
    wxDataViewItem item = m_dataViewList->RowToItem(0);
    if (item.IsOk()) {
      wxUIntPtr ptr = m_dataViewList->GetItemData(item);
      if (ptr != 0) {
        size_t idx = static_cast<size_t>(ptr);
        if (idx < m_allCodes.size())
          m_selectedCode = m_allCodes[idx];
      }
    }
  } else {
    m_selectedCode.Clear();
  }

  m_updating = false;
}

// ------------------------------------------------------------------
// Обработчики событий
// ------------------------------------------------------------------
void AddIPTVPlaylistDialog::OnSearchText(wxCommandEvent &event) {
  wxString filterText = m_searchCtrl->GetValue();
  UpdateList(filterText);
  event.Skip();
}

void AddIPTVPlaylistDialog::OnSelectionChanged(wxDataViewEvent &event) {
  wxDataViewItem item = event.GetItem();
  if (item.IsOk()) {
    wxUIntPtr ptr = m_dataViewList->GetItemData(item);
    if (ptr != 0) {
      size_t idx = static_cast<size_t>(ptr);
      if (idx < m_allCodes.size())
        m_selectedCode = m_allCodes[idx];
    }
  }
  event.Skip();
}

void AddIPTVPlaylistDialog::OnFilterTypeChanged(wxCommandEvent &) {
  wxString filterType = m_filterTypeChoice->GetStringSelection();
  m_searchCtrl->SetValue(wxEmptyString); // очищаем поиск
  StartAsyncFetch(filterType);
}

void AddIPTVPlaylistDialog::OnRefresh(wxCommandEvent &) {
  m_metadataMgr->InvalidateCache();
  wxString filterType = m_filterTypeChoice->GetStringSelection();
  StartAsyncFetch(filterType);
  wxMessageBox("Refreshing data...", "Info", wxOK | wxICON_INFORMATION, this);
}

void AddIPTVPlaylistDialog::OnOK(wxCommandEvent &event) {
  if (m_selectedCode.IsEmpty()) {
    wxMessageBox("Please select a value from the list.", "Error",
                 wxOK | wxICON_ERROR, this);
    return;
  }

  wxString filterType = m_filterTypeChoice->GetStringSelection();
  wxString url, title;
  if (!BuildPlaylistUrl(filterType, m_selectedCode, url, title)) {
    wxMessageBox("Failed to build playlist URL.", "Error", wxOK | wxICON_ERROR,
                 this);
    return;
  }

  m_selectedUrl = url;
  m_selectedTitle = title;

  event.Skip();
}

void AddIPTVPlaylistDialog::OnCancel(wxCommandEvent &) {
  m_cancelled = true;
  EndModal(wxID_CANCEL);
}

// ------------------------------------------------------------------
// Вспомогательные методы
// ------------------------------------------------------------------
bool AddIPTVPlaylistDialog::BuildPlaylistUrl(const wxString &filterType,
                                             const wxString &code,
                                             wxString &outUrl,
                                             wxString &outTitle) {
  wxString codeLower = code.Lower();
  wxString baseUrl = "https://iptv-org.github.io/iptv/";

  if (filterType == "Country") {
    outUrl = baseUrl + "countries/" + codeLower + ".m3u";
    outTitle = "IPTV " + GetDisplayName("Country", code);
    return true;
  } else if (filterType == "Language") {
    outUrl = baseUrl + "languages/" + codeLower + ".m3u";
    outTitle = "IPTV " + GetDisplayName("Language", code);
    return true;
  } else if (filterType == "Category") {
    outUrl = baseUrl + "categories/" + codeLower + ".m3u";
    outTitle = "IPTV " + GetDisplayName("Category", code);
    return true;
  }
  return false;
}

wxString AddIPTVPlaylistDialog::GetDisplayName(const wxString &filterType,
                                               const wxString &code) {
  if (filterType == "Country") {
    std::vector<Country> countries;
    if (m_metadataMgr->FetchCountries(countries)) {
      for (const auto &c : countries) {
        if (wxString::FromUTF8(c.code).IsSameAs(code, false))
          return wxString::FromUTF8(c.name);
      }
    }
  } else if (filterType == "Language") {
    std::vector<Language> languages;
    if (m_metadataMgr->FetchLanguages(languages)) {
      for (const auto &l : languages) {
        if (wxString::FromUTF8(l.code).IsSameAs(code, false))
          return wxString::FromUTF8(l.name);
      }
    }
  } else if (filterType == "Category") {
    std::vector<Category> categories;
    if (m_metadataMgr->FetchCategories(categories)) {
      for (const auto &c : categories) {
        if (wxString::FromUTF8(c.id).IsSameAs(code, false))
          return wxString::FromUTF8(c.name);
      }
    }
  }
  return code;
}