#include "xtream/AddXtreamPlaylistDialog.h"

#include "Utils.h"
#include "xtream/XtreamUrl.h"

#include <wx/sizer.h>
#include <wx/uri.h>
#include <wx/statbox.h>


namespace {

// Trim, prepend http:// если нет схемы, обрезать хвостовые "/".
wxString NormalizeServerUrl(const wxString &raw) {
  wxString url = raw;
  url.Trim(true).Trim(false);
  if (url.IsEmpty())
    return url;
  if (url.Find("://") == wxNOT_FOUND)
    url = "http://" + url;
  while (url.EndsWith("/"))
    url.RemoveLast();
  return url;
}

} // namespace

AddXtreamPlaylistDialog::AddXtreamPlaylistDialog(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, _U("Add Xtream Codes Playlist"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  auto *mainSizer = new wxBoxSizer(wxVERTICAL);

  auto addRow = [](wxWindow *box, wxSizer *sizer, const wxString &label,
                   wxWindow *ctrl) {
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(box, wxID_ANY, label), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, box->FromDIP(5));
    row->Add(ctrl, 1, wxEXPAND);
    sizer->Add(row, 0, wxEXPAND | wxALL, box->FromDIP(5));
  };

  auto *reqSizer = AddSectionHeader(this, mainSizer, _U("Required"));
  wxWindow *reqBox = reqSizer->GetStaticBox();

  m_serverCtrl = new wxTextCtrl(reqBox, wxID_ANY, "");
  m_serverCtrl->SetHint("example.com:8080");
  addRow(reqBox, reqSizer, _U("Server URL:"), m_serverCtrl);

  m_userCtrl = new wxTextCtrl(reqBox, wxID_ANY, "");
  m_userCtrl->SetHint(_U("Required for bare server URL"));
  addRow(reqBox, reqSizer, _U("Username:"), m_userCtrl);

  m_passCtrl = new wxTextCtrl(reqBox, wxID_ANY, "", wxDefaultPosition,
                              wxDefaultSize, wxTE_PASSWORD);
  m_passCtrl->SetHint(_U("Required for bare server URL"));
  addRow(reqBox, reqSizer, _U("Password:"), m_passCtrl);

  mainSizer->AddSpacer(FromDIP(10));

  auto *optSizer = AddSectionHeader(this, mainSizer, _U("Optional"));
  wxWindow *optBox = optSizer->GetStaticBox();

  m_titleCtrl = new wxTextCtrl(optBox, wxID_ANY, "");
  m_titleCtrl->SetHint(_U("Optional: leave empty to use server host"));
  addRow(optBox, optSizer, _U("Title:"), m_titleCtrl);

  m_epgUrlCtrl = new wxTextCtrl(optBox, wxID_ANY, "");
  m_epgUrlCtrl->SetHint(_U("Auto if empty"));
  addRow(optBox, optSizer, _U("EPG URL:"), m_epgUrlCtrl);

  m_autoUpdateCheck =
      new wxCheckBox(optBox, wxID_ANY, _U("Auto-update on startup"));
  m_autoUpdateCheck->SetValue(true);
  m_autoUpdateCheck->SetToolTip(
      _U("Playlist will be re-downloaded from its source "
         "every time the application starts."));
  optSizer->Add(m_autoUpdateCheck, 0, wxALL, FromDIP(5));

  m_errorLabel = new wxStaticText(this, wxID_ANY, "");
  m_errorLabel->SetForegroundColour(*wxRED);
  m_errorLabel->Hide();
  mainSizer->Add(m_errorLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
                 FromDIP(10));

  mainSizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL,
                 FromDIP(10));

  SetSizer(mainSizer);
  SetClientSize(wxSize(DIALOG_WIDTH, mainSizer->GetMinSize().GetHeight()));
  Layout();

  Bind(wxEVT_BUTTON, &AddXtreamPlaylistDialog::OnOK, this, wxID_OK);
  m_serverCtrl->Bind(wxEVT_TEXT, &AddXtreamPlaylistDialog::OnServerUrlChanged,
                     this);
}

