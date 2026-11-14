#include "AddIPTVPlaylistDialog.h"
#include "../LogControl.h"
#include "../PlaylistManager.h"
#include "../Utils.h"
#include "IPTVOrgMetadataManager.h"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

AddIPTVPlaylistDialog::AddIPTVPlaylistDialog(wxWindow *parent,
                                             PlaylistManager *playlistMgr)
    : wxDialog(parent, wxID_ANY, _U("Add Playlist from IPTV-Org"),
               wxDefaultPosition, wxSize(600, 450),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_playlistMgr(playlistMgr),
      m_metadataMgr(std::make_shared<IPTVOrgMetadataManager>(playlistMgr)),
      m_pollTimer(this, wxNewId()) {
  Bind(wxEVT_TIMER, &AddIPTVPlaylistDialog::OnFetchPoll, this,
       m_pollTimer.GetId());
  InitializeUI();
}

AddIPTVPlaylistDialog::~AddIPTVPlaylistDialog() {
  // No waiting: worker does not touch `this`. Signal cancel; worker finishes
  // independently (curl aborts on next progress tick via externalCancel).
  m_pollTimer.Stop();
  if (m_currentFetch) {
    m_currentFetch->cancelled.store(true, std::memory_order_release);
    m_currentFetch.reset();
  }
  m_metadataMgr.reset();
}

void AddIPTVPlaylistDialog::InitializeUI() {
  auto *mainSizer = new wxBoxSizer(wxVERTICAL);

  // ------ Тип фильтра ------
  auto *filterTypeSizer = new wxBoxSizer(wxHORIZONTAL);
  filterTypeSizer->Add(new wxStaticText(this, wxID_ANY, _U("Filter by:")), 0,
                       wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  m_filterTypeChoice = new wxChoice(this, wxID_ANY);
  filterTypeSizer->Add(m_filterTypeChoice, 0, wxEXPAND);
  mainSizer->Add(filterTypeSizer, 0, wxEXPAND | wxALL, FromDIP(10));

  // ------ Поле поиска (wxTextCtrl) ------
  m_searchCtrl =
      new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxDefaultSize, wxTE_PROCESS_ENTER);
  m_searchCtrl->SetHint(_U("Type to search..."));
  mainSizer->Add(m_searchCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                 FromDIP(10));

  // ------ Список (wxDataViewListCtrl) ------
  m_dataViewList = new wxDataViewListCtrl(this, wxID_ANY);
  m_dataViewList->AppendTextColumn(_U("Name"), wxDATAVIEW_CELL_INERT, 400,
                                   wxALIGN_LEFT);
  mainSizer->Add(m_dataViewList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                 FromDIP(10));

  // ------ Auto-update ------
  m_autoUpdateCheck = new wxCheckBox(this, wxID_ANY,
                                     _U("Auto-update on startup"));
  m_autoUpdateCheck->SetValue(true);
  mainSizer->Add(m_autoUpdateCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM,
                 FromDIP(10));

  // ------ Кнопки ------
  auto *btnSizer = new wxBoxSizer(wxHORIZONTAL);
  m_refreshBtn = new wxButton(this, wxID_ANY, _U("Refresh List"));
  btnSizer->Add(m_refreshBtn, 0, wxRIGHT, FromDIP(10));
  btnSizer->AddStretchSpacer(1);
  btnSizer->Add(CreateButtonSizer(wxOK | wxCANCEL));
  mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, FromDIP(10));

  SetSizer(mainSizer);
  SetMinSize(wxSize(600, 400));
  Layout();

  BindEvents();

  PopulateFilterTypes();
  m_filterTypeChoice->SetSelection(0);
  m_filterType = FilterType::Country;
  StartAsyncFetch(m_filterType);
}

void AddIPTVPlaylistDialog::BindEvents() {
  // Filter choice
  m_filterTypeChoice->Bind(wxEVT_CHOICE,
                           &AddIPTVPlaylistDialog::OnFilterTypeChanged, this);

  // Search text
  m_searchCtrl->Bind(wxEVT_TEXT, &AddIPTVPlaylistDialog::OnSearchText, this);

  // Data view selection
  m_dataViewList->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED,
                       &AddIPTVPlaylistDialog::OnSelectionChanged, this);

  // Refresh button
  m_refreshBtn->Bind(wxEVT_BUTTON, &AddIPTVPlaylistDialog::OnRefresh, this);

  // OK / Cancel — кнопки создаются CreateButtonSizer'ом, указателей нет,
  // но они дети диалога с ID wxID_OK / wxID_CANCEL. Биндим на this по ID.
  Bind(wxEVT_BUTTON, &AddIPTVPlaylistDialog::OnOK, this, wxID_OK);
  Bind(wxEVT_BUTTON, &AddIPTVPlaylistDialog::OnCancel, this, wxID_CANCEL);
}

