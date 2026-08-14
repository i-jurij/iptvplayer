/**
 * EPGManager – manages EPG data: loading, parsing, caching, and channel
 * mapping.
 *
 * Time representation: all time_t values are stored as UTC seconds since epoch
 * (std::time(nullptr)). Transactions: insertion of channels/programs and saving
 * of last_update are performed within a single transaction. Thread safety:
 * methods that modify the database are protected by mutexes. Asynchronous tasks
 * are managed via std::future and a cancellation flag.
 */

#ifndef EPGMANAGER_H
#define EPGMANAGER_H

#include "EPGData.h"
#include "EPGDatabase.h"
#include "PlaylistManager.h"
#include <functional>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <wx/event.h>
#include <wx/timer.h>

class ConfigManager;
class PlaylistManager;
class Channel;

class EPGManager : public wxEvtHandler {
public:
  EPGManager(ConfigManager *configManager, PlaylistManager *playlistManager);
  ~EPGManager();

  bool LoadFromFile(const std::string &filePath);
  void SetDbPath(const std::string &path);
  bool OpenDatabase();

  bool LoadFromUrl(const std::string &url, const std::string &userAgent = "");
  void Refresh();
  void WaitForRefresh();

  struct MatchResult {
    std::string channelId;
    std::string method;
    int score = 0;
    std::string confidence;
  };

  using MatchCallback =
      std::function<void(int matched, int total, int progress, bool success)>;
  void MatchChannels(const std::vector<Channel> &playlistChannels,
                     const std::string &playlistId,
                     MatchCallback callback = nullptr);
  void MatchChannelsAsync(const std::vector<Channel> &playlistChannels,
                          const std::string &playlistId,
                          MatchCallback callback = nullptr);

  MatchResult FindBestMatch(const Channel &playlistChannel) const;

  EpgProgram GetCurrentProgram(const std::string &tvgId) const;
  std::vector<EpgProgram> GetProgramsForChannel(const std::string &tvgId,
                                                const std::string &channelName,
                                                time_t date) const;
  std::vector<std::string> GetChannelIdsWithEpg() const;

  void SetSources(const std::vector<EpgSource> &sources);
  std::vector<EpgSource> GetSources() const;
  void SaveSourcesToConfig() const;

  // Ручные маппинги
  void SetManualMapping(const std::string &tvgId, const std::string &epgId);
  void RemoveChannelMapping(const std::string &tvgId);
  std::string GetEpgChannelIdForTvgId(const std::string &tvgId) const;

  bool LoadMappingForPlaylist(const std::string &playlistId,
                              const std::vector<Channel> &channels);
  void SaveMappingForPlaylist(const std::string &playlistId,
                              const std::vector<Channel> &channels);
  void InvalidatePlaylistMapping(const std::string &playlistId);

  void LoadRegionalSuffixes(const std::string &path);
  void SetRegionalSuffixes(const std::vector<std::string> &suffixes);

  bool IsLoaded() const { return m_loaded; }
  time_t GetLastUpdate() const { return m_lastUpdate; }

  void SetAutoUpdateEnabled(bool enabled);
  bool IsAutoUpdateEnabled() const;
  void SetUpdateIntervalHours(int hours);
  int GetUpdateIntervalHours() const;
  void SetDaysToKeep(int days);
  int GetDaysToKeep() const;

  bool DeleteCache();
  std::string getLastError() const;
  void setLastError(const std::string &msg) const;

  void
  SetOnUpdateFinished(std::function<void(int, const std::string &)> callback);
  using RefreshStartedCallback = std::function<void()>;
  void SetOnRefreshStarted(RefreshStartedCallback callback);

  void AbortDownload();
  bool HasMapping() const;
  const DownloadProgress &GetDownloadProgress() const;
  void StartAutoUpdate();
  void StopAutoUpdate();
  void RestartAutoUpdate();

  void SetCurrentPlaylistId(const std::string &playlistId) {
    m_currentPlaylistId = playlistId;
  }

  void CancelMatching() { m_cancelMatching = true; }

private:
  std::unordered_map<std::string, std::string>
      m_tvgIdIndex;                          // normalized tvgId → epgChannelId
  mutable std::shared_mutex m_tvgIndexMutex; // защита индекса

  void RebuildTvgIdIndex();
  
  wxTimer *m_startupUpdateTimer = nullptr;
  void OnStartupUpdateTimer(wxTimerEvent &event);

  std::atomic<bool> m_cancelMatching{false};

  std::future<void> m_matchFuture; // для MatchChannelsAsync

  wxTimer *m_autoUpdateTimer = nullptr;
  void OnAutoUpdateTimer(wxTimerEvent &event);

  std::atomic<bool> m_isRefreshing{false};
  bool IsRefreshing() const { return m_isRefreshing.load(); }

  DownloadProgress m_downloadProgress;
  std::function<void(int, const std::string &)> m_onUpdateFinished;
  RefreshStartedCallback m_onRefreshStarted;

  struct NormalizedChannel {
    std::string id;
    std::string normalizedName;
    std::vector<std::string> tokens;
  };

  void RebuildNormalizedCache();
  void NormalizeTvgId(std::string &id) const;
  std::string NormalizeName(const std::string &name) const;
  std::vector<std::string> Tokenize(const std::string &name) const;
  int LevenshteinDistance(const std::string &s1, const std::string &s2) const;
  int CalculateNameScore(const std::string &name1,
                         const std::string &name2) const;

  void InitializeDefaultRegionalSuffixes();
  std::vector<std::string> m_regionalSuffixes;

  std::unique_ptr<EPGDatabase> m_db;
  std::string m_dbPath;
  std::string m_epgChannelsHash;
  std::string m_currentPlaylistId;

  mutable std::shared_mutex m_mappingMutex;
  std::unordered_map<std::string, std::string> m_channelMapping;
  std::unordered_map<std::string, std::string> m_manualMapping;

  mutable std::mutex m_normalizedCacheMutex;
  std::vector<NormalizedChannel> m_normalizedCache;

  std::vector<EpgSource> m_sources;
  bool m_loaded = false;
  time_t m_lastUpdate = 0;

  bool m_autoUpdateEnabled = false;
  int m_updateIntervalHours = 24;
  int m_daysToKeep = 3;

  ConfigManager *m_configManager;
  PlaylistManager *m_playlistManager;

  mutable std::recursive_mutex m_dbMutex;
  mutable std::recursive_mutex m_mutex;
  std::future<void> m_refreshFuture;
  mutable std::mutex m_lastErrorMutex;
  mutable std::string m_lastError;

  bool ParseAndMerge(const std::string &xmlData, const std::string &sourceUrl);
  void CleanExpiredPrograms();
  void LoadSourcesFromConfig();
  std::string ComputePlaylistHash(const std::vector<Channel> &channels) const;
};

#endif