void AddXtreamPlaylistDialog::ShowInlineError(const wxString &msg) {
  m_errorLabel->SetLabel(msg);
  m_errorLabel->Show();
  Layout();
}

void AddXtreamPlaylistDialog::ClearInlineError() {
  if (m_errorLabel->IsShown()) {
    m_errorLabel->SetLabel("");
    m_errorLabel->Hide();
    Layout();
  }
}

void AddXtreamPlaylistDialog::OnServerUrlChanged(wxCommandEvent &) {
  // Динамический inline для panel_api.php — до нажатия OK.
  if (m_serverCtrl->GetValue().Lower().Contains("panel_api.php"))
    ShowInlineError(_U("Reseller panel endpoint, not for playback."));
  else
    ClearInlineError();
}

void AddXtreamPlaylistDialog::OnOK(wxCommandEvent &event) {
  ClearInlineError();

  const wxString server = NormalizeServerUrl(m_serverCtrl->GetValue());
  if (server.IsEmpty()) {
    ShowInlineError(_U("Please enter Server URL."));
    return;
  }

  if (!IsNetworkUrl(server)) {
    ShowInlineError(_U("Not a network URL."));
    return;
  }
  if (!IsHttpUrl(server)) {
    ShowInlineError(_U("Xtream requires HTTP/HTTPS."));
    return;
  }

  wxURI uri(server);
  if (!uri.HasServer() || uri.GetServer().IsEmpty()) {
    ShowInlineError(_U("Invalid host."));
    return;
  }
  if (server.Lower().Contains("panel_api.php")) {
    ShowInlineError(_U("Reseller panel endpoint, not for playback."));
    return;
  }

  if (!HasNonTrivialPath(server)) {
    if (GetUsername().IsEmpty()) {
      ShowInlineError(_U("Please enter Username."));
      return;
    }
    if (GetPassword().IsEmpty()) {
      ShowInlineError(_U("Please enter Password."));
      return;
    }
  }

  event.Skip();
}

wxString AddXtreamPlaylistDialog::GetServerUrl() const {
  return NormalizeServerUrl(m_serverCtrl->GetValue());
}

wxString AddXtreamPlaylistDialog::GetUsername() const {
  return m_userCtrl->GetValue().Trim();
}

wxString AddXtreamPlaylistDialog::GetPassword() const {
  return m_passCtrl->GetValue().Trim();
}

wxString AddXtreamPlaylistDialog::GetTitle() const {
  const wxString title = m_titleCtrl->GetValue().Trim();
  if (!title.IsEmpty())
    return title;
  const wxString server = GetServerUrl();
  wxURI uri(server);
  if (uri.HasServer() && !uri.GetServer().IsEmpty())
    return uri.GetServer();
  return server;
}

wxString AddXtreamPlaylistDialog::GetEpgUrl() const {
  const wxString epgValue = m_epgUrlCtrl->GetValue().Trim();
  if (!epgValue.IsEmpty())
    return epgValue;

  const wxString server = GetServerUrl();
  if (server.IsEmpty())
    return wxString();

  if (HasNonTrivialPath(server)) {
    const UrlCredentials creds = ExtractQueryCredentials(server);
    if (creds.valid && !creds.user.IsEmpty() && !creds.pass.IsEmpty())
      return xtream::BuildXmltv(GetOrigin(server), creds.user, creds.pass);
    return wxString();
  }

  const wxString user = GetUsername();
  const wxString pass = GetPassword();
  if (user.IsEmpty() || pass.IsEmpty())
    return wxString();
  return xtream::BuildXmltv(server, user, pass);
}

bool AddXtreamPlaylistDialog::GetAutoUpdate() const {
  return m_autoUpdateCheck->GetValue();
}
