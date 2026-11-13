#include "xtream/VodInfoPanel.h"

#include "LogControl.h"
#include "LogoCache.h"
#include "Utils_Platform.h"
#include "Utils_UI.h"

#include <rapidjson/document.h>

#include <wx/button.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>

namespace {

wxString Str(const std::string &s) { return wxString::FromUTF8(s); }

wxString FormatTimeShort(double seconds) {
  if (seconds < 0 || std::isnan(seconds) || std::isinf(seconds))
    return "00:00";
  const int total = static_cast<int>(std::round(seconds));
  const int h = total / 3600;
  const int m = (total % 3600) / 60;
  const int s = total % 60;
  if (h > 0)
    return wxString::Format("%d:%02d:%02d", h, m, s);
  return wxString::Format("%02d:%02d", m, s);
}

std::string GetStr(const rapidjson::Value &v, const char *key) {
  if (!v.IsObject() || !v.HasMember(key))
    return std::string();
  const auto &f = v[key];
  if (f.IsString())
    return f.GetString();
  return std::string();
}

double GetDouble(const rapidjson::Value &v, const char *key) {
  if (!v.IsObject() || !v.HasMember(key))
    return 0.0;
  const auto &f = v[key];
  if (f.IsDouble())
    return f.GetDouble();
  if (f.IsInt())
    return static_cast<double>(f.GetInt());
  if (f.IsString()) {
    try {
      return std::stod(f.GetString());
    } catch (...) {
      return 0.0;
    }
  }
  return 0.0;
}

} // namespace

VodInfoPanel::VodInfoPanel(wxWindow *parent) : wxPanel(parent) {
  BuildUi();
  Clear();
}

