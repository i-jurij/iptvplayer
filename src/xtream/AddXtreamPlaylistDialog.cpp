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

  // ---- User-Agent (editable combobox with presets) ----
  struct UaPreset {
    wxString label;
    wxString ua;
  };
  const std::vector<UaPreset> presets = {
      {_U("mpv (default)"), xtream::DefaultUserAgent()},
      {_U("VLC"), "VLC/3.0.20 LibVLC/3.0.20"},
      {_U("TiviMate"), "TiviMate/4.7.0 (Linux;Android 9) ExoPlayerLib/2.18.1"},
      {_U("Kodi"), "Kodi/20.5.0 (Windows NT 10.0; Win64; x64) App_Bitness/64 "
                   "Version/20.5.0"},
      {_U("ExoPlayer"), "ExoPlayerLib/2.18.1"},
      {_U("Chrome"),
       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
       "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"},
  };

  m_uaCombo = new wxComboBox(optBox, wxID_ANY, "", wxDefaultPosition,
                             wxDefaultSize, 0, nullptr, wxCB_DROPDOWN);
  for (const auto &p : presets)
    m_uaCombo->Append(p.label);
  m_uaCombo->SetValue(xtream::DefaultUserAgent());
  m_uaCombo->Bind(wxEVT_COMBOBOX, [this, presets](wxCommandEvent &evt) {
    const int sel = evt.GetSelection();
    if (sel < 0 || sel >= static_cast<int>(presets.size()))
      return;
    // wxEVT_COMBOBOX срабатывает даже если selection не изменился
    // (например, пользователь отредактировал текст, потом кликнул тот же
    // preset). Восстанавливаем UA, если текст отличается.
    if (m_uaCombo->GetValue() != presets[sel].ua) {
      m_uaCombo->SetValue(presets[sel].ua);
      m_uaCombo->SetSelection(sel);
    }
  });
  addRow(optBox, optSizer, _U("User Agent:"), m_uaCombo);

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

  const wxString path = uri.GetPath().Lower();
  if (path.EndsWith(".ts") || path.EndsWith(".mp4") || path.EndsWith(".mkv") ||
      path.EndsWith(".avi") || path.EndsWith(".mp3")) {
    ShowInlineError(_U("This looks like a stream URL (video/audio file). "
                       "Use the server address (http://host:port) instead."));
    return;
  }

  // Creds: query > input fields.
  const UrlCredentials qc = ExtractQueryCredentials(server);
  m_probeServer = server;
  m_probeUser = (qc.valid && !qc.user.IsEmpty()) ? qc.user : GetUsername();
  m_probePass = (qc.valid && !qc.pass.IsEmpty()) ? qc.pass : GetPassword();

  if (m_probeUser.IsEmpty()) {
    ShowInlineError(_U("Please enter Username."));
    return;
  }
  if (m_probePass.IsEmpty()) {
    ShowInlineError(_U("Please enter Password."));
    return;
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
  if (uri.HasServer() && !uri.GetServer().IsEmpty()) {
    wxString host = uri.GetServer();
    if (uri.HasPort())
      host += ":" + uri.GetPort();
    return host;
  }
  return server;
}

wxString AddXtreamPlaylistDialog::GetUserAgent() const {
  const wxString ua = m_uaCombo ? m_uaCombo->GetValue().Trim() : wxString();
  if (!ua.IsEmpty())
    return ua;
  return xtream::DefaultUserAgent();
}

wxString AddXtreamPlaylistDialog::GetEpgUrl() const {
  const wxString epgValue = m_epgUrlCtrl->GetValue().Trim();
  if (!epgValue.IsEmpty())
    return epgValue;

  const wxString server = GetServerUrl();
  if (server.IsEmpty())
    return wxString();

  const UrlCredentials qc = ExtractQueryCredentials(server);
  const wxString user =
      (qc.valid && !qc.user.IsEmpty()) ? qc.user : GetUsername();
  const wxString pass =
      (qc.valid && !qc.pass.IsEmpty()) ? qc.pass : GetPassword();
  if (user.IsEmpty() || pass.IsEmpty())
    return wxString();

  const wxString base = xtream::StripPhpSuffix(server);
  if (base.IsEmpty())
    return wxString();
  return xtream::BuildXmltv(base, user, pass);
}

bool AddXtreamPlaylistDialog::GetAutoUpdate() const {
  return m_autoUpdateCheck->GetValue();
}
