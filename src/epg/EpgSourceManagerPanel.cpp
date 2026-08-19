#include "epg/EpgSourceManagerPanel.h"
#include "MainFrame.h"
#include "Utils.h"
#include "epg/AddEpgSourceDialog.h"
#include "epg/EPGData.h"
#include "epg/EPGManager.h"
#include "epg/ManualMappingDialog.h"

#include <wx/dcclient.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>

wxBEGIN_EVENT_TABLE(EpgSourceManagerPanel, wxPanel)
    EVT_BUTTON(ID_ADD_SOURCE, EpgSourceManagerPanel::OnAdd)
        EVT_BUTTON(ID_EDIT_SOURCE, EpgSourceManagerPanel::OnEdit)
            EVT_BUTTON(ID_REMOVE_SOURCE, EpgSourceManagerPanel::OnRemove)
                EVT_BUTTON(ID_REFRESH_SOURCE, EpgSourceManagerPanel::OnRefresh)
                    EVT_BUTTON(ID_DELETE_CACHE,
                               EpgSourceManagerPanel::OnDeleteCache)
                        EVT_LIST_ITEM_SELECTED(
                            wxID_ANY, EpgSourceManagerPanel::OnSourceSelected)
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

  // ---- Блок "EPG Sources" ----
  wxStaticBox *sourceBox = new wxStaticBox(this, wxID_ANY, "EPG Sources");
  wxStaticBoxSizer *sourceBoxSizer =
      new wxStaticBoxSizer(sourceBox, wxVERTICAL);

  // Список источников
  m_sourceList = new wxListCtrl(sourceBox, wxID_ANY, wxDefaultPosition,
                                wxSize(FromDIP(300), FromDIP(150)),
                                wxLC_REPORT | wxLC_SINGLE_SEL);
  // Колонки: URL, Name, Last Update, Status
  m_sourceList->InsertColumn(0, "URL", wxLIST_FORMAT_LEFT, FromDIP(300));
  m_sourceList->InsertColumn(1, "Name", wxLIST_FORMAT_LEFT, FromDIP(150));
  m_sourceList->InsertColumn(2, "Last Update", wxLIST_FORMAT_LEFT,
                             FromDIP(150));
  m_sourceList->InsertColumn(3, "Status", wxLIST_FORMAT_LEFT, FromDIP(80));

  sourceBoxSizer->Add(m_sourceList, 1, wxEXPAND | wxALL, FromDIP(5));

  // Кнопки управления источниками
  wxBoxSizer *btnSizer = new wxBoxSizer(wxHORIZONTAL);
  m_addBtn = new wxButton(sourceBox, ID_ADD_SOURCE, "Add");
  m_editBtn = new wxButton(sourceBox, ID_EDIT_SOURCE, "Edit");
  m_removeBtn = new wxButton(sourceBox, ID_REMOVE_SOURCE, "Remove");
  m_refreshBtn = new wxButton(sourceBox, ID_REFRESH_SOURCE, "Refresh Now");

  btnSizer->Add(m_addBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(m_editBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(m_removeBtn, 0, wxRIGHT, FromDIP(5));
  btnSizer->Add(m_refreshBtn, 0, wxRIGHT, FromDIP(5));

  // Activity Indicator
  m_activityIndicator = new wxActivityIndicator(sourceBox, wxID_ANY);
  m_activityIndicator->Hide();
  btnSizer->Add(m_activityIndicator, 0, wxLEFT | wxALIGN_CENTER_VERTICAL,
                FromDIP(5));

  // Кнопка Cancel
  m_cancelBtn = new wxButton(sourceBox, wxID_ANY, "Cancel");
  m_cancelBtn->Hide();
  m_cancelBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
    if (m_epgMgr) {
      m_epgMgr->AbortDownload();
      m_epgMgr->CancelMatching();
    }
    SetRefreshing(false);
  });
  btnSizer->Add(m_cancelBtn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(5));

  m_manualMapBtn = new wxButton(sourceBox, wxID_ANY, "Manual Mapping...");
  btnSizer->Add(m_manualMapBtn, 0, wxRIGHT, FromDIP(5));
  m_manualMapBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnManualMapping,
                       this);

  sourceBoxSizer->Add(btnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                      FromDIP(5));

  mainSizer->Add(sourceBoxSizer, 1, wxEXPAND | wxALL, FromDIP(15));
  mainSizer->AddSpacer(FromDIP(10));

  // ---- Блок "Cache Management" ----
  wxStaticBox *cacheBox = new wxStaticBox(this, wxID_ANY, "Cache Management");
  wxStaticBoxSizer *cacheBoxSizer = new wxStaticBoxSizer(cacheBox, wxVERTICAL);

  m_deleteCacheBtn = new wxButton(cacheBox, ID_DELETE_CACHE, "Delete Cache");
  cacheBoxSizer->Add(m_deleteCacheBtn, 0, wxLEFT | wxRIGHT | wxBOTTOM,
                     FromDIP(5));

  wxStaticText *cacheNote = new wxStaticText(
      cacheBox, wxID_ANY,
      "Deletes all locally cached EPG data. Next update will re-download.");
  cacheNote->SetForegroundColour(*wxLIGHT_GREY);
  cacheBoxSizer->Add(cacheNote, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));

  mainSizer->Add(cacheBoxSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                 FromDIP(15));
  mainSizer->AddSpacer(FromDIP(10));

  // ---- Блок "Auto-update settings" (опционально) ----
  if (m_showAutoUpdateSettings) {
    wxStaticBox *settingsBox =
        new wxStaticBox(this, wxID_ANY, "Auto-update settings");
    wxStaticBoxSizer *settingsSizer =
        new wxStaticBoxSizer(settingsBox, wxVERTICAL);

    wxFlexGridSizer *grid = new wxFlexGridSizer(2, FromDIP(5), FromDIP(10));

    // Auto-update
    grid->Add(new wxStaticText(settingsBox, wxID_ANY, "Auto-update:"), 0,
              wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
    m_autoUpdateCheck = new wxCheckBox(settingsBox, wxID_ANY, "");
    grid->Add(m_autoUpdateCheck, 0, wxALIGN_LEFT);

    // Update interval
    grid->Add(
        new wxStaticText(settingsBox, wxID_ANY, "Update interval (hours):"), 0,
        wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
    m_updateIntervalSpin =
        new wxSpinCtrl(settingsBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       wxSize(FromDIP(80), -1));
    m_updateIntervalSpin->SetRange(1, 168);
    m_updateIntervalSpin->SetValue(24);
    grid->Add(m_updateIntervalSpin, 0, wxALIGN_LEFT);

    // Days to keep
    grid->Add(new wxStaticText(settingsBox, wxID_ANY, "Days to keep in cache:"),
              0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
    m_daysToKeepSpin =
        new wxSpinCtrl(settingsBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       wxSize(FromDIP(80), -1));
    m_daysToKeepSpin->SetRange(1, 30);
    m_daysToKeepSpin->SetValue(3);
    grid->Add(m_daysToKeepSpin, 0, wxALIGN_LEFT);

    settingsSizer->Add(grid, 0, wxEXPAND | wxALL, FromDIP(5));
    mainSizer->Add(settingsSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                   FromDIP(15));
  }

  // ---- Блок "Match settings" ----
  wxStaticBox *matchBox = new wxStaticBox(this, wxID_ANY, "Match settings");
  wxStaticBoxSizer *matchSizer = new wxStaticBoxSizer(matchBox, wxVERTICAL);

  wxFlexGridSizer *matchGrid = new wxFlexGridSizer(2, FromDIP(5), FromDIP(10));

  // Fuzzy threshold
  matchGrid->Add(
      new wxStaticText(matchBox, wxID_ANY, "Fuzzy threshold (50-100):"), 0,
      wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
  m_fuzzyThresholdSpin =
      new wxSpinCtrl(matchBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxSize(FromDIP(80), -1));
  m_fuzzyThresholdSpin->SetRange(50, 100);
  m_fuzzyThresholdSpin->SetValue(75);
  matchGrid->Add(m_fuzzyThresholdSpin, 0, wxALIGN_LEFT);

  // Substring min length
  matchGrid->Add(
      new wxStaticText(matchBox, wxID_ANY, "Substring min length (1-20):"), 0,
      wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
  m_substringMinLengthSpin =
      new wxSpinCtrl(matchBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxSize(FromDIP(80), -1));
  m_substringMinLengthSpin->SetRange(1, 20);
  m_substringMinLengthSpin->SetValue(6);
  matchGrid->Add(m_substringMinLengthSpin, 0, wxALIGN_LEFT);

  // Substring ratio
  matchGrid->Add(
      new wxStaticText(matchBox, wxID_ANY, "Substring ratio (1-100%):"), 0,
      wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
  m_substringMinRatioSpin =
      new wxSpinCtrl(matchBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxSize(FromDIP(80), -1));
  m_substringMinRatioSpin->SetRange(1, 100);
  m_substringMinRatioSpin->SetValue(30);
  matchGrid->Add(m_substringMinRatioSpin, 0, wxALIGN_LEFT);

  // Min match score
  matchGrid->Add(
      new wxStaticText(matchBox, wxID_ANY, "Min match score (1-100):"), 0,
      wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
  m_minScoreSpin = new wxSpinCtrl(matchBox, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxSize(FromDIP(80), -1));
  m_minScoreSpin->SetRange(1, 100);
  m_minScoreSpin->SetValue(50);
  matchGrid->Add(m_minScoreSpin, 0, wxALIGN_LEFT);

  matchSizer->Add(matchGrid, 0, wxEXPAND | wxALL, FromDIP(5));

  // Кнопка Apply
  m_applyMatchBtn = new wxButton(matchBox, wxID_ANY, "Apply");
  m_applyMatchBtn->Bind(wxEVT_BUTTON, &EpgSourceManagerPanel::OnApplyMatch,
                        this);
  wxBoxSizer *applySizer = new wxBoxSizer(wxHORIZONTAL);
  applySizer->AddStretchSpacer();
  applySizer->Add(m_applyMatchBtn, 0);
  matchSizer->Add(applySizer, 0, wxEXPAND | wxALL, FromDIP(5));

  mainSizer->Add(matchSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                 FromDIP(15));
  
  SetSizer(mainSizer);
  UpdateSourceList();

  // При изменении размера окна пересчитывать ширину колонок
  Bind(wxEVT_SIZE, [this](wxSizeEvent &evt) {
    AdjustColumnWidths();
    evt.Skip();
  });

  // Начальная настройка ширины колонок
  AdjustColumnWidths();

  // Загружаем текущие значения в контролы
  LoadSettings();
}

void EpgSourceManagerPanel::OnManualMapping(wxCommandEvent &) {
  if (!m_epgMgr) {
    wxMessageBox("EPG Manager not available.", "Error", wxOK | wxICON_ERROR,
                 this);
    return;
  }
  if (!m_mainFrame) {
    wxMessageBox("Main frame not available.", "Error", wxOK | wxICON_ERROR,
                 this);
    return;
  }
  ManualMappingDialog dlg(this, m_epgMgr, m_mainFrame);
  dlg.ShowModal();
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
  AdjustColumnWidths();
}

void EpgSourceManagerPanel::LoadSettings() {
  if (!m_epgMgr || !m_showAutoUpdateSettings)
    return;

  m_autoUpdateCheck->SetValue(m_epgMgr->IsAutoUpdateEnabled());
  m_updateIntervalSpin->SetValue(m_epgMgr->GetUpdateIntervalHours());
  m_daysToKeepSpin->SetValue(m_epgMgr->GetDaysToKeep());

  // Загрузка настроек матчинга
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
    wxMessageBox("Match settings applied. Re-matching channels in background.",
                 "Info", wxOK | wxICON_INFORMATION, this);
  } else {
    wxMessageBox("Match settings saved.", "Info", wxOK | wxICON_INFORMATION,
                 this);
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

  SetRefreshing(true);
  m_epgMgr->Refresh(); // асинхронно

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
  // Можно добавить логику при выборе строки
}

// --------------------------------------------------------------------------
// AdjustColumnWidths – адаптивная ширина колонок по содержимому
// --------------------------------------------------------------------------
void EpgSourceManagerPanel::AdjustColumnWidths() {
  if (!m_sourceList)
    return;

  wxClientDC dc(m_sourceList);
  dc.SetFont(m_sourceList->GetFont());

  // Заголовки колонок
  wxString headers[] = {"URL", "Name", "Last Update", "Status"};
  const int colCount = 4;
  // Минимальные и максимальные ширины (в пикселях, с учётом DPI)
  int minWidths[colCount] = {FromDIP(150), FromDIP(100), FromDIP(100),
                             FromDIP(60)};
  int maxWidths[colCount] = {FromDIP(600), FromDIP(300), FromDIP(200),
                             FromDIP(100)};

  int bestWidths[colCount];

  // Вычисляем ширину для каждой колонки
  for (int col = 0; col < colCount; ++col) {
    int maxTextWidth = 0;

    // Ширина заголовка
    wxSize headerSize = dc.GetTextExtent(headers[col]);
    maxTextWidth = headerSize.GetWidth();

    // Ширина всех строк в колонке
    int itemCount = m_sourceList->GetItemCount();
    for (int i = 0; i < itemCount; ++i) {
      wxString text = m_sourceList->GetItemText(i, col);
      wxSize textSize = dc.GetTextExtent(text);
      if (textSize.GetWidth() > maxTextWidth)
        maxTextWidth = textSize.GetWidth();
    }

    // Добавляем небольшой запас (отступы)
    int desired = maxTextWidth + FromDIP(10);
    // Ограничиваем минимумом и максимумом
    if (desired < minWidths[col])
      desired = minWidths[col];
    if (desired > maxWidths[col])
      desired = maxWidths[col];
    bestWidths[col] = desired;
  }

  // Получаем доступную ширину списка
  int totalWidth = m_sourceList->GetClientSize().GetWidth();
  if (totalWidth <= 0) {
    // Если окно ещё не отображено, устанавливаем минимальные/желаемые ширины
    for (int col = 0; col < colCount; ++col) {
      m_sourceList->SetColumnWidth(col, bestWidths[col]);
    }
    return;
  }

  // Сумма желаемых ширин
  int sumDesired = 0;
  for (int col = 0; col < colCount; ++col)
    sumDesired += bestWidths[col];

  if (sumDesired <= totalWidth) {
    // Есть лишнее место – распределяем между URL (0) и Name (1) в пропорции
    // 60/40
    int extra = totalWidth - sumDesired;
    int addToUrl = static_cast<int>(extra * 0.6);
    int addToName = extra - addToUrl;

    // Применяем добавки, но не превышаем максимумы
    int newUrl = bestWidths[0] + addToUrl;
    int newName = bestWidths[1] + addToName;

    if (newUrl > maxWidths[0]) {
      addToName += (newUrl - maxWidths[0]);
      newUrl = maxWidths[0];
    }
    if (newName > maxWidths[1]) {
      // Если имя достигло максимума, остаток некуда добавлять – оставляем
      // пустым
      newName = maxWidths[1];
    }
    bestWidths[0] = std::min(newUrl, maxWidths[0]);
    bestWidths[1] = std::min(newName, maxWidths[1]);
  } else {
    // Если желаемая ширина больше доступной, включаем горизонтальную прокрутку
    // Оставляем желаемые ширины (они могут быть больше клиентской области)
    // Ничего не меняем.
  }

  // Устанавливаем ширины колонок
  for (int col = 0; col < colCount; ++col) {
    m_sourceList->SetColumnWidth(col, bestWidths[col]);
  }
}
