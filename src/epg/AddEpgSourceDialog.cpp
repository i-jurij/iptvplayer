#include "AddEpgSourceDialog.h"
#include "Utils.h"

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
    : wxDialog(parent, wxID_ANY, _("Add EPG Source"), wxDefaultPosition,
               wxSize(FromDIP(550), -1),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // ---- Кнопка "More info" (сверху, выровнена вправо) ----
  wxBoxSizer *topSizer = new wxBoxSizer(wxHORIZONTAL);
  topSizer->AddStretchSpacer();
  m_infoBtn =
      new wxButton(this, wxID_ANY, _("More info about popular sources →"));
  topSizer->Add(m_infoBtn, 0);
  mainSizer->Add(topSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

  // ---- Информационная строка  ----
  wxBoxSizer *infoSizer = new wxBoxSizer(wxHORIZONTAL);
  infoSizer->Add(new wxStaticText(this, wxID_ANY,
                                  _("Enter XMLTV URL or choose a local file.")),
                 0, wxALIGN_CENTER_VERTICAL);
  mainSizer->Add(infoSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

  // Основная сетка
  wxFlexGridSizer *grid = new wxFlexGridSizer(2, FromDIP(5), FromDIP(10));
  grid->AddGrowableCol(1);

  grid->Add(new wxStaticText(this, wxID_ANY, _("URL / Path:")), 0,
            wxALIGN_CENTER_VERTICAL);
  wxBoxSizer *rowSizer = new wxBoxSizer(wxHORIZONTAL);
  m_urlCtrl = new wxTextCtrl(this, wxID_ANY, "https://");
  m_browseBtn = new wxButton(this, wxID_ANY, _("Browse..."));
  rowSizer->Add(m_urlCtrl, 1, wxEXPAND | wxRIGHT, FromDIP(5));
  rowSizer->Add(m_browseBtn, 0);
  grid->Add(rowSizer, 1, wxEXPAND);

  grid->Add(new wxStaticText(this, wxID_ANY, _("Name (optional):")), 0,
            wxALIGN_CENTER_VERTICAL);
  m_nameCtrl = new wxTextCtrl(this, wxID_ANY, "");
  grid->Add(m_nameCtrl, 1, wxEXPAND);

  // ---- Чекбокс "Auto-update" ----
  grid->Add(new wxStaticText(this, wxID_ANY, ""), 0, wxALIGN_CENTER_VERTICAL);
  m_autoUpdateCheck = new wxCheckBox(this, wxID_ANY, _("Auto-update"));
  m_autoUpdateCheck->SetValue(false);
  grid->Add(m_autoUpdateCheck, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

  mainSizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

  wxStaticText *nameNote = new wxStaticText(
      this, wxID_ANY,
      _("If empty, name will be auto-filled from URL or file name."));
  nameNote->SetForegroundColour(*wxLIGHT_GREY);
  mainSizer->Add(nameNote, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

  wxSizer *btnSizer = CreateButtonSizer(wxOK | wxCANCEL);
  if (btnSizer)
    mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, FromDIP(10));

  SetSizerAndFit(mainSizer);
  CentreOnParent();

  // Bind events
  m_browseBtn->Bind(wxEVT_BUTTON, &AddEpgSourceDialog::OnBrowse, this);
  m_infoBtn->Bind(wxEVT_BUTTON, &AddEpgSourceDialog::OnInfo, this);
  m_urlCtrl->Bind(wxEVT_TEXT, &AddEpgSourceDialog::AutoFillName, this);
}

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::OnBrowse(wxCommandEvent &) {
  wxFileDialog dlg(
      this, _("Select XMLTV file"), wxEmptyString, wxEmptyString,
      _("XMLTV files (*.xml;*.xml.gz;*.gz)|*.xml;*.xml.gz;*.gz|All "
        "files (*.*)|*.*"),
      wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (dlg.ShowModal() == wxID_OK) {
    m_urlCtrl->SetValue(dlg.GetPath());
    wxCommandEvent evt(wxEVT_TEXT, m_urlCtrl->GetId());
    AutoFillName(evt);
  }
}

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::OnInfo(wxCommandEvent &) { ShowInfoDialog(); }

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::ShowInfoDialog() {
  wxDialog infoDlg(this, wxID_ANY, _("Popular EPG Sources"), wxDefaultPosition,
                   wxSize(FromDIP(700), FromDIP(450)),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // ---- Подсказка (вверху) ----
  wxStaticText *hint = new wxStaticText(
      &infoDlg, wxID_ANY,
      _("ℹ Double‑click on any row to copy the URL to clipboard.\n"
        "Note: URLs may become outdated; you can search for current XMLTV "
        "sources online."));
  wxFont hintFont = hint->GetFont();
  hintFont.SetWeight(wxFONTWEIGHT_BOLD);
  hint->SetFont(hintFont);
  mainSizer->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM,
                 infoDlg.FromDIP(10));

  // ---- Поиск и парсинг JSON ----
  wxString jsonPath = FindResourceFile("epg_sources.json");
  wxString jsonContent;
  bool found = false;
  if (!jsonPath.IsEmpty()) {
    wxFile file(jsonPath);
    if (file.IsOpened() && file.ReadAll(&jsonContent)) {
      found = true;
    }
  }

  if (!found || jsonContent.IsEmpty()) {
    wxStaticText *msg =
        new wxStaticText(&infoDlg, wxID_ANY,
                         _("EPG sources file (epg_sources.json) not found.\n\n"
                           "Please check installation or search online for "
                           "'XMLTV EPG sources'."));
    msg->SetForegroundColour(*wxRED);
    mainSizer->Add(msg, 1, wxEXPAND | wxALL, infoDlg.FromDIP(20));
  } else {
    rapidjson::Document doc;
    doc.Parse(jsonContent.ToUTF8().data());
    if (doc.HasParseError()) {
      wxStaticText *err =
          new wxStaticText(&infoDlg, wxID_ANY,
                           _("Error parsing epg_sources.json.\n\n"
                             "Please check file format (valid JSON array)."));
      err->SetForegroundColour(*wxRED);
      mainSizer->Add(err, 1, wxEXPAND | wxALL, infoDlg.FromDIP(20));
    } else if (doc.IsArray()) {
      wxListView *list =
          new wxListView(&infoDlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         wxLC_REPORT | wxLC_SINGLE_SEL);
      list->InsertColumn(0, _("Region"), wxLIST_FORMAT_LEFT,
                         infoDlg.FromDIP(100));
      list->InsertColumn(1, _("Name"), wxLIST_FORMAT_LEFT,
                         infoDlg.FromDIP(150));
      list->InsertColumn(2, _("URL"), wxLIST_FORMAT_LEFT, infoDlg.FromDIP(280));
      list->InsertColumn(3, _("Notes"), wxLIST_FORMAT_LEFT,
                         infoDlg.FromDIP(150));

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

      mainSizer->Add(list, 1, wxEXPAND | wxALL, infoDlg.FromDIP(10));

      // ---- Обработка двойного клика ----
      wxTimer *timer = new wxTimer(&infoDlg);
      timer->SetOwner(&infoDlg, wxID_HIGHEST + 1000);
      infoDlg.Bind(wxEVT_CLOSE_WINDOW, [timer](wxCloseEvent &evt) {
        timer->Stop();
        evt.Skip();
      });
      infoDlg.Bind(
          wxEVT_BUTTON,
          [timer](wxCommandEvent &evt) {
            if (evt.GetId() == wxID_OK) {
              timer->Stop();
            }
            evt.Skip();
          },
          wxID_OK);

      list->Bind(
          wxEVT_LIST_ITEM_ACTIVATED, [hint, timer, list](wxListEvent &evt) {
            long idx = evt.GetIndex();
            if (idx == -1)
              return;
            wxString url = list->GetItemText(idx, 2);
            if (!url.IsEmpty() && wxTheClipboard->Open()) {
              wxTheClipboard->SetData(new wxTextDataObject(url));
              wxTheClipboard->Close();

              hint->SetLabel(_("✓ URL copied to clipboard!\n"
                               "Note: URLs may become outdated; you can "
                               "search for current XMLTV sources online."));
              hint->SetForegroundColour(wxColour(0, 180, 0));
              timer->StartOnce(2000);
            }
          });

      infoDlg.Bind(wxEVT_TIMER, [hint, timer](wxTimerEvent &evt) {
        if (evt.GetId() == timer->GetId()) {
          hint->SetLabel(
              _("ℹ Double‑click on any row to copy the URL to clipboard.\n"
                "Note: URLs may become outdated; you can search for current "
                "XMLTV sources online."));
          hint->SetForegroundColour(wxNullColour);
        }
      });

    } else {
      wxStaticText *err = new wxStaticText(
          &infoDlg, wxID_ANY,
          _("epg_sources.json must contain a JSON array of objects."));
      err->SetForegroundColour(*wxRED);
      mainSizer->Add(err, 1, wxEXPAND | wxALL, infoDlg.FromDIP(20));
    }
  }

  wxSizer *btnSizer = infoDlg.CreateButtonSizer(wxOK);
  if (btnSizer)
    mainSizer->Add(btnSizer, 0, wxALL | wxALIGN_RIGHT, infoDlg.FromDIP(10));

  infoDlg.SetSizerAndFit(mainSizer);
  infoDlg.CentreOnParent();
  infoDlg.ShowModal();
}

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::OnOk(wxCommandEvent &) {
  wxString value = m_urlCtrl->GetValue();
  if (value.IsEmpty()) {
    wxMessageBox(_("Please enter a URL or select a file."), _("Error"),
                 wxOK | wxICON_ERROR, this);
    return;
  }
  if (!IsNetworkUrl(value)) {
    wxFileName fn(value);
    if (!fn.IsAbsolute()) {
      fn.MakeAbsolute();
      value = fn.GetFullPath();
    }
    if (!wxFileExists(value)) {
      wxMessageBox(_("File does not exist."), _("Error"), wxOK | wxICON_ERROR,
                   this);
      return;
    }
    m_urlCtrl->SetValue(value);
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
void AddEpgSourceDialog::SetUrlOrPath(const wxString &url) {
  m_urlCtrl->SetValue(url);
}

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::SetName(const wxString &name) {
  m_nameCtrl->SetValue(name);
}

//-----------------------------------------------------------------------------
void AddEpgSourceDialog::SetAutoUpdate(bool value) {
  m_autoUpdateCheck->SetValue(value);
}