void VodInfoPanel::BuildUi() {
  SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

  auto *outer = new wxBoxSizer(wxVERTICAL);

  m_scroll = new wxScrolledWindow(this, wxID_ANY);
  m_scroll->SetScrollRate(0, 10);
  m_scroll->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

  auto *sizer = new wxBoxSizer(wxVERTICAL);

  m_poster = new wxStaticBitmap(m_scroll, wxID_ANY, wxBitmap());
  m_poster->SetMinSize(wxSize(FromDIP(kPosterW), FromDIP(kPosterH)));
  sizer->Add(m_poster, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(10));

  m_titleText = new wxStaticText(m_scroll, wxID_ANY, "");
  {
    wxFont f = m_titleText->GetFont();
    f.SetPointSize(f.GetPointSize() + 2);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    m_titleText->SetFont(f);
  }
  sizer->Add(m_titleText, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

  m_metaText = new wxStaticText(m_scroll, wxID_ANY, "");
  {
    wxFont f = m_metaText->GetFont();
    f.SetStyle(wxFONTSTYLE_ITALIC);
    m_metaText->SetFont(f);
    m_metaText->SetForegroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  }
  sizer->Add(m_metaText, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

  auto makeRow = [&](wxStaticText *&label, const wxString &hint) {
    label = new wxStaticText(m_scroll, wxID_ANY, "");
    wxFont f = label->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    label->SetFont(f);
    (void)hint;
    sizer->Add(label, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
  };

  makeRow(m_genreText, "Genre");
  makeRow(m_directorText, "Director");
  makeRow(m_castText, "Cast");

  m_plotHeader = new wxStaticText(m_scroll, wxID_ANY, _U("Plot"));
  {
    wxFont f = m_plotHeader->GetFont();
    f.SetWeight(wxFONTWEIGHT_BOLD);
    m_plotHeader->SetFont(f);
  }
  sizer->Add(m_plotHeader, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

  m_plotText = new wxStaticText(m_scroll, wxID_ANY, "");
  sizer->Add(m_plotText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
             FromDIP(10));

  auto *btnRow = new wxBoxSizer(wxHORIZONTAL);

  m_playBtn = new wxButton(m_scroll, wxID_ANY,
                           wxString::FromUTF8("\xE2\x96\xB6 ") + _U("Play"));
  m_playBtn->Bind(wxEVT_BUTTON, &VodInfoPanel::OnPlay, this);
  btnRow->Add(m_playBtn, 0, wxRIGHT, FromDIP(8));

  m_playFromBeginningBtn =
      new wxButton(m_scroll, wxID_ANY, _U("Play from beginning"));
  m_playFromBeginningBtn->Bind(wxEVT_BUTTON, &VodInfoPanel::OnPlayFromBeginning,
                               this);
  m_playFromBeginningBtn->Hide();
  btnRow->Add(m_playFromBeginningBtn, 0, wxRIGHT, FromDIP(8));

  m_trailerBtn = new wxButton(m_scroll, wxID_ANY, _U("Trailer"));

  m_trailerBtn->Bind(wxEVT_BUTTON, &VodInfoPanel::OnTrailer, this);
  m_trailerBtn->Hide();
  btnRow->Add(m_trailerBtn, 0);

  sizer->Add(btnRow, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(10));

  m_scroll->SetSizer(sizer);
  m_scroll->FitInside();
  m_scroll->SetScrollRate(0, 10);

  outer->Add(m_scroll, 1, wxEXPAND);
  SetSizer(outer);

  Bind(wxEVT_SIZE, &VodInfoPanel::OnSize, this);
}

void VodInfoPanel::Clear() {
  m_playlistName.clear();
  m_streamId = 0;
  m_posterUrl.clear();
  m_posterBitmap = wxBitmap();
  m_trailerUrl.clear();

  m_poster->SetBitmap(wxBitmap());
  m_titleText->SetLabel(_U("Select a movie"));
  m_metaText->SetLabel("");
  m_genreText->SetLabel("");
  m_genreText->Hide();
  m_directorText->SetLabel("");
  m_directorText->Hide();
  m_castText->SetLabel("");
  m_castText->Hide();
  m_plotHeader->Hide();
  m_plotText->SetLabel("");
  m_plotText->Hide();
  m_playBtn->Disable();
  m_playBtn->SetLabel(wxString::FromUTF8("\xE2\x96\xB6 ") + _U("Play"));
  if (m_playFromBeginningBtn)
    m_playFromBeginningBtn->Hide();
  m_trailerBtn->Hide();
  m_resumePos = 0.0;
  m_hasResume = false;

  Layout();
  m_scroll->Layout();
  m_scroll->FitInside();
}

void VodInfoPanel::SetVod(const xtream::XtreamDatabase::VodStream &vod,
                          const std::string &infoJson,
                          const std::string &playlistName,
                          const xtream::XtreamDatabase::PlaybackPosition &playbackPos) {
  m_streamId = vod.streamId;
  m_playlistName = playlistName;
  m_posterUrl = vod.posterUrl;
  m_posterBitmap = wxBitmap();
  m_trailerUrl.clear();

  m_resumePos = 0.0;
  m_hasResume = false;
  if (playbackPos.hasPosition() && !playbackPos.finished &&
      playbackPos.position >= 10.0) {
    m_resumePos = playbackPos.position;
    m_hasResume = true;
  }

  m_titleText->SetLabel(vod.name.empty() ? _U("(Untitled)") : Str(vod.name));

  const InfoFields fields = ParseInfoJson(infoJson);

  wxString meta;
  const double rating = fields.rating > 0.0 ? fields.rating : vod.rating;
  if (rating > 0.0)
    meta += wxString::Format("\xE2\x98\x85 %.1f", rating);
  if (!fields.duration.empty()) {
    if (!meta.IsEmpty())
      meta += "  \xC2\xB7  ";
    meta += Str(fields.duration);
  }
  m_metaText->SetLabel(meta);

  ApplyInfoFields(fields);

  m_playBtn->Enable(m_streamId > 0);

  if (m_hasResume) {
    m_playBtn->SetLabel(wxString::FromUTF8("\xE2\x96\xB6 ") +
                        wxString::Format(_U("Continue from %s"),
                                         FormatTimeShort(m_resumePos)));
    if (m_playFromBeginningBtn)
      m_playFromBeginningBtn->Show();
  } else {
    m_playBtn->SetLabel(wxString::FromUTF8("\xE2\x96\xB6 ") + _U("Play"));
    if (m_playFromBeginningBtn)
      m_playFromBeginningBtn->Hide();
  }

  UpdatePoster();

  Layout();
  m_scroll->Layout();
  m_scroll->FitInside();
  RewrapTexts();
}

void VodInfoPanel::ApplyInfoFields(const InfoFields &fields) {
  if (!fields.genre.empty()) {
    m_genreText->SetLabel(_U("Genre:") + " " + Str(fields.genre));
    m_genreText->Show();
  } else {
    m_genreText->Hide();
  }

  if (!fields.director.empty()) {
    m_directorText->SetLabel(_U("Director:") + " " + Str(fields.director));
    m_directorText->Show();
  } else {
    m_directorText->Hide();
  }

  if (!fields.cast.empty()) {
    m_castText->SetLabel(_U("Cast:") + " " + Str(fields.cast));
    m_castText->Show();
  } else {
    m_castText->Hide();
  }

  if (!fields.plot.empty()) {
    m_plotText->SetLabel(Str(fields.plot));
    m_plotHeader->Show();
    m_plotText->Show();
  } else {
    m_plotHeader->Hide();
    m_plotText->Hide();
  }

  if (!fields.youtubeTrailer.empty()) {
    m_trailerUrl = fields.youtubeTrailer;
    m_trailerBtn->Show();
  } else {
    m_trailerBtn->Hide();
  }
}

void VodInfoPanel::UpdatePoster() {
  if (m_posterUrl.empty() || m_playlistName.empty() || m_streamId <= 0) {
    m_poster->SetBitmap(wxBitmap());
    return;
  }

  const wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
  const wxSize sz(FromDIP(kPosterW), FromDIP(kPosterH));
  const double cs = GetContentScale(this);
  const int physW = std::max(1, static_cast<int>(std::round(sz.x * cs)));
  const int physH = std::max(1, static_cast<int>(std::round(sz.y * cs)));
  const int scale100 = std::max(100, static_cast<int>(std::round(cs * 100.0)));

  const std::string mediaKey = "vod:" + std::to_string(m_streamId);

  auto cached = LogoCache::GetCachedPosterPtr(LogoCache::MakePosterKey(
      m_playlistName, mediaKey, m_posterUrl, physW, physH, scale100));

  if (cached && cached->IsOk()) {
    wxImage img = cached->ConvertToImage();
    if (img.IsOk()) {
      img.Rescale(sz.x, sz.y, wxIMAGE_QUALITY_HIGH);
      m_posterBitmap = wxBitmap(img);
      m_poster->SetBitmap(m_posterBitmap);
      return;
    }
    // Fallback: cached валиден, но ConvertToImage вернул невалидный образ.
    // Показываем bitmap напрямую (размер уже совпадает — он в ключе).
    m_posterBitmap = *cached;
    m_poster->SetBitmap(m_posterBitmap);
    Layout();
    return;
  }

  m_poster->SetBitmap(wxBitmap());

  LogoCache::GetPosterAsync(
      m_playlistName, mediaKey, m_posterUrl, physW, physH, bg,
      [this, sz](LogoCache::LogoBitmapPtr bmp) {
        if (this->IsBeingDeleted())
          return;
        if (!bmp || !bmp->IsOk())
          return;
        wxImage img = bmp->ConvertToImage();
        if (!img.IsOk())
          return;
        img.Rescale(sz.x, sz.y, wxIMAGE_QUALITY_HIGH);
        m_posterBitmap = wxBitmap(img);
        m_poster->SetBitmap(m_posterBitmap);
        Layout();
      },
      cs);
}

void VodInfoPanel::OnSize(wxSizeEvent &evt) {
  evt.Skip();
  RewrapTexts();
}

void VodInfoPanel::RewrapTexts() {
  if (!m_scroll)
    return;
  const int w = m_scroll->GetClientSize().GetWidth() - FromDIP(20);
  if (w < FromDIP(80))
    return;

  if (m_plotText && m_plotText->IsShown())
    m_plotText->Wrap(w);
  if (m_castText && m_castText->IsShown())
    m_castText->Wrap(w);
  if (m_directorText && m_directorText->IsShown())
    m_directorText->Wrap(w);
  if (m_genreText && m_genreText->IsShown())
    m_genreText->Wrap(w);

  m_scroll->FitInside();
}

void VodInfoPanel::OnPlay(wxCommandEvent &) {
  if (m_streamId <= 0)
    return;
  if (m_onPlay)
    m_onPlay(m_streamId, /*fromBeginning=*/false);
}

void VodInfoPanel::OnPlayFromBeginning(wxCommandEvent &) {
  if (m_streamId <= 0)
    return;
  if (m_onPlay)
    m_onPlay(m_streamId, /*fromBeginning=*/true);
}

void VodInfoPanel::OnTrailer(wxCommandEvent &) {
  if (m_trailerUrl.empty())
    return;
  const wxString url = NormalizeTrailerUrl(m_trailerUrl);
  if (!url.IsEmpty())
    wxLaunchDefaultBrowser(url);
}

VodInfoPanel::InfoFields VodInfoPanel::ParseInfoJson(const std::string &json) {
  InfoFields fields;
  if (json.empty())
    return fields;

  rapidjson::Document d;
  if (d.Parse(json.c_str()).HasParseError() || !d.IsObject())
    return fields;

  const rapidjson::Value *info = &d;
  if (d.HasMember("info") && d["info"].IsObject())
    info = &d["info"];

  fields.plot = GetStr(*info, "plot");
  if (fields.plot.empty())
    fields.plot = GetStr(*info, "description");
  fields.cast = GetStr(*info, "cast");
  if (fields.cast.empty())
    fields.cast = GetStr(*info, "actors");
  fields.director = GetStr(*info, "director");
  fields.genre = GetStr(*info, "genre");
  if (fields.genre.empty())
    fields.genre = GetStr(*info, "genres");
  fields.duration = GetStr(*info, "duration");
  if (fields.duration.empty())
    fields.duration = GetStr(*info, "episode_run_time");
  fields.youtubeTrailer = GetStr(*info, "youtube_trailer");
  if (fields.youtubeTrailer.empty())
    fields.youtubeTrailer = GetStr(*info, "trailer");
  fields.rating = GetDouble(*info, "rating");

  return fields;
}

wxString VodInfoPanel::NormalizeTrailerUrl(const std::string &idOrUrl) {
  if (idOrUrl.empty())
    return wxString();

  if (idOrUrl.find("://") != std::string::npos)
    return Str(idOrUrl);

  return "https://www.youtube.com/watch?v=" + Str(idOrUrl);
}
