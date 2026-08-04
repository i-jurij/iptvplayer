#include "AddEpgSourceDialog.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <wx/clipbrd.h>
#include <wx/dataobj.h> 
#include <wx/event.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>

#include <wx/timer.h>

//-----------------------------------------------------------------------------
AddEpgSourceDialog::AddEpgSourceDialog(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, "Add EPG Source", wxDefaultPosition,
               wxSize(550, -1), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // Информационная строка + кнопка "More info"
  wxBoxSizer *infoSizer = new wxBoxSizer(wxHORIZONTAL);
  infoSizer->Add(new wxStaticText(this, wxID_ANY,
                                  "Enter XMLTV URL or choose a local file."),
                 0, wxALIGN_CENTER_VERTICAL);
  infoSizer->AddStretchSpacer();
  m_infoBtn = new wxButton(this, wxID_ANY, "More info about popular sources →");
  infoSizer->Add(m_infoBtn, 0);
  mainSizer->Add(infoSizer, 0, wxEXPAND | wxALL, 10);

  // Основная сетка
  wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
  grid->AddGrowableCol(1);

  grid->Add(new wxStaticText(this, wxID_ANY, "URL / Path:"), 0,
            wxALIGN_CENTER_VERTICAL);
  wxBoxSizer *rowSizer = new wxBoxSizer(wxHORIZONTAL);
  m_urlCtrl = new wxTextCtrl(this, wxID_ANY, "https://");
  m_browseBtn = new wxButton(this, wxID_ANY, "Browse...");
  rowSizer->Add(m_urlCtrl, 1, wxEXPAND | wxRIGHT, 5);
  rowSizer->Add(m_browseBtn, 0);
  grid->Add(rowSizer, 1, wxEXPAND);

  grid->Add(new wxStaticText(this, wxID_ANY, "Name (optional):"), 0,
            wxALIGN_CENTER_VERTICAL);
  m_nameCtrl = new wxTextCtrl(this, wxID_ANY, "");
  grid->Add(m_nameCtrl, 1, wxEXPAND);

  mainSizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

  wxStaticText *nameNote = new wxStaticText(
      this, wxID_ANY,
      "If empty, name will be auto-filled from URL or file name.");
  nameNote->SetForegroundColour(*wxLIGHT_GREY);
  mainSizer->Add(nameNote, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

  wxSizer *btnSizer = CreateButtonSizer(wxOK | wxCANCEL);
  if (btnSizer)
    mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 10);

  SetSizerAndFit(mainSizer);
  CentreOnParent();

  // Bind events
  m_browseBtn->Bind(wxEVT_BUTTON, &AddEpgSourceDialog::OnBrowse, this);
  m_infoBtn->Bind(wxEVT_BUTTON, &AddEpgSourceDialog::OnInfo, this);
  m_urlCtrl->Bind(wxEVT_TEXT, &AddEpgSourceDialog::AutoFillName, this);
}

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::OnBrowse(wxCommandEvent &) {
  wxFileDialog dlg(this, "Select XMLTV file", wxEmptyString, wxEmptyString,
                   "XMLTV files (*.xml;*.xml.gz;*.gz)|*.xml;*.xml.gz;*.gz|All "
                   "files (*.*)|*.*",
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (dlg.ShowModal() == wxID_OK) {
    m_urlCtrl->SetValue(dlg.GetPath());
    // Имитируем событие текста для автозаполнения
    wxCommandEvent evt(wxEVT_TEXT, m_urlCtrl->GetId());
    AutoFillName(evt);
  }
}

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::OnInfo(wxCommandEvent &) { ShowInfoDialog(); }

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::ShowInfoDialog() {
  wxDialog infoDlg(this, wxID_ANY, "Popular EPG Sources", wxDefaultPosition,
                   wxSize(700, 450), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // ---- Подсказка (вверху) ----
  wxStaticText *hint = new wxStaticText(
      &infoDlg, wxID_ANY,
      "ℹ Double‑click on any row to copy the URL to clipboard.");
  wxFont hintFont = hint->GetFont();
  hintFont.SetWeight(wxFONTWEIGHT_BOLD);
  hint->SetFont(hintFont);
  // цвет не задаём — системный (читаемый)
  mainSizer->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 10);

  // ---- Поиск и парсинг JSON ----
  wxString jsonContent;
  bool found = false;

  wxArrayString searchPaths;
  wxString resourceDir = wxStandardPaths::Get().GetResourcesDir();
  if (!resourceDir.IsEmpty())
    searchPaths.Add(resourceDir);
  wxString exeDir = wxPathOnly(wxStandardPaths::Get().GetExecutablePath());
  if (!exeDir.IsEmpty() && exeDir != resourceDir)
    searchPaths.Add(exeDir);
  wxString cwd = wxGetCwd();
  if (!cwd.IsEmpty() && cwd != resourceDir && cwd != exeDir)
    searchPaths.Add(cwd);
  for (const auto &base : searchPaths) {
    searchPaths.Add(base + "/resources");
  }

  for (const auto &dir : searchPaths) {
    wxFileName candidate(dir, "epg_sources.json");
    if (candidate.FileExists()) {
      wxFile file(candidate.GetFullPath());
      if (file.IsOpened() && file.ReadAll(&jsonContent)) {
        found = true;
        break;
      }
    }
  }

  if (!found || jsonContent.IsEmpty()) {
    wxStaticText *msg = new wxStaticText(
        &infoDlg, wxID_ANY,
        "EPG sources file (epg_sources.json) not found.\n\n"
        "Please check installation or search online for 'XMLTV EPG sources'.");
    msg->SetForegroundColour(*wxRED);
    mainSizer->Add(msg, 1, wxEXPAND | wxALL, 20);
  } else {
    rapidjson::Document doc;
    doc.Parse(jsonContent.ToUTF8().data());
    if (doc.HasParseError()) {
      wxStaticText *err =
          new wxStaticText(&infoDlg, wxID_ANY,
                           "Error parsing epg_sources.json.\n\n"
                           "Please check file format (valid JSON array).");
      err->SetForegroundColour(*wxRED);
      mainSizer->Add(err, 1, wxEXPAND | wxALL, 20);
    } else if (doc.IsArray()) {
      wxListView *list =
          new wxListView(&infoDlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         wxLC_REPORT | wxLC_SINGLE_SEL);
      list->InsertColumn(0, "Region", wxLIST_FORMAT_LEFT, 100);
      list->InsertColumn(1, "Name", wxLIST_FORMAT_LEFT, 150);
      list->InsertColumn(2, "URL", wxLIST_FORMAT_LEFT, 280);
      list->InsertColumn(3, "Notes", wxLIST_FORMAT_LEFT, 150);

      int row = 0;
      for (rapidjson::SizeType i = 0; i < doc.Size(); ++i) {
        const auto &item = doc[i];
        if (!item.IsObject())
          continue;
        long idx = list->InsertItem(
            row, wxString::FromUTF8(item["region"].GetString()));
        list->SetItem(idx, 1, wxString::FromUTF8(item["name"].GetString()));
        list->SetItem(idx, 2, wxString::FromUTF8(item["url"].GetString()));
        list->SetItem(idx, 3, wxString::FromUTF8(item["notes"].GetString()));
        ++row;
      }

      mainSizer->Add(list, 1, wxEXPAND | wxALL, 10);

      // ---- Обработка двойного клика с динамической подсказкой ----
      wxTimer *timer = new wxTimer(&infoDlg);
      timer->SetOwner(&infoDlg, wxID_HIGHEST + 1000);

      list->Bind(wxEVT_LIST_ITEM_ACTIVATED,
                 [hint, timer, list](wxListEvent &evt) {
                   long idx = evt.GetIndex();
                   if (idx == -1)
                     return;
                   wxString url = list->GetItemText(idx, 2);
                   if (!url.IsEmpty() && wxTheClipboard->Open()) {
                     wxTheClipboard->SetData(new wxTextDataObject(url));
                     wxTheClipboard->Close();

                     hint->SetLabel("✓ URL copied to clipboard!");
                     hint->SetForegroundColour(wxColour(0, 180, 0)); // зелёный

                     timer->StartOnce(2000);
                   }
                 });

      infoDlg.Bind(wxEVT_TIMER, [hint, timer](wxTimerEvent &evt) {
        if (evt.GetId() == timer->GetId()) {
          hint->SetLabel(
              "ℹ Double‑click on any row to copy the URL to clipboard.");
          hint->SetForegroundColour(wxNullColour); // возврат к системному цвету
        }
      });

    } else {
      wxStaticText *err = new wxStaticText(
          &infoDlg, wxID_ANY,
          "epg_sources.json must contain a JSON array of objects.");
      err->SetForegroundColour(*wxRED);
      mainSizer->Add(err, 1, wxEXPAND | wxALL, 20);
    }
  }

  wxSizer *btnSizer = infoDlg.CreateButtonSizer(wxOK);
  if (btnSizer)
    mainSizer->Add(btnSizer, 0, wxALL | wxALIGN_RIGHT, 10);

  infoDlg.SetSizerAndFit(mainSizer);
  infoDlg.CentreOnParent();
  infoDlg.ShowModal();
}

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::OnOk(wxCommandEvent &) {
  wxString value = m_urlCtrl->GetValue();
  if (value.IsEmpty()) {
    wxMessageBox("Please enter a URL or select a file.", "Error",
                 wxOK | wxICON_ERROR, this);
    return;
  }
  if (!value.StartsWith("http://") && !value.StartsWith("https://")) {
    if (!wxFileExists(value)) {
      wxMessageBox("File does not exist.", "Error", wxOK | wxICON_ERROR, this);
      return;
    }
  }
  EndModal(wxID_OK);
}

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::AutoFillName(wxCommandEvent &) {
  if (!m_nameCtrl->GetValue().IsEmpty())
    return;

  wxString value = m_urlCtrl->GetValue();
  if (value.IsEmpty())
    return;

  wxString name;
  if (value.StartsWith("http://") || value.StartsWith("https://")) {
    wxString path = value.AfterLast('/').BeforeFirst('?');
    if (path.EndsWith(".xml") || path.EndsWith(".gz") ||
        path.EndsWith(".xml.gz")) {
      path = path.BeforeLast('.');
    }
    name = path.IsEmpty() ? value.BeforeFirst('/').AfterFirst('/') : path;
  } else {
    wxFileName fn(value);
    name = fn.GetName();
  }

  if (!name.IsEmpty()) {
    m_nameCtrl->SetValue(name);
  }
}

//-----------------------------------------------------------------------------
wxString AddEpgSourceDialog::GetUrlOrPath() const {
  return m_urlCtrl->GetValue();
}

//-----------------------------------------------------------------------------
wxString AddEpgSourceDialog::GetName() const { return m_nameCtrl->GetValue(); }

//-----------------------------------------------------------------------------
bool AddEpgSourceDialog::IsFileSource() const {
  wxString val = m_urlCtrl->GetValue();
  return !val.StartsWith("http://") && !val.StartsWith("https://");
}