void AddIPTVPlaylistDialog::PopulateFilterTypes() {
  // Порядок совпадает с FilterType (Header).
  m_filterTypeChoice->Append(_U("Country"));   // FilterType::Country  = 0
  m_filterTypeChoice->Append(_U("Language"));  // FilterType::Language = 1
  m_filterTypeChoice->Append(_U("Category"));  // FilterType::Category = 2
  m_filterTypeChoice->SetSelection(0);
}

// ------------------------------------------------------------------
// Асинхронная загрузка
// ------------------------------------------------------------------
void AddIPTVPlaylistDialog::StartAsyncFetch(FilterType filterType) {
  // Cancel any in-flight fetch. Its results will be discarded.
  if (m_currentFetch) {
    m_currentFetch->cancelled.store(true, std::memory_order_release);
    m_currentFetch.reset();
  }

  m_allDisplayItems.clear();
  m_allNames.clear();
  m_allCodes.clear();
  m_allDisplayItems.push_back(_U("Loading..."));
  m_allNames.push_back(wxEmptyString);
  m_allCodes.push_back(wxEmptyString);
  UpdateList(wxEmptyString);

  auto token = std::make_shared<FetchToken>();
  token->filterType = filterType;
  m_currentFetch = token;

  auto mgr = m_metadataMgr;
  std::atomic<bool> *cancelFlag = &token->cancelled;

  std::thread([token, mgr, filterType, cancelFlag]() {
    std::string err;
    bool ok = false;
    switch (filterType) {
    case FilterType::Country:
      ok = mgr->FetchCountries(token->countries, &err, cancelFlag);
      break;
    case FilterType::Language:
      ok = mgr->FetchLanguages(token->languages, &err, cancelFlag);
      break;
    case FilterType::Category:
      ok = mgr->FetchCategories(token->categories, &err, cancelFlag);
      break;
    }
    if (!cancelFlag->load(std::memory_order_acquire)) {
      token->success = ok;
      token->errorText = std::move(err);
    } else {
      token->success = false;
    }
    token->done.store(true, std::memory_order_release);
  }).detach();

  m_pollTimer.Start(50);
}

void AddIPTVPlaylistDialog::OnFetchPoll(wxTimerEvent &) {
  auto token = m_currentFetch;
  if (!token) {
    m_pollTimer.Stop();
    return;
  }
  if (!token->done.load(std::memory_order_acquire))
    return;

  m_pollTimer.Stop();
  m_currentFetch.reset();

  if (token->cancelled.load(std::memory_order_acquire))
    return;

  OnDataLoaded(token->filterType, token->success, token->countries,
               token->languages, token->categories, token->errorText);
}

// ------------------------------------------------------------------
// Обработка загруженных данных
// ------------------------------------------------------------------
void AddIPTVPlaylistDialog::OnDataLoaded(
    FilterType filterType, bool success,
    const std::vector<Country> &countries,
    const std::vector<Language> &languages,
    const std::vector<Category> &categories, const std::string &errorText) {

  m_allDisplayItems.clear();
  m_allNames.clear();
  m_allCodes.clear();

  if (!success) {
    wxString msg;
    if (errorText.empty()) {
      msg = _U("Failed to load data");
    } else {
      msg = wxString::Format(_U("Error: %s"),
                             wxString::FromUTF8(errorText));
    }
    m_allDisplayItems.push_back(msg);
    m_allNames.push_back(wxEmptyString);
    m_allCodes.push_back(wxEmptyString);
  } else {
    auto add = [this](const wxString &name, const wxString &code) {
      m_allNames.push_back(name);
      m_allCodes.push_back(code);
      m_allDisplayItems.push_back(name + " (" + code + ")");
    };
    switch (filterType) {
      case FilterType::Country:
        for (const auto &c : countries)
          add(wxString::FromUTF8(c.name), wxString::FromUTF8(c.code));
        break;
      case FilterType::Language:
        for (const auto &l : languages)
          add(wxString::FromUTF8(l.name), wxString::FromUTF8(l.code));
        break;
      case FilterType::Category:
        for (const auto &c : categories)
          add(wxString::FromUTF8(c.name), wxString::FromUTF8(c.id));
        break;
    }
  }

  // Обновляем список с учётом текущего текста поиска
  wxString currentFilter = m_searchCtrl->GetValue();
  UpdateList(currentFilter);
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
    wxDataViewItem item = m_dataViewList->RowToItem(0);
    if (item.IsOk()) {
      size_t idx = static_cast<size_t>(m_dataViewList->GetItemData(item));
      if (idx < m_allCodes.size())
        m_selectedCode = m_allCodes[idx];
      else
        m_selectedCode.Clear();
    } else {
      m_selectedCode.Clear();
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
    size_t idx = static_cast<size_t>(m_dataViewList->GetItemData(item));
    if (idx < m_allCodes.size())
      m_selectedCode = m_allCodes[idx];
  }
  event.Skip();
}

