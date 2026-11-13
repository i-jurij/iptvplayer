#include "xtream/PlaylistInfoDialog.h"

#include "Playlist.h"
#include "Utils_Net.h"
#include "Utils_Platform.h"
#include "Utils_UI.h"
#include "xtream/XtreamInfo.h"

#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <ctime>
#include <wx/uri.h>

namespace {

// Собирает новый M3U source из server_info.url + server_info.port
// + path/query исходного source. Возвращает пустую строку, если
// server_info.url невалиден.
wxString BuildSuggestedSource(const Playlist &playlist) {
  if (!playlist.isXtream() || !playlist.getXtreamMeta().hasUserInfo())
    return wxString();

  const auto si =
      xtream::ParseServerInfo(playlist.getXtreamMeta().serverInfoJson);
  if (si.url.empty() || !IsValidServerUrl(wxString::FromUTF8(si.url)))
    return wxString();

  wxURI siUri(wxString::FromUTF8(si.url));

  wxString newOrigin = siUri.GetScheme().Lower() + "://" + siUri.GetServer();
  if (siUri.HasPort())
    newOrigin += ":" + siUri.GetPort();
  else if (!si.port.empty())
    newOrigin += ":" + wxString::FromUTF8(si.port);

  wxString path = siUri.GetPath();
  if (!path.empty() && path != "/")
    newOrigin += path;

  const wxString oldSource = wxString::FromUTF8(playlist.getSource());
  wxURI oldUri(oldSource);

  wxString oldPath = oldUri.GetPath();
  if (oldPath.empty() || oldPath == "/")
    oldPath.clear();

  wxString oldQuery = oldUri.HasQuery() ? oldUri.GetQuery() : wxString();

  wxString result = newOrigin + oldPath;
  if (!oldQuery.empty())
    result += "?" + oldQuery;

  return result;
}

// Origin без path/query.
wxString OriginOf(const wxString &url) {
  if (url.IsEmpty())
    return wxString();
  wxURI uri(url);
  if (!uri.HasScheme() || !uri.HasServer())
    return wxString();
  wxString origin = uri.GetScheme().Lower() + "://" + uri.GetServer();
  if (uri.HasPort())
    origin += ":" + uri.GetPort();
  return origin;
}

} // namespace

PlaylistInfoDialog::PlaylistInfoDialog(wxWindow *parent,
                                       const Playlist &playlist)
    : wxDialog(parent, wxID_ANY, _U("Playlist Info"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_playlistCopy(playlist) {
  BuildUi(m_playlistCopy);
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

  {
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    auto *lbl = new wxStaticText(this, wxID_ANY, _U("Source:"));
    wxFont f = lbl->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    lbl->SetFont(f);
    row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    const wxString sourceStr = wxString::FromUTF8(playlist.getSource());

    auto *urlCtrl = new wxTextCtrl(
        this, wxID_ANY, sourceStr,
        wxDefaultPosition, wxDefaultSize,
        wxTE_READONLY | wxBORDER_NONE);
    urlCtrl->SetBackgroundColour(GetBackgroundColour());
    urlCtrl->SetToolTip(_U("Select and copy with Ctrl+C"));

    {
      wxClientDC dc(this);
      dc.SetFont(GetFont());
      const int textW = dc.GetTextExtent(sourceStr).GetWidth();
      urlCtrl->SetMinSize(wxSize(textW + FromDIP(24), -1));
    }

    row->Add(urlCtrl, 1, wxEXPAND);

    infoBox->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
  }

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
      addRow(accBox, _U("Connections:"),
             wxString::Format(_U("active %d / %d allowed"),
                              ui.activeConnections, ui.maxConnections));
    }

    if (ui.isTrial)
      addRow(accBox, _U("Type:"), _U("Trial account"));

    const xtream::ServerInfo si = xtream::ParseServerInfo(meta.serverInfoJson);
    if (!si.url.empty() && si.url != playlist.getSource()) {
      addRow(accBox, _U("Canonical URL:"), wxString::FromUTF8(si.url));

      const wxString oldSource = wxString::FromUTF8(playlist.getSource());
      const wxString newOrigin = OriginOf(wxString::FromUTF8(si.url));
      const wxString oldOrigin = OriginOf(oldSource);

      if (!newOrigin.IsEmpty() && !oldOrigin.IsEmpty() &&
          newOrigin != oldOrigin) {
        const wxString suggested = BuildSuggestedSource(playlist);
        if (!suggested.IsEmpty()) {
          auto *btnRow = new wxBoxSizer(wxHORIZONTAL);
          auto *btn = new wxButton(this, wxID_ANY, _U("Update source…"));
          btn->SetToolTip(
              _U("Server reports a different canonical address. Open the "
                 "playlist editor with the new URL pre-filled."));
          btn->Bind(wxEVT_BUTTON,
                    [this, suggested](wxCommandEvent &) {
                      if (m_onUpdateSource)
                        m_onUpdateSource(m_playlistCopy, suggested);
                    });
          btnRow->Add(btn, 0, wxLEFT, FromDIP(0));
          accBox->Add(btnRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
        }
      }
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

  {
    const int screenW = wxGetDisplaySize().GetWidth();
    const int maxW = screenW - FromDIP(120);
    wxSize cur = GetSize();
    if (cur.GetWidth() > maxW)
      SetSize(wxSize(maxW, cur.GetHeight()));
  }

  Layout();
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
