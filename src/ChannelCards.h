#pragma once

#include "CardsBase.h"
#include "TypeAheadSearch.h"

#include <memory>

class ChannelCards : public CardsBase {
public:
  ChannelCards(wxWindow *parent);

  void SetChannels(const std::vector<Channel> &channels,
                   const std::string &playlistName);

  bool RemoveChannel(const std::string &name, const std::string &playlistName);

private:
  std::string m_playlistName;

  wxBitmap m_favFilled;
  wxBitmap m_favOutline;

  wxBitmap GetStarBitmap(const Channel &ch) const override;
  void OnCardClick(size_t index, bool fav, const wxRect &rect) override;
  void OnDoubleClick(wxMouseEvent &evt);

  std::unique_ptr<TypeAheadSearch> m_search;
};