void AddIPTVPlaylistDialog::OnFilterTypeChanged(wxCommandEvent &) {
  const int sel = m_filterTypeChoice->GetSelection();
  if (sel >= 0 && sel <= 2)
    m_filterType = static_cast<FilterType>(sel);
  else
    m_filterType = FilterType::Country;

  m_searchCtrl->SetValue(wxEmptyString); // очищаем поиск
  StartAsyncFetch(m_filterType);
}

void AddIPTVPlaylistDialog::OnRefresh(wxCommandEvent &) {
  m_metadataMgr->InvalidateCache();
  StartAsyncFetch(m_filterType);
  wxMessageBox(_U("Refreshing data..."), _U("Info"),
               wxOK | wxICON_INFORMATION, this);
}

void AddIPTVPlaylistDialog::OnOK(wxCommandEvent &event) {
  const int row = m_dataViewList->GetSelectedRow();
  if (row < 0) {
    wxMessageBox(_U("Please select a value from the list."), _U("Error"),
                 wxOK | wxICON_ERROR, this);
    return;
  }
  wxDataViewItem item = m_dataViewList->RowToItem(row);
  if (!item.IsOk()) {
    wxMessageBox(_U("Please select a value from the list."), _U("Error"),
                 wxOK | wxICON_ERROR, this);
    return;
  }
  const size_t idx = static_cast<size_t>(m_dataViewList->GetItemData(item));
  if (idx >= m_allCodes.size() || m_allCodes[idx].IsEmpty()) {
    wxMessageBox(_U("Please select a valid value."), _U("Error"),
                 wxOK | wxICON_ERROR, this);
    return;
  }

  const wxString code = m_allCodes[idx];
  const wxString name = (idx < m_allNames.size() && !m_allNames[idx].IsEmpty())
                            ? m_allNames[idx]
                            : code;

  wxString url, title;
  if (!BuildPlaylistUrl(m_filterType, code, name, url, title)) {
    wxMessageBox(_U("Failed to build playlist URL."), _U("Error"),
                 wxOK | wxICON_ERROR, this);
    return;
  }

  m_selectedUrl = url;
  m_selectedTitle = title;
  event.Skip();
}

void AddIPTVPlaylistDialog::OnCancel(wxCommandEvent &) {
  // Destructor will signal cancel; worker aborts on next curl tick.
  EndModal(wxID_CANCEL);
}

// ------------------------------------------------------------------
// Вспомогательные методы
// ------------------------------------------------------------------
bool AddIPTVPlaylistDialog::BuildPlaylistUrl(FilterType filterType,
                                             const wxString &code,
                                             const wxString &name,
                                             wxString &outUrl,
                                             wxString &outTitle) {
  wxString codeLower = code.Lower();
  wxString baseUrl = "https://iptv-org.github.io/iptv/";

  switch (filterType) {
  case FilterType::Country:
    outUrl = baseUrl + "countries/" + codeLower + ".m3u";
    outTitle = _U("IPTV ") + name;
    return true;
  case FilterType::Language:
    outUrl = baseUrl + "languages/" + codeLower + ".m3u";
    outTitle = _U("IPTV ") + name;
    return true;
  case FilterType::Category:
    outUrl = baseUrl + "categories/" + codeLower + ".m3u";
    outTitle = _U("IPTV ") + name;
    return true;
  }
  return false;
}
