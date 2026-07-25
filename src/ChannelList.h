#pragma once

#include "BaseChannelList.h"
#include "Channel.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ChannelList : public BaseChannelList {
public:
  ChannelList(wxWindow *parent, wxWindowID id);
  ~ChannelList();

  void loadChannelsAsync(const std::vector<Channel> &channels,
                         const std::string &playlistName);

  using SelectCallback =
      std::function<void(const Channel &, size_t, const wxRect &)>;
  void SetSelectCallback(SelectCallback cb) { m_onSelect = std::move(cb); }

protected:
  SelectCallback m_onSelect;
  void OnChannelActivated(const Channel &ch, int col) override;
  void OnFavoriteToggled(const Channel &ch, bool isFav) override;
  void OnKeyDown(wxKeyEvent &evt);

private:
  int m_rightClickRow = -1;
  void OnContextMenu(wxDataViewEvent &evt);
  void ShowContextMenu(const Channel &ch);

  std::atomic<size_t> m_prefetchRemaining{500};

  // Background prefetch worker
  void StartBackgroundPrefetch();
  void StopBackgroundPrefetch();
  void BackgroundPrefetchLoop();

  std::thread m_bgWorker;
  std::atomic<bool> m_bgRunning{false};
  std::mutex m_bgMutex;
  std::condition_variable m_bgCv;
  size_t m_bgNextIndex{0};
};
