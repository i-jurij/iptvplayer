#ifndef EPGMANAGER_H
#define EPGMANAGER_H

#include "EPGData.h"

#include <wx/event.h>

#include <future>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ConfigManager;
class PlaylistManager;
class Channel;

wxDECLARE_EVENT(EVT_EPG_UPDATED, wxCommandEvent);

class EPGManager {
public:
  EPGManager(ConfigManager *configManager, PlaylistManager *playlistManager);
  ~EPGManager();

  void SetCachePath(const std::string &path);
  bool LoadFromCache();
  bool SaveToCache() const;
  bool LoadFromUrl(const std::string &url, const std::string &userAgent = "");
  void Refresh();
  void WaitForRefresh();

  void MatchChannels(const std::vector<Channel> &playlistChannels);

  EpgProgram GetCurrentProgram(const std::string &tvgId) const;
  std::vector<EpgProgram> GetProgramsForChannel(const std::string &tvgId,
                                                time_t date) const;
  std::vector<std::string> GetChannelIdsWithEpg() const;

  void SetSources(const std::vector<EpgSource> &sources);
  std::vector<EpgSource> GetSources() const;

  bool IsLoaded() const { return m_loaded; }
  time_t GetLastUpdate() const { return m_lastUpdate; }

  void SetAutoUpdateEnabled(bool enabled) { m_autoUpdateEnabled = enabled; }
  bool IsAutoUpdateEnabled() const { return m_autoUpdateEnabled; }
  void SetUpdateIntervalHours(int hours) { m_updateIntervalHours = hours; }
  int GetUpdateIntervalHours() const { return m_updateIntervalHours; }
  void SetDaysToKeep(int days) { m_daysToKeep = days; }
  int GetDaysToKeep() const { return m_daysToKeep; }

  void SetMapping(const std::string &tvgId, const std::string &channelId);
  std::string GetEpgChannelIdForTvgId(const std::string &tvgId) const;
  void SaveSourcesToConfig() const;
  bool DeleteCache();
  std::string getLastError() const;
  void setLastError(const std::string &msg) const;
  void RemoveChannelMapping(const std::string &tvgId);

private:
  std::string BuildCacheJson() const;

  mutable std::mutex m_lastErrorMutex;
  mutable std::string m_lastError;

  ConfigManager *m_configManager;
  PlaylistManager *m_playlistManager;
  std::string m_cachePath;

  std::unordered_map<std::string, EpgChannel> m_channels;
  std::unordered_map<std::string, std::string> m_channelMapping;

  std::vector<EpgSource> m_sources;
  bool m_loaded = false;
  time_t m_lastUpdate = 0;
  bool m_autoUpdateEnabled = false;
  int m_updateIntervalHours = 24;
  int m_daysToKeep = 3;

  // Исправлено: std::mutex → std::recursive_mutex
  mutable std::recursive_mutex m_mutex;
  std::future<void> m_refreshFuture;

  bool ParseAndMerge(const std::string &xmlData, const std::string &sourceUrl);
  void CleanExpiredPrograms();
  void LoadSourcesFromConfig();
};

#endif
