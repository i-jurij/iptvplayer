#pragma once

#include "Channel.h"
#include "LogoCache.h"

#include <wx/dataview.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class ChannelDataModel : public wxDataViewVirtualListModel {
public:
  ChannelDataModel();

  void SetFavorites(
      const std::vector<std::pair<std::string, std::string>> &favs);
  void SetRowKey(unsigned int row, const std::string &key);

  uint64_t GetModelVersion() const {
    return m_channelsVersion.load(std::memory_order_relaxed);
  }

  unsigned int GetColumnCount() const override;
  wxString GetColumnType(unsigned int col) const override;

  void GetValueByRow(wxVariant &variant, unsigned int row,
                     unsigned int col) const override;

  bool SetValueByRow(const wxVariant &variant, unsigned int row,
                     unsigned int col) override;

  void ChangeValue(const wxVariant &variant, unsigned int row,
                   unsigned int col);

  void SetChannels(const std::vector<Channel> &channels,
                   const std::string &playlistName, int logoSize, int dpi);

  void AppendChannels(const std::vector<Channel> &channels,
                      const std::string &playlistName, size_t preloadCount,
                      int dpi);

  const Channel &GetChannel(unsigned int row) const;
  bool IsFavorite(unsigned int row) const;

  void SetFavoritesFromNames(const std::vector<std::string> &names);

  void SetSorting(int column, bool ascending);
  int GetSortColumn() const { return m_sortColumn; }
  bool IsSortAscending() const { return m_sortAscending; }

  void DisableSorting() { m_disableSorting = true; }
  void EnableSorting() { m_disableSorting = false; }

  wxDataViewItem GetItem(unsigned int row) const {
    return wxDataViewItem(reinterpret_cast<void *>(row + 1));
  }

  unsigned int GetRow(const wxDataViewItem &item) const override {
    return reinterpret_cast<uintptr_t>(item.GetID()) - 1;
  }

  std::string MakeCacheKey(const std::string &playlist,
                           const std::string &channelOrUrl, int size,
                           int dpi) const;
  void CheckDpiReset();

  void UpdateRowByName(const std::string &playlist, const std::string &name);

  void SafeUpdateRowByName(const std::string &playlist, const std::string &name,
                           const LogoCache::LogoBitmapPtr &bmp = nullptr,
                           uint64_t expectedModelVer = 0);
  void UpdateRowByIndex(unsigned int row);

  // --- New hybrid API: request load if missing ---
  enum class RequestLogoStatus { AlreadyCached, Queued, Skipped };
  // RequestLogoLoadIfMissing checks cache and, if missing, invokes enqueue
  // callback.
  RequestLogoStatus RequestLogoLoadIfMissing(unsigned int row,
                                             bool highPriority);

  // Set callback that model will call to enqueue a row for loading.
  // Signature: (row, highPriority)
  void SetEnqueueCallback(std::function<void(unsigned int, bool)> cb);

  void RemoveChannel(const std::string &name, const std::string &url);

private:
  mutable std::unordered_map<unsigned int, std::string> m_rowKeyCache;

  std::vector<Channel> m_channels;
  std::vector<bool> m_favorites;

  std::string m_playlistName;

  int m_sortColumn;
  bool m_sortAscending;
  bool m_disableSorting;

  void Resort() override;

  int m_lastDpi = 0;

  int m_logoSize;

  // model version to invalidate in-flight callbacks
  std::atomic<uint64_t> m_channelsVersion{0};

  // enqueue callback (set by view)
  std::function<void(unsigned int, bool)> m_enqueueCallback;
};
