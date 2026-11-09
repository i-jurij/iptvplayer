#include "xtream/PlaylistInfoDialog.h"

#include "Playlist.h"
#include "Utils_Platform.h"
#include "Utils_UI.h"
#include "xtream/XtreamInfo.h"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>

#include <ctime>

PlaylistInfoDialog::PlaylistInfoDialog(wxWindow *parent,
                                       const Playlist &playlist)
    : wxDialog(parent, wxID_ANY, _U("Playlist Info"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  BuildUi(playlist);
}

void PlaylistInfoDialog::BuildUi(const Playlist &playlist) {
  auto *mainSizer = new wxBoxSizer(wxVERTICAL);

  auto addRow = [this](wxSizer *sizer, const wxString &label,
                       const wxString &value) {
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    auto *lbl = new wxStaticText(this, wxID_ANY, label);
    wxFont f = lbl->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    lbl->SetFont(f);
    row->Add(lbl, 0, wxALIGN_TOP | wxRIGHT, FromDIP(8));

    auto *val = new wxStaticText(this, wxID_ANY, value);
    val->Wrap(FromDIP(420));
    row->Add(val, 1, wxEXPAND);

    sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
  };

  auto *infoBox = new wxStaticBoxSizer(wxVERTICAL, this, _U("Playlist"));
  addRow(infoBox, _U("Title:"), wxString::FromUTF8(playlist.getTitle()));
  addRow(infoBox, _U("Source:"), wxString::FromUTF8(playlist.getSource()));
  addRow(infoBox, _U("Channels:"),
         wxString::Format("%zu", playlist.getChannelCount()));
  mainSizer->Add(infoBox, 0, wxEXPAND | wxALL, FromDIP(10));

  if (playlist.isXtream() && playlist.getXtreamMeta().hasUserInfo()) {
    const auto &meta = playlist.getXtreamMeta();
    const xtream::UserInfo ui = xtream::ParseUserInfo(meta.userInfoJson);

    auto *accBox = new wxStaticBoxSizer(wxVERTICAL, this, _U("Account"));

    addRow(accBox, _U("Status:"), wxString::FromUTF8(xtream::HumanStatus(ui)));

    if (ui.hasExpDate()) {
      wxString expStr =
          wxString::FromUTF8(xtream::FormatExpiryDate(ui.expDate));
      const int64_t now = static_cast<int64_t>(std::time(nullptr));
      if (xtream::IsExpiringSoon(ui.expDate, now, 3))
        expStr += " " + _U("(soon!)");
      addRow(accBox, _U("Expiration:"), expStr);
    } else {
      addRow(accBox, _U("Expiration:"), _U("Unlimited"));
    }

    if (ui.maxConnections > 0) {
      addRow(
          accBox, _U("Connections:"),
          wxString::Format("%d / %d", ui.activeConnections, ui.maxConnections));
    }

    if (ui.isTrial)
      addRow(accBox, _U("Type:"), _U("Trial account"));

    const xtream::ServerInfo si = xtream::ParseServerInfo(meta.serverInfoJson);
    if (!si.url.empty() && si.url != playlist.getSource()) {
      addRow(accBox, _U("Canonical URL:"), wxString::FromUTF8(si.url));
    }

    mainSizer->Add(accBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                   FromDIP(10));
  }

  mainSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, FromDIP(6));

  auto *btnSizer = CreateButtonSizer(wxOK);
  if (btnSizer) {
    auto *okBtn = wxDynamicCast(FindWindowById(wxID_OK, this), wxButton);
    if (okBtn)
      okBtn->SetLabel(_U("Close"));
    mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, FromDIP(8));
  }

  SetSizerAndFit(mainSizer);
  CentreOnParent();
}

bool PlaylistInfoDialog::ShouldShowExpiryWarning(const Playlist &playlist) {
  if (!playlist.isXtream() || !playlist.getXtreamMeta().hasUserInfo())
    return false;
  const xtream::UserInfo ui =
      xtream::ParseUserInfo(playlist.getXtreamMeta().userInfoJson);
  if (!ui.hasExpDate())
    return false;
  return xtream::IsExpiringSoon(ui.expDate,
                                static_cast<int64_t>(std::time(nullptr)), 3);
}
