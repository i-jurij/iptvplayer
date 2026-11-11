#pragma once

#include "xtream/XtreamDatabase.h"

#include <cstdint>
#include <functional>
#include <string>

#include <wx/bitmap.h>
#include <wx/panel.h>

class wxButton;
class wxScrolledWindow;
class wxStaticBitmap;
class wxStaticText;

class VodInfoPanel : public wxPanel {
public:
  explicit VodInfoPanel(wxWindow *parent);

  void SetVod(const xtream::XtreamDatabase::VodStream &vod,
              const std::string &infoJson, const std::string &playlistName,
              const xtream::XtreamDatabase::PlaybackPosition &playbackPos = {});
  void Clear();

  using PlayCallback = std::function<void(int64_t streamId,
                                          bool fromBeginning)>;
  void SetPlayCallback(PlayCallback cb) { m_onPlay = std::move(cb); }

private:
  struct InfoFields {
    std::string plot;
    std::string cast;
    std::string director;
    std::string genre;
    std::string duration;
    std::string youtubeTrailer;
    double rating = 0.0;
  };

  void BuildUi();
  void UpdatePoster();
  void ApplyInfoFields(const InfoFields &fields);
  void OnSize(wxSizeEvent &evt);
  void OnPlay(wxCommandEvent &evt);
  void OnPlayFromBeginning(wxCommandEvent &evt);
  void OnTrailer(wxCommandEvent &evt);
  void RewrapTexts();

  static InfoFields ParseInfoJson(const std::string &json);
  static wxString NormalizeTrailerUrl(const std::string &idOrUrl);

  wxScrolledWindow *m_scroll = nullptr;
  wxStaticBitmap *m_poster = nullptr;
  wxStaticText *m_titleText = nullptr;
  wxStaticText *m_metaText = nullptr;
  wxStaticText *m_genreText = nullptr;
  wxStaticText *m_directorText = nullptr;
  wxStaticText *m_castText = nullptr;
  wxStaticText *m_plotHeader = nullptr;
  wxStaticText *m_plotText = nullptr;
  wxButton *m_playBtn = nullptr;
  wxButton *m_playFromBeginningBtn = nullptr;
  wxButton *m_trailerBtn = nullptr;

  std::string m_playlistName;
  int64_t m_streamId = 0;
  double m_resumePos = 0.0;
  bool m_hasResume = false;
  std::string m_posterUrl;
  wxBitmap m_posterBitmap;
  std::string m_trailerUrl;
  PlayCallback m_onPlay;

  static constexpr int kPosterW = 220;
  static constexpr int kPosterH = 330;
};