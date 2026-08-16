#include "EpgSourceManagerPanel.h"
#include "Utils.h"
#include "epg/AddEpgSourceDialog.h"
#include "epg/EPGData.h"
#include "epg/EPGManager.h"

#include <wx/filename.h>
#include <wx/statbox.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h> // для wxTextEntryDialog

wxBEGIN_EVENT_TABLE(EpgSourceManagerPanel, wxPanel)
    EVT_BUTTON(ID_ADD_SOURCE, EpgSourceManagerPanel::OnAdd)
    EVT_BUTTON(ID_EDIT_SOURCE, EpgSourceManagerPanel::OnEdit)
    EVT_BUTTON(ID_REMOVE_SOURCE, EpgSourceManagerPanel::OnRemove)
    EVT_BUTTON(ID_REFRESH_SOURCE, EpgSourceManagerPanel::OnRefresh)
    EVT_BUTTON(ID_DELETE_CACHE, EpgSourceManagerPanel::OnDeleteCache)
    EVT_LIST_ITEM_SELECTED(wxID_ANY, EpgSourceManagerPanel::OnSourceSelected)
wxEND_EVENT_TABLE()

EpgSourceManagerPanel::EpgSourceManagerPanel(
    wxWindow *parent, EPGManager *epgMgr,
                                    bool showAutoUpdateSettings)
    : wxPanel(parent, wxID_ANY), m_epgMgr(epgMgr),
      m_showAutoUpdateSettings(showAutoUpdateSettings), m_dirty(false) {
  SetupUI();
}

EpgSourceManagerPanel::~EpgSourceManagerPanel() {}

void EpgSourceManagerPanel::SetupUI() {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // Список источников
  m_sourceList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition,
                                wxSize(FromDIP(300), FromDIP(150)),
                                wxLC_REPORT | wxLC_SINGLE_SEL);
  m_sourceList->InsertColumn(0, "URL", wxLIST_FORMAT_LEFT, FromDIP(300));
  m_sourceList->InsertColumn(1, "Name", wxLIST_FORMAT_LEFT, FromDIP(150));
  m_sourceList->InsertColumn(2, "Last Update", wxLIST_FORMAT_LEFT,
                             FromDIP(150));
  m_sourceList->InsertColumn(3, "Status", wxLIST_FORMAT_LEFT, FromDIP(80));
  mainSizer->Add(m_sourceList, 1, wxEXPAND | wxALL, FromDIP(5));

  // Кнопки управления
  wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
  m_addBtn = new wxButton(this, ID_ADD_SOURCE, "Add");
  m_editBtn = new wxButton(this, ID_EDIT_SOURCE, "Edit");
  m_removeBtn = new wxButton(this, ID_REMOVE_SOURCE, "Remove");
  m_refreshBtn = new wxButton(this, ID_REFRESH_SOURCE, "Refresh Now");
  m_deleteCacheBtn = new wxButton(this, ID_DELETE_CACHE, "Delete Cache");

  btnSizer->Add(m_addBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(m_editBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(m_removeBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(m_refreshBtn, 0, wxRIGHT, FromDIP(5));
  // Activity Indicator
  m_activityIndicator = new wxActivityIndicator(this, wxID_ANY);
  m_activityIndicator->Hide();
  btnSizer->Add(m_activityIndicator, 0, wxLEFT | wxALIGN_CENTER_VERTICAL,
                FromDIP(5));

  // Кнопка Cancel
  m_cancelBtn = new wxButton(this, wxID_ANY, "Cancel");
  m_cancelBtn->Hide();
  m_cancelBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    if (m_epgMgr) {
      m_epgMgr->AbortDownload();
      m_epgMgr->CancelMatching();
    }
    SetRefreshing(false);
  });
  btnSizer->Add(m_cancelBtn, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL,
                FromDIP(5));

  btnSizer->Add(m_deleteCacheBtn, 0);
  mainSizer->Add(btnSizer, 0, wxBOTTOM, FromDIP(5));

  // Опционально: настройки автообновления
  if (m_showAutoUpdateSettings) {
    wxStaticBox *settingsBox =
        new wxStaticBox(this, wxID_ANY, "Auto-update settings");
    wxStaticBoxSizer *settingsSizer =
        new wxStaticBoxSizer(settingsBox, wxVERTICAL);

    wxFlexGridSizer *grid = new wxFlexGridSizer(2, FromDIP(5), FromDIP(10));
    grid->AddGrowableCol(1);

    // Все элементы должны иметь родителем settingsBox, а не this
    grid->Add(new wxStaticText(settingsBox, wxID_ANY, "Auto-update:"), 0,
              wxALIGN_CENTER_VERTICAL);
    m_autoUpdateCheck = new wxCheckBox(settingsBox, wxID_ANY, "");
    grid->Add(m_autoUpdateCheck, 0, wxEXPAND);

    grid->Add(
        new wxStaticText(settingsBox, wxID_ANY, "Update interval (hours):"), 0,
        wxALIGN_CENTER_VERTICAL);
    m_updateIntervalSpin =
        new wxSpinCtrl(settingsBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       wxSize(FromDIP(80), -1));
    m_updateIntervalSpin->SetRange(1, 168);
    m_updateIntervalSpin->SetValue(24);
    grid->Add(m_updateIntervalSpin, 0, wxEXPAND);

    grid->Add(new wxStaticText(settingsBox, wxID_ANY, "Days to keep in cache:"),
              0, wxALIGN_CENTER_VERTICAL);
    m_daysToKeepSpin =
        new wxSpinCtrl(settingsBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       wxSize(FromDIP(80), -1));
    m_daysToKeepSpin->SetRange(1, 30);
    m_daysToKeepSpin->SetValue(3);
    grid->Add(m_daysToKeepSpin, 0, wxEXPAND);

    settingsSizer->Add(grid, 0, wxEXPAND | wxALL, FromDIP(5));
    mainSizer->Add(settingsSizer, 0, wxEXPAND | wxALL, FromDIP(5));
  }

  SetSizer(mainSizer);
  UpdateSourceList();
}

