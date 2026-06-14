#pragma once

#include "BaseChannelList.h"
#include <functional>

class FavoritesList : public BaseChannelList {
public:
  FavoritesList(wxWindow *parent, wxWindowID id);

  void loadChannels(const std::vector<Channel> &channels);
  size_t GetChannelCount() const { return GetModel()->GetCount(); }
  using SelectCallback =
      std::function<void(const Channel &, size_t, const wxRect &)>;
  void SetSelectCallback(SelectCallback cb) { m_onSelect = std::move(cb); }

protected:
  SelectCallback m_onSelect;
  void OnChannelActivated(const Channel &ch, int col) override;
  void OnFavoriteToggled(const Channel &ch, bool isFav) override;
  void OnKeyDown(wxKeyEvent &evt);
};
