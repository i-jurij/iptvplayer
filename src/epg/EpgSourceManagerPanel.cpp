#include "epg/EpgSourceManagerPanel.h"
#include "MainFrame.h"
#include "Utils.h"
#include "epg/AddEpgSourceDialog.h"
#include "epg/EPGData.h"
#include "epg/EPGManager.h"
#include "epg/ManualMappingDialog.h"

#include <wx/dcclient.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textdlg.h>

EpgSourceManagerPanel::EpgSourceManagerPanel(wxWindow *parent,
                                             EPGManager *epgMgr,
                                             bool showAutoUpdateSettings)
    : wxPanel(parent, wxID_ANY), m_epgMgr(epgMgr),
      m_showAutoUpdateSettings(showAutoUpdateSettings), m_dirty(false) {
  SetupUI();

  // Подписка на прогресс
  if (m_epgMgr) {
    m_epgMgr->AddOnProgress([this](const EpgProgressInfo &info) {
      wxTheApp->CallAfter([this, info]() { OnProgressUpdate(info); });
    });
  }

  // Привязки событий через Bind (без таблицы)
  m_sourceList->Bind(wxEVT_LIST_ITEM_SELECTED,
                     &EpgSourceManagerPanel::OnSourceSelected, this);
  m_addBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnAdd, this);
  m_editBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnEdit, this);
  m_removeBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnRemove, this);
  m_refreshBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnRefresh, this);
  m_deleteCacheBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnDeleteCache,
                         this);
  m_applyMatchBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnApplyMatch,
                        this);
  m_cancelBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    if (m_epgMgr) {
      m_epgMgr->AbortDownload();
      m_epgMgr->CancelMatching();
    }
    // SetBusy(false);
  });
  m_autoApplyBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnAutoApply, this);
}

EpgSourceManagerPanel::~EpgSourceManagerPanel() {}

void EpgSourceManagerPanel::OnAutoApply(wxCommandEvent &) {
  SaveSettings();
  wxMessageBox(_("Auto-update settings applied."), _("Info"),
               wxOK | wxICON_INFORMATION, this);
}