void EpgSourceManagerPanel::SetRefreshing(bool refreshing) {
  if (refreshing) {
    m_activityIndicator->Show();
    m_activityIndicator->Start();
    m_cancelBtn->Show();
    m_refreshBtn->Disable();
    m_addBtn->Disable();
    m_editBtn->Disable();
    m_removeBtn->Disable();
    m_deleteCacheBtn->Disable();
  } else {
    m_activityIndicator->Stop();
    m_activityIndicator->Hide();
    m_cancelBtn->Hide();
    m_refreshBtn->Enable();
    m_addBtn->Enable();
    m_editBtn->Enable();
    m_removeBtn->Enable();
    m_deleteCacheBtn->Enable();
  }
  Layout();
}

void EpgSourceManagerPanel::UpdateSourceList() {
  if (!m_epgMgr)
    return;
  auto sources = m_epgMgr->GetSources();
  m_sourceList->DeleteAllItems();
  for (size_t i = 0; i < sources.size(); ++i) {
    long idx = m_sourceList->InsertItem(i, wxString::FromUTF8(sources[i].url));
    m_sourceList->SetItem(idx, 1, wxString::FromUTF8(sources[i].name));
    m_sourceList->SetItem(idx, 2, EpgTime::FormatTime(sources[i].lastUpdate));

    wxString url = wxString::FromUTF8(sources[i].url);
    wxString status;
    if (IsNetworkUrl(url)) {
      status = "online";
    } else {
      status = wxFileExists(url) ? "✔" : "✘ (missing)";
    }
    m_sourceList->SetItem(idx, 3, status);
  }
  m_dirty = false;
}

void EpgSourceManagerPanel::LoadSettings() {
  if (!m_epgMgr || !m_showAutoUpdateSettings)
    return;
  m_autoUpdateCheck->SetValue(m_epgMgr->IsAutoUpdateEnabled());
  m_updateIntervalSpin->SetValue(m_epgMgr->GetUpdateIntervalHours());
  m_daysToKeepSpin->SetValue(m_epgMgr->GetDaysToKeep());
}

void EpgSourceManagerPanel::SaveSettings() {
  if (!m_epgMgr || !m_showAutoUpdateSettings)
    return;
  m_epgMgr->SetAutoUpdateEnabled(m_autoUpdateCheck->GetValue());
  m_epgMgr->SetUpdateIntervalHours(m_updateIntervalSpin->GetValue());
  m_epgMgr->SetDaysToKeep(m_daysToKeepSpin->GetValue());
  m_epgMgr->RestartAutoUpdate();
  m_dirty = false;
}

bool EpgSourceManagerPanel::IsDirty() const { return m_dirty; }

void EpgSourceManagerPanel::EnableButtons(bool enable) {
  m_addBtn->Enable(enable);
  m_editBtn->Enable(enable);
  m_removeBtn->Enable(enable);
  m_refreshBtn->Enable(enable);
  m_deleteCacheBtn->Enable(enable);
}