void EpgSourceManagerPanel::SetupUI() {
  // Создаём прокручиваемую область
  m_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxVSCROLL);
  m_scrolledWindow->SetScrollRate(5, 5);

  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // ---- Блок "EPG Sources" ----
  wxStaticBox *sourceBox =
      new wxStaticBox(m_scrolledWindow, wxID_ANY, _("EPG Sources"));
  wxStaticBoxSizer *sourceBoxSizer =
      new wxStaticBoxSizer(sourceBox, wxVERTICAL);

  m_sourceList = new wxListCtrl(sourceBox, wxID_ANY, wxDefaultPosition,
                                wxSize(FromDIP(300), FromDIP(150)),
                                wxLC_REPORT | wxLC_SINGLE_SEL);
  m_sourceList->InsertColumn(0, _("URL"), wxLIST_FORMAT_LEFT, FromDIP(300));
  m_sourceList->InsertColumn(1, _("Name"), wxLIST_FORMAT_LEFT, FromDIP(150));
  m_sourceList->InsertColumn(2, _("Imported"), wxLIST_FORMAT_LEFT,
                             FromDIP(150));
  m_sourceList->InsertColumn(3, _("Availability"), wxLIST_FORMAT_LEFT,
                             FromDIP(100));
  m_sourceList->InsertColumn(4, _("Auto"), wxLIST_FORMAT_CENTER, FromDIP(50));
  m_sourceList->SetMinSize(wxSize(-1, FromDIP(150)));

  m_sourceList->Bind(wxEVT_LIST_ITEM_ACTIVATED,
                     &EpgSourceManagerPanel::OnToggleAutoUpdate, this);

  sourceBoxSizer->Add(m_sourceList, 1, wxEXPAND | wxALL, FromDIP(5));

  // ---- Кнопки управления источниками ----
  wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
  m_addBtn = new wxButton(sourceBox, wxID_ANY, _("Add"));
  m_editBtn = new wxButton(sourceBox, wxID_ANY, _("Edit"));
  m_removeBtn = new wxButton(sourceBox, wxID_ANY, _("Remove"));
  m_refreshBtn = new wxButton(sourceBox, wxID_ANY, _("Refresh Now"));

  btnSizer->Add(m_addBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(m_editBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(m_removeBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(m_refreshBtn, 0, wxRIGHT, FromDIP(5));

  // ---- Activity Indicator ----
  m_activityIndicator = new wxActivityIndicator(sourceBox, wxID_ANY);
  m_activityIndicator->Hide();
  btnSizer->Add(m_activityIndicator, 0, wxLEFT | wxALIGN_CENTER_VERTICAL,
                FromDIP(5));

  // ---- Кнопка Cancel ----
  m_cancelBtn = new wxButton(sourceBox, wxID_ANY, _("Cancel"));
  m_cancelBtn->Hide();
  btnSizer->Add(m_cancelBtn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(5));

  sourceBoxSizer->Add(btnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                      FromDIP(5));

  mainSizer->Add(sourceBoxSizer, 1, wxEXPAND | wxALL, FromDIP(15));
  mainSizer->AddSpacer(FromDIP(5));

  // ---- Блок "Cache Management" ----
  wxStaticBox *cacheBox =
      new wxStaticBox(m_scrolledWindow, wxID_ANY, _("Cache Management"));
  wxStaticBoxSizer *cacheBoxSizer = new wxStaticBoxSizer(cacheBox, wxVERTICAL);
  wxStaticText *cacheNote = new wxStaticText(
      cacheBox, wxID_ANY,
      _("Deletes all locally cached EPG data. Next update will re-download."));
  cacheNote->SetForegroundColour(*wxLIGHT_GREY);
  cacheBoxSizer->Add(cacheNote, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
  m_deleteCacheBtn =
      new wxButton(cacheBox, wxID_ANY, _("Delete cache / Update all"));
  cacheBoxSizer->Add(m_deleteCacheBtn, 0, wxLEFT | wxRIGHT | wxBOTTOM,
                     FromDIP(10));
  mainSizer->Add(cacheBoxSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                 FromDIP(15));
  mainSizer->AddSpacer(FromDIP(10));

  // ---- Блок "Auto-update settings" ----
  if (m_showAutoUpdateSettings) {
    wxStaticBox *settingsBox =
        new wxStaticBox(m_scrolledWindow, wxID_ANY, _("Auto-update settings"));
    wxStaticBoxSizer *settingsSizer =
        new wxStaticBoxSizer(settingsBox, wxVERTICAL);

    wxFlexGridSizer *grid = new wxFlexGridSizer(2, FromDIP(5), FromDIP(10));

    grid->Add(new wxStaticText(settingsBox, wxID_ANY, _("Auto-update:")), 0,
              wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
    m_autoUpdateCheck = new wxCheckBox(settingsBox, wxID_ANY, "");
    grid->Add(m_autoUpdateCheck, 0, wxALIGN_LEFT);

    grid->Add(
        new wxStaticText(settingsBox, wxID_ANY, _("Update interval (hours):")),
        0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
    m_updateIntervalSpin =
        new wxSpinCtrl(settingsBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       wxSize(FromDIP(120), -1));
    m_updateIntervalSpin->SetRange(1, 168);
    m_updateIntervalSpin->SetValue(24);
    grid->Add(m_updateIntervalSpin, 0, wxALIGN_LEFT);

    grid->Add(
        new wxStaticText(settingsBox, wxID_ANY, _("Days to keep in cache:")), 0,
        wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
    m_daysToKeepSpin =
        new wxSpinCtrl(settingsBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       wxSize(FromDIP(120), -1));
    m_daysToKeepSpin->SetRange(1, 30);
    m_daysToKeepSpin->SetValue(3);
    grid->Add(m_daysToKeepSpin, 0, wxALIGN_LEFT);

    settingsSizer->Add(grid, 0, wxEXPAND | wxALL, FromDIP(5));

    // ---- Кнопка Apply ----
    m_autoApplyBtn = new wxButton(settingsBox, wxID_ANY, _("Apply"));
    m_autoApplyBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnAutoApply,
                         this);

    wxBoxSizer *applySizer = new wxBoxSizer(wxHORIZONTAL);
    applySizer->Add(m_autoApplyBtn, 0, wxLEFT, FromDIP(5));
    settingsSizer->Add(applySizer, 0, wxEXPAND | wxALL, FromDIP(5));

    mainSizer->Add(settingsSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                   FromDIP(15));
  }

  // ---- Блок "Match settings" ----
  wxStaticBox *matchBox =
      new wxStaticBox(m_scrolledWindow, wxID_ANY, _("Match settings"));
  wxStaticBoxSizer *matchSizer = new wxStaticBoxSizer(matchBox, wxVERTICAL);

  // Кнопки над контролами
  wxBoxSizer *topMatchSizer = new wxBoxSizer(wxHORIZONTAL);
  m_aboutMatchBtn = new wxButton(matchBox, wxID_ANY, _("About matching"));
  m_editRulesBtn = new wxButton(matchBox, wxID_ANY, _("Edit matching rules"));
  topMatchSizer->Add(m_aboutMatchBtn, 0, wxRIGHT, FromDIP(10));
  topMatchSizer->Add(m_editRulesBtn, 0);
  matchSizer->Add(topMatchSizer, 0, wxEXPAND | wxALL, FromDIP(10));

  m_aboutMatchBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnAboutMatch,
                        this);
  m_editRulesBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnEditRules, this);

  // controls grid
  wxFlexGridSizer *matchGrid = new wxFlexGridSizer(2, FromDIP(5), FromDIP(10));

  matchGrid->Add(
      new wxStaticText(matchBox, wxID_ANY, _("Fuzzy threshold (50-100):")), 0,
      wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
  m_fuzzyThresholdSpin =
      new wxSpinCtrl(matchBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxSize(FromDIP(120), -1));
  m_fuzzyThresholdSpin->SetRange(50, 100);
  m_fuzzyThresholdSpin->SetValue(75);
  matchGrid->Add(m_fuzzyThresholdSpin, 0, wxALIGN_LEFT);

  matchGrid->Add(
      new wxStaticText(matchBox, wxID_ANY, _("Substring min length (1-20):")),
      0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
  m_substringMinLengthSpin =
      new wxSpinCtrl(matchBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxSize(FromDIP(120), -1));
  m_substringMinLengthSpin->SetRange(1, 20);
  m_substringMinLengthSpin->SetValue(6);
  matchGrid->Add(m_substringMinLengthSpin, 0, wxALIGN_LEFT);

  matchGrid->Add(
      new wxStaticText(matchBox, wxID_ANY, _("Substring ratio (1-100%):")), 0,
      wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
  m_substringMinRatioSpin =
      new wxSpinCtrl(matchBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxSize(FromDIP(120), -1));
  m_substringMinRatioSpin->SetRange(1, 100);
  m_substringMinRatioSpin->SetValue(30);
  matchGrid->Add(m_substringMinRatioSpin, 0, wxALIGN_LEFT);

  matchGrid->Add(
      new wxStaticText(matchBox, wxID_ANY, _("Min match score (1-100):")), 0,
      wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
  m_minScoreSpin = new wxSpinCtrl(matchBox, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxSize(FromDIP(120), -1));
  m_minScoreSpin->SetRange(1, 100);
  m_minScoreSpin->SetValue(50);
  matchGrid->Add(m_minScoreSpin, 0, wxALIGN_LEFT);

  matchSizer->Add(matchGrid, 0, wxEXPAND | wxALL, FromDIP(5));

  m_applyMatchBtn = new wxButton(matchBox, wxID_ANY, _("Apply (Match Now)"));
  wxBoxSizer *applySizer = new wxBoxSizer(wxHORIZONTAL);
  applySizer->Add(m_applyMatchBtn, 0, wxLEFT, FromDIP(5));
  matchSizer->Add(applySizer, 0, wxEXPAND | wxALL, FromDIP(5));

  mainSizer->Add(matchSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                 FromDIP(15));

  m_scrolledWindow->SetSizer(mainSizer);
  m_scrolledWindow->SetVirtualSize(mainSizer->GetMinSize());
  m_scrolledWindow->FitInside();

  wxBoxSizer *panelSizer = new wxBoxSizer(wxVERTICAL);
  panelSizer->Add(m_scrolledWindow, 1, wxEXPAND);
  SetSizer(panelSizer);

  UpdateSourceList();
  AdjustColumnWidths();

  Bind(wxEVT_SIZE, [this](wxSizeEvent &evt) {
    AdjustColumnWidths();
    evt.Skip();
  });

  LoadSettings();
}

// === Обработчики событий ===
void EpgSourceManagerPanel::OnToggleAutoUpdate(wxListEvent &event) {
  long idx = event.GetIndex();
  if (idx == -1)
    return;
  if (!m_epgMgr)
    return;

  auto sources = m_epgMgr->GetSources();
  if (idx >= (long)sources.size())
    return;

  sources[idx].autoUpdate = !sources[idx].autoUpdate;
  m_epgMgr->SetSources(sources);
  m_epgMgr->SaveSourcesToConfig();
  UpdateSourceList();
}

void EpgSourceManagerPanel::OnAdd(wxCommandEvent &) {
  if (!m_epgMgr) {
    wxMessageBox(_("EPG Manager not available."), _("Error"),
                 wxOK | wxICON_ERROR);
    return;
  }

  AddEpgSourceDialog dlg(this);
  if (dlg.ShowModal() != wxID_OK)
    return;

  wxString urlOrPath = dlg.GetUrlOrPath();
  wxString name = dlg.GetName();
  bool isFile = !IsNetworkUrl(urlOrPath);

  auto sources = m_epgMgr->GetSources();
  std::string urlUtf8 = urlOrPath.ToUTF8().data();
  for (const auto &src : sources) {
    if (src.url == urlUtf8) {
      wxMessageBox(_("This EPG source already exists."), _("Info"),
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
  src.autoUpdate = dlg.GetAutoUpdate();
  sources.push_back(src);

  m_epgMgr->SetSources(sources);
  m_epgMgr->SaveSourcesToConfig();
  m_dirty = true;

  // Запускаем асинхронную загрузку нового источника
  RefreshSourceInternal(urlUtf8, name.ToUTF8().data());
}

void EpgSourceManagerPanel::OnEdit(wxCommandEvent &) {
  long sel =
      m_sourceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (sel == -1) {
    wxMessageBox(_("Please select a source to edit."), _("Info"),
                 wxOK | wxICON_INFORMATION);
    return;
  }

  auto sources = m_epgMgr->GetSources();
  if (sel >= (long)sources.size())
    return;

  AddEpgSourceDialog dlg(this);
  dlg.SetTitle(_("Edit EPG Source"));
  dlg.SetUrlOrPath(wxString::FromUTF8(sources[sel].url));
  dlg.SetName(wxString::FromUTF8(sources[sel].name));
  dlg.SetAutoUpdate(sources[sel].autoUpdate);

  if (dlg.ShowModal() != wxID_OK)
    return;

  sources[sel].url = dlg.GetUrlOrPath().ToUTF8().data();
  sources[sel].name = dlg.GetName().ToUTF8().data();
  sources[sel].autoUpdate = dlg.GetAutoUpdate();

  m_epgMgr->SetSources(sources);
  m_epgMgr->SaveSourcesToConfig();
  UpdateSourceList();
  m_dirty = true;
}

void EpgSourceManagerPanel::OnRemove(wxCommandEvent &) {
  long sel =
      m_sourceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (sel == -1) {
    wxMessageBox(_("Please select a source to remove."), _("Info"),
                 wxOK | wxICON_INFORMATION);
    return;
  }

  if (wxMessageBox(_("Remove selected EPG source?"), _("Confirm"),
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
  long selected =
      m_sourceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (selected == -1) {
    wxMessageBox(_("Please select a source to refresh."), _("Info"),
                 wxOK | wxICON_INFORMATION, this);
    return;
  }
  wxString urlWx = m_sourceList->GetItemText(selected, 0);
  wxString nameWx = m_sourceList->GetItemText(selected, 1);
  RefreshSourceInternal(urlWx.ToUTF8().data(), nameWx.ToUTF8().data());
}

void EpgSourceManagerPanel::OnDeleteCache(wxCommandEvent &) {
  if (!m_epgMgr)
    return;

  wxDialog dlg(this, wxID_ANY, _("Delete EPG Cache"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

  wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

  wxString msg =
      _("EPG cache contains all downloaded programmes and channel mappings.\n"
        "Deleting will completely clear this data. You will need to reload EPG "
        "from sources.\n\n"
        "Updating all sources may take several minutes (depending on number of "
        "sources).\n"
        "You can also update individual sources later using the \"Refresh "
        "Now\" button.");
  wxStaticText *info = new wxStaticText(&dlg, wxID_ANY, msg);
  info->Wrap(FromDIP(460));
  topSizer->Add(info, 0, wxALL | wxEXPAND, FromDIP(12));

  wxCheckBox *updateCheck =
      new wxCheckBox(&dlg, wxID_ANY, _("Update all sources now"));
  updateCheck->SetValue(false);
  topSizer->Add(updateCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

  wxSizer *btnSizer = dlg.CreateButtonSizer(wxOK | wxCANCEL);
  if (btnSizer) {
    wxButton *okBtn =
        wxDynamicCast(wxWindow::FindWindowById(wxID_OK, &dlg), wxButton);
    if (okBtn)
      okBtn->SetLabel(_("Delete"));
    topSizer->Add(btnSizer, 0, wxALL | wxALIGN_RIGHT, FromDIP(12));
  }

  dlg.SetSizerAndFit(topSizer);
  dlg.CentreOnParent();

  if (dlg.ShowModal() != wxID_OK)
    return;

  if (!m_epgMgr->DeleteCache()) {
    wxMessageBox(_("Failed to delete EPG cache."), _("Error"),
                 wxOK | wxICON_ERROR, this);
    return;
  }

  UpdateSourceList();

  if (updateCheck->GetValue()) {
    auto sources = m_epgMgr->GetSources();
    if (!sources.empty()) {
      SetBusy(true);
      m_epgMgr->UpdateAllSources(false);
    }
  } else {
    wxMessageBox(_("EPG cache cleared. You can now refresh sources manually."),
                 _("Info"), wxOK | wxICON_INFORMATION, this);
  }
}

void EpgSourceManagerPanel::OnSourceSelected(wxListEvent &) {}

// === Методы управления ===
void EpgSourceManagerPanel::UpdateSourceList() {
  if (!m_epgMgr)
    return;
  auto sources = m_epgMgr->GetSources();
  m_sourceList->DeleteAllItems();

  std::unordered_set<std::string> currentUrls;
  for (const auto &src : sources) {
    currentUrls.insert(src.url);
  }
  for (auto it = m_availabilityCache.begin();
       it != m_availabilityCache.end();) {
    if (currentUrls.find(it->first) == currentUrls.end()) {
      it = m_availabilityCache.erase(it);
    } else {
      ++it;
    }
  }

  for (size_t i = 0; i < sources.size(); ++i) {
    long idx = m_sourceList->InsertItem(i, wxString::FromUTF8(sources[i].url));
    m_sourceList->SetItem(idx, 1, wxString::FromUTF8(sources[i].name));

    if (sources[i].lastUpdate > 0) {
      m_sourceList->SetItem(idx, 2, EpgTime::FormatTime(sources[i].lastUpdate));
    } else {
      m_sourceList->SetItem(idx, 2, "");
    }

    wxString url = wxString::FromUTF8(sources[i].url);
    wxString avail;
    if (IsNetworkUrl(url)) {
      auto it = m_availabilityCache.find(sources[i].url);
      if (it != m_availabilityCache.end()) {
        const auto &entry = it->second;
        if (entry.available) {
          avail = _("online");
        } else {
          if (entry.failCount >= 5) {
            avail = _("offline (permanent)");
          } else {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - entry.timestamp);
            if (elapsed.count() >= 60) {
              avail = _("checking...");
              CheckAvailabilityAsync(sources[i].url);
            } else {
              avail = _("offline");
            }
          }
        }
      } else {
        avail = _("checking...");
        CheckAvailabilityAsync(sources[i].url);
      }
    } else {
      avail = wxFileExists(url) ? _("✔") : _("✘ (missing)");
    }
    m_sourceList->SetItem(idx, 3, avail);
    m_sourceList->SetItem(idx, 4, sources[i].autoUpdate ? _("✔") : "");
  }

  m_dirty = false;
  AdjustColumnWidths();
}

void EpgSourceManagerPanel::CheckAvailabilityAsync(const std::string &url) {
  (void)std::async(std::launch::async, [this, url]() {
    auto result = CheckUrlAvailability(url, "", 5, 100 * 1024 * 1024);
    wxTheApp->CallAfter([this, url, available = result.available]() {
      OnAvailabilityChecked(url, available);
    });
  });
}

void EpgSourceManagerPanel::OnAvailabilityChecked(const std::string &url,
                                                  bool available) {
  auto &entry = m_availabilityCache[url];
  entry.timestamp = std::chrono::steady_clock::now();
  if (available) {
    entry.available = true;
    entry.failCount = 0;
  } else {
    entry.available = false;
    entry.failCount++;
  }

  for (long i = 0; i < m_sourceList->GetItemCount(); ++i) {
    wxString itemUrl = m_sourceList->GetItemText(i, 0);
    if (itemUrl.ToUTF8().data() == url) {
      wxString status;
      if (entry.available) {
        status = _("online");
      } else {
        if (entry.failCount >= 5) {
          status = _("offline (permanent)");
        } else {
          status = _("offline");
        }
      }
      m_sourceList->SetItem(i, 3, status);
      break;
    }
  }
}

void EpgSourceManagerPanel::LoadSettings() {
  if (!m_epgMgr || !m_showAutoUpdateSettings)
    return;

  m_autoUpdateCheck->SetValue(m_epgMgr->IsAutoUpdateEnabled());
  m_updateIntervalSpin->SetValue(m_epgMgr->GetUpdateIntervalHours());
  m_daysToKeepSpin->SetValue(m_epgMgr->GetDaysToKeep());

  if (m_fuzzyThresholdSpin)
    m_fuzzyThresholdSpin->SetValue(m_epgMgr->GetFuzzyThreshold());
  if (m_substringMinLengthSpin)
    m_substringMinLengthSpin->SetValue(m_epgMgr->GetSubstringMinLength());
  if (m_substringMinRatioSpin)
    m_substringMinRatioSpin->SetValue(m_epgMgr->GetSubstringMinRatio());
  if (m_minScoreSpin)
    m_minScoreSpin->SetValue(m_epgMgr->GetMinMatchScore());
}

void EpgSourceManagerPanel::SaveMatchSettings() {
  if (!m_epgMgr)
    return;
  m_epgMgr->SetFuzzyThreshold(m_fuzzyThresholdSpin->GetValue());
  m_epgMgr->SetSubstringMinLength(m_substringMinLengthSpin->GetValue());
  m_epgMgr->SetSubstringMinRatio(m_substringMinRatioSpin->GetValue());
  m_epgMgr->SetMinMatchScore(m_minScoreSpin->GetValue());
  m_dirty = true;
}

void EpgSourceManagerPanel::OnApplyMatch(wxCommandEvent &) {
  SaveMatchSettings();
  if (m_epgMgr) {
    m_epgMgr->ReMatchCurrentPlaylist();
    wxMessageBox(
        _("Match settings applied. Re-matching channels in background."),
        _("Info"), wxOK | wxICON_INFORMATION, this);
  } else {
    wxMessageBox(_("Match settings saved."), _("Info"),
                 wxOK | wxICON_INFORMATION, this);
  }
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

void EpgSourceManagerPanel::AdjustColumnWidths() {
  if (!m_sourceList)
    return;

  wxClientDC dc(m_sourceList);
  dc.SetFont(m_sourceList->GetFont());

  wxString headers[] = {_("URL"), _("Name"), _("Imported"), _("Availability")};
  const int colCount = 4;
  int minWidths[colCount] = {FromDIP(150), FromDIP(100), FromDIP(100),
                             FromDIP(60)};
  int maxWidths[colCount] = {FromDIP(600), FromDIP(300), FromDIP(200),
                             FromDIP(100)};

  int bestWidths[colCount];

  for (int col = 0; col < colCount; ++col) {
    int maxTextWidth = 0;
    wxSize headerSize = dc.GetTextExtent(headers[col]);
    maxTextWidth = headerSize.GetWidth();

    int itemCount = m_sourceList->GetItemCount();
    for (int i = 0; i < itemCount; ++i) {
      wxString text = m_sourceList->GetItemText(i, col);
      wxSize textSize = dc.GetTextExtent(text);
      if (textSize.GetWidth() > maxTextWidth)
        maxTextWidth = textSize.GetWidth();
    }

    int desired = maxTextWidth + FromDIP(10);
    if (desired < minWidths[col])
      desired = minWidths[col];
    if (desired > maxWidths[col])
      desired = maxWidths[col];
    bestWidths[col] = desired;
  }

  int totalWidth = m_sourceList->GetClientSize().GetWidth();
  if (totalWidth <= 0) {
    for (int col = 0; col < colCount; ++col) {
      m_sourceList->SetColumnWidth(col, bestWidths[col]);
    }
    return;
  }

  int sumDesired = 0;
  for (int col = 0; col < colCount; ++col)
    sumDesired += bestWidths[col];

  if (sumDesired <= totalWidth) {
    int extra = totalWidth - sumDesired;
    int addToUrl = static_cast<int>(extra * 0.6);
    int addToName = extra - addToUrl;

    int newUrl = bestWidths[0] + addToUrl;
    int newName = bestWidths[1] + addToName;

    if (newUrl > maxWidths[0]) {
      addToName += (newUrl - maxWidths[0]);
      newUrl = maxWidths[0];
    }
    if (newName > maxWidths[1]) {
      newName = maxWidths[1];
    }
    bestWidths[0] = std::min(newUrl, maxWidths[0]);
    bestWidths[1] = std::min(newName, maxWidths[1]);
  }

  for (int col = 0; col < colCount; ++col) {
    m_sourceList->SetColumnWidth(col, bestWidths[col]);
  }
}

// === Методы прогресса ===
void EpgSourceManagerPanel::OnProgressUpdate(const EpgProgressInfo &info) {
  bool isBusy = (info.stage != EpgProgressStage::None &&
                 info.stage != EpgProgressStage::Done &&
                 info.stage != EpgProgressStage::Cancelled &&
                 info.stage != EpgProgressStage::Error);
  SetBusy(isBusy);
}

void EpgSourceManagerPanel::SetBusy(bool busy) {
  if (m_busy == busy)
    return;
  m_busy = busy;

  // Управление индикатором активности
  if (busy) {
    m_activityIndicator->Show();
    m_activityIndicator->Start();
  } else {
    m_activityIndicator->Stop();
    m_activityIndicator->Hide();
  }

  // Кнопка Cancel – показываем только во время занятости
  m_cancelBtn->Show(busy);

  // Блокировка/разблокировка всех интерактивных элементов
  m_addBtn->Enable(!busy);
  m_editBtn->Enable(!busy);
  m_removeBtn->Enable(!busy);
  m_refreshBtn->Enable(!busy);
  m_deleteCacheBtn->Enable(!busy);
  m_applyMatchBtn->Enable(!busy);
  m_autoApplyBtn->Enable(!busy);
  if (m_autoUpdateCheck)
    m_autoUpdateCheck->Enable(!busy);
  if (m_updateIntervalSpin)
    m_updateIntervalSpin->Enable(!busy);
  if (m_daysToKeepSpin)
    m_daysToKeepSpin->Enable(!busy);
  if (m_fuzzyThresholdSpin)
    m_fuzzyThresholdSpin->Enable(!busy);
  if (m_substringMinLengthSpin)
    m_substringMinLengthSpin->Enable(!busy);
  if (m_substringMinRatioSpin)
    m_substringMinRatioSpin->Enable(!busy);
  if (m_minScoreSpin)
    m_minScoreSpin->Enable(!busy);
  m_sourceList->Enable(!busy);

  Layout();
}

void EpgSourceManagerPanel::RefreshSourceInternal(
    const std::string &url, const std::string &name,
    std::function<void(bool)> onComplete) {
  if (!m_epgMgr) {
    if (onComplete)
      onComplete(false);
    return;
  }

  m_epgMgr->RefreshSourceAsync(
      url, name,
      [this, url, name, onComplete](bool success, const std::string &error) {
        if (success) {
          auto sources = m_epgMgr->GetSources();
          for (auto &src : sources) {
            if (src.url == url) {
              src.lastUpdate = EpgTime::GetCurrentUtcEpoch();
              break;
            }
          }
          m_epgMgr->SetSources(sources);
          m_epgMgr->SaveSourcesToConfig();
          UpdateSourceList();

          if (m_mainFrame) {
            std::string playlistId = m_mainFrame->GetCurrentPlaylistId();
            if (!playlistId.empty()) {
              m_epgMgr->ReMatchCurrentPlaylist();
            }
          }
        } else {
          wxMessageBox(_("Failed to update source: ") + wxString::FromUTF8(error),
                       _("Error"), wxOK | wxICON_ERROR, this);
        }
        if (onComplete)
          onComplete(success);
      });
}

void EpgSourceManagerPanel::OnAboutMatch(wxCommandEvent &) {
  wxString msg =
      _("EPG Matching Algorithm\n\n"
        "1. Manual mappings (highest priority)\n"
        "2. TVG-ID match (if present)\n"
        "3. Exact name match (after normalization)\n"
        "4. Substring match (if one name contains the other)\n"
        "5. Token-sort (percentage of common words)\n"
        "6. Jaro-Winkler refinement for borderline cases\n\n"
        "Adjustable parameters (visible below):\n"
        "  • Fuzzy threshold (50–100) – influences Jaro-Winkler bonus\n"
        "  • Substring min length – minimum length for substring match\n"
        "  • Substring ratio – percentage threshold (not used directly, kept "
        "for compatibility)\n"
        "  • Min match score – required score (0–100) to accept a match\n\n"
        "Score thresholds:\n"
        "  • 85+ : High confidence\n"
        "  • 70+ : Medium confidence\n"
        "  • 60+ : Low confidence\n\n"
        "For advanced tuning, you can manually edit the configuration files\n"
        "‘matching_rules.json’ located in the config directory.\n"
        "The default settings are already optimised – it is strongly "
        "recommended to use\n"
        "manual mappings or aliases (in ‘channel_aliases.json’) for specific "
        "corrections instead of modifying these files.");
  wxMessageBox(msg, _("About EPG Matching"), wxOK | wxICON_INFORMATION, this);
}

void EpgSourceManagerPanel::OnEditRules(wxCommandEvent &) {
  if (!m_epgMgr) {
    wxMessageBox(_("EPG Manager not available."), _("Error"),
                 wxOK | wxICON_ERROR, this);
    return;
  }

  wxString configDir = m_epgMgr->GetConfigDirectory();
  if (configDir.IsEmpty()) {
    wxMessageBox(_("Could not determine config directory."), _("Error"),
                 wxOK | wxICON_ERROR, this);
    return;
  }

  wxFileName rulesFile(configDir, "matching_rules.json");
  wxFileName aliasesFile(configDir, "channel_aliases.json");

  wxString rulesPath = rulesFile.GetFullPath();
  wxString aliasesPath = aliasesFile.GetFullPath();

  wxDialog dlg(this, wxID_ANY, _("Edit Matching Rules"), wxDefaultPosition,
               wxSize(FromDIP(600), FromDIP(400)),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

  int screenHeight = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y);
  int maxHeight = static_cast<int>(screenHeight * 0.8);
  dlg.SetMaxSize(wxSize(-1, maxHeight));

  wxScrolledWindow *scrolled = new wxScrolledWindow(
      &dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
  scrolled->SetScrollRate(0, 5);
  wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);
  scrolled->SetSizer(topSizer);

  // Жирное оранжевое предупреждение о перезапуске
  wxString warningText =
      _("After editing, restart the application for changes to take effect.");
  wxStaticText *warningTextCtrl =
      new wxStaticText(scrolled, wxID_ANY, warningText);
  wxFont warningFont = warningTextCtrl->GetFont();
  warningFont.SetWeight(wxFONTWEIGHT_BOLD);
  warningTextCtrl->SetFont(warningFont);
  warningTextCtrl->SetForegroundColour(wxColour(255, 165, 0)); // оранжевый
  topSizer->Add(warningTextCtrl, 0, wxEXPAND | wxALL, FromDIP(10));

  // Основной текст
  wxString info = wxString::Format(
      _("You can fine-tune matching by editing the following JSON files in the "
        "config directory:\n\n"
        "  • %s\n"
        "  • %s\n\n"
        "These files contain advanced parameters (token_high, jaro_high, "
        "stopwords, etc.).\n"
        "The default values are already well-tuned. It is recommended to use "
        "manual mappings or aliases\n"
        "for specific channel corrections instead of modifying these files.\n\n"
        "If the files are missing, built‑in defaults are used."),
      rulesPath, aliasesPath);

  wxStaticText *infoText = new wxStaticText(scrolled, wxID_ANY, info);
  infoText->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
  infoText->Wrap(FromDIP(560));
  topSizer->Add(infoText, 0, wxEXPAND | wxALL, FromDIP(10));

  // Кнопки
  wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
  wxButton *openFolderBtn =
      new wxButton(scrolled, wxID_ANY, _("Open Config Folder"));
  wxButton *openRulesBtn =
      new wxButton(scrolled, wxID_ANY, _("Open matching_rules.json"));
  wxButton *openAliasesBtn =
      new wxButton(scrolled, wxID_ANY, _("Open channel_aliases.json"));
  wxButton *closeBtn = new wxButton(scrolled, wxID_OK, _("Close"));

  btnSizer->Add(openFolderBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(openRulesBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(openAliasesBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(closeBtn, 0);
  topSizer->Add(btnSizer, 0, wxALL | wxALIGN_CENTER, FromDIP(10));

  openFolderBtn->Bind(wxEVT_BUTTON, [configDir](wxCommandEvent &) {
    wxLaunchDefaultApplication(configDir);
  });
  openRulesBtn->Bind(wxEVT_BUTTON, [rulesPath](wxCommandEvent &) {
    wxLaunchDefaultApplication(rulesPath);
  });
  openAliasesBtn->Bind(wxEVT_BUTTON, [aliasesPath](wxCommandEvent &) {
    wxLaunchDefaultApplication(aliasesPath);
  });

  wxBoxSizer *dlgSizer = new wxBoxSizer(wxVERTICAL);
  dlgSizer->Add(scrolled, 1, wxEXPAND);
  dlg.SetSizer(dlgSizer);

  dlg.Fit();
  wxSize currentSize = dlg.GetSize();
  if (currentSize.GetHeight() > maxHeight) {
    dlg.SetSize(wxSize(currentSize.GetWidth(), maxHeight));
    scrolled->FitInside();
  }

  dlg.CentreOnParent();
  dlg.ShowModal();
}