void EpgSourceManagerPanel::OnAdd(wxCommandEvent &) {
  if (!m_epgMgr) {
    wxMessageBox("EPG Manager not available.", "Error", wxOK | wxICON_ERROR);
    return;
  }

  AddEpgSourceDialog dlg(this);
  if (dlg.ShowModal() != wxID_OK) {
    return;
  }

  wxString urlOrPath = dlg.GetUrlOrPath();
  wxString name = dlg.GetName();
  bool isFile = !IsNetworkUrl(urlOrPath);

  // Проверка дубликата
  auto sources = m_epgMgr->GetSources();
  std::string urlUtf8 = urlOrPath.ToUTF8().data();
  for (const auto &src : sources) {
    if (src.url == urlUtf8) {
      wxMessageBox("This EPG source already exists.", "Info",
                   wxOK | wxICON_INFORMATION);
      return;
    }
  }

  if (name.IsEmpty()) {
    if (isFile) {
      wxFileName fn(urlOrPath);
      name = fn.GetName();
    } else {
      wxString path = urlOrPath.AfterLast('/').BeforeFirst('?');
      if (path.EndsWith(".xml") || path.EndsWith(".gz") ||
          path.EndsWith(".xml.gz"))
        path = path.BeforeLast('.');
      name = path.IsEmpty() ? urlOrPath.BeforeFirst('/').AfterFirst('/') : path;
    }
  }

  EpgSource src;
  src.url = urlUtf8;
  src.name = name.ToUTF8().data();
  src.lastUpdate = 0;
  sources.push_back(src);

  m_epgMgr->SetSources(sources);
  m_epgMgr->SaveSourcesToConfig();

  // Показываем индикатор
  SetRefreshing(true);
  m_epgMgr->Refresh(); // асинхронно

  // Обновляем список источников сразу (без ожидания Refresh)
  UpdateSourceList();
  m_dirty = true;
}

void EpgSourceManagerPanel::OnEdit(wxCommandEvent &) {
  long sel =
      m_sourceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (sel == -1) {
    wxMessageBox("Please select a source to edit.", "Info",
                 wxOK | wxICON_INFORMATION);
    return;
  }

  auto sources = m_epgMgr->GetSources();
  if (sel >= (long)sources.size())
    return;

  wxString oldUrl = wxString::FromUTF8(sources[sel].url);
  wxString oldName = wxString::FromUTF8(sources[sel].name);

  wxTextEntryDialog urlDlg(this, "Edit EPG source URL:", "Edit EPG Source",
                           oldUrl);
  if (urlDlg.ShowModal() != wxID_OK)
    return;
  wxString newUrl = urlDlg.GetValue();

  wxTextEntryDialog nameDlg(this, "Edit source name:", "Edit Source Name",
                            oldName);
  wxString newName;
  if (nameDlg.ShowModal() == wxID_OK)
    newName = nameDlg.GetValue();

  sources[sel].url = newUrl.ToUTF8().data();
  sources[sel].name = newName.ToUTF8().data();
  m_epgMgr->SetSources(sources);
  m_epgMgr->SaveSourcesToConfig();
  UpdateSourceList();
  m_dirty = true;
}

void EpgSourceManagerPanel::OnRemove(wxCommandEvent &) {
  long sel =
      m_sourceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (sel == -1) {
    wxMessageBox("Please select a source to remove.", "Info",
                 wxOK | wxICON_INFORMATION);
    return;
  }

  if (wxMessageBox("Remove selected EPG source?", "Confirm",
                   wxYES_NO | wxICON_QUESTION) != wxYES)
    return;

  auto sources = m_epgMgr->GetSources();
  if (sel < (long)sources.size()) {
    sources.erase(sources.begin() + sel);
    m_epgMgr->SetSources(sources);
    m_epgMgr->SaveSourcesToConfig();
    UpdateSourceList();
    m_dirty = true;
  }
}

void EpgSourceManagerPanel::OnRefresh(wxCommandEvent &) {
  if (m_epgMgr) {
    SetRefreshing(true);
    m_epgMgr->Refresh(); // асинхронно
  }
}

void EpgSourceManagerPanel::OnDeleteCache(wxCommandEvent &) {
  if (!m_epgMgr)
    return;
  if (wxMessageBox("Delete all locally cached EPG data? This will force "
                   "re-download on next update.",
                   "Confirm", wxYES_NO | wxICON_QUESTION) != wxYES) {
    return;
  }
  if (m_epgMgr->DeleteCache()) {
    wxMessageBox("EPG cache deleted.", "Info", wxOK | wxICON_INFORMATION);
    UpdateSourceList();
  } else {
    wxMessageBox("Failed to delete EPG cache.", "Error", wxOK | wxICON_ERROR);
  }
}

void EpgSourceManagerPanel::OnSourceSelected(wxListEvent &) {
  // Можно добавить логику при выборе строки (например, включить/выключить
  // кнопки)
}
