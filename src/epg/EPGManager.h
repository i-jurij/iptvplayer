#ifndef EPGMANAGER_H
#define EPGMANAGER_H

#include "EPGData.h"

#include <wx/event.h>

#include <future>
#include <mutex>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>


class ConfigManager;
class PlaylistManager;
class Channel;

class EPGManager {
public:
  EPGManager(ConfigManager *configManager, PlaylistManager *playlistManager);
  ~EPGManager();

  bool LoadFromFile(const std::string &filePath);
  
  void SetCachePath(const std::string &path);
  bool LoadFromCache();
  bool SaveToCache() const;
  bool LoadFromUrl(const std::string &url, const std::string &userAgent = "");
  void Refresh();
  void WaitForRefresh();

  // ---------- Сопоставление каналов ----------
  struct MatchResult {
    std::string channelId; // id из EPG
    std::string method;    // "manual", "exact_name", "token_sort", "fuzzy",
                           // "substring", "exact_tvgid_fallback"
    int score = 0;
    std::string confidence; // "high", "medium", "low"
  };

  void MatchChannels(const std::vector<Channel> &playlistChannels);
  MatchResult FindBestMatch(const Channel &playlistChannel) const;

  // ---------- Получение программ ----------
  EpgProgram GetCurrentProgram(const std::string &tvgId) const;
  std::vector<EpgProgram> GetProgramsForChannel(const std::string &tvgId,
                                                const std::string &channelName,
                                                time_t date) const;
  std::vector<std::string> GetChannelIdsWithEpg() const;

  // ---------- Управление источниками ----------
  void SetSources(const std::vector<EpgSource> &sources);
  std::vector<EpgSource> GetSources() const;
  void SaveSourcesToConfig() const;

  // ---------- Ручное сопоставление ----------
  void SetManualMapping(const std::string &tvgId, const std::string &epgId);
  void SetMapping(const std::string &tvgId,
                  const std::string &channelId); // обратная совместимость
  void RemoveChannelMapping(const std::string &tvgId);
  std::string GetEpgChannelIdForTvgId(const std::string &tvgId) const;

  // ---------- Региональные суффиксы ----------
  void LoadRegionalSuffixes(const std::string &path);
  void SetRegionalSuffixes(const std::vector<std::string> &suffixes);

  // ---------- Статус и настройки ----------
  bool IsLoaded() const { return m_loaded; }
  time_t GetLastUpdate() const { return m_lastUpdate; }

  void SetAutoUpdateEnabled(bool enabled) { m_autoUpdateEnabled = enabled; }
  bool IsAutoUpdateEnabled() const { return m_autoUpdateEnabled; }
  void SetUpdateIntervalHours(int hours) { m_updateIntervalHours = hours; }
  int GetUpdateIntervalHours() const { return m_updateIntervalHours; }
  void SetDaysToKeep(int days) { m_daysToKeep = days; }
  int GetDaysToKeep() const { return m_daysToKeep; }

  bool DeleteCache();
  std::string getLastError() const;
  void setLastError(const std::string &msg) const;

  void
  SetOnUpdateFinished(std::function<void(int, const std::string &)> callback);

private:
  std::function<void(int, const std::string &)> m_onUpdateFinished;

  // ---------- Внутренние структуры ----------
  struct NormalizedChannel {
    std::string id;
    std::string normalizedName;
    std::vector<std::string> tokens;
  };

  // ---------- Методы нормализации ----------
  void RebuildNormalizedCache();
  void NormalizeTvgId(std::string &id) const;
  std::string NormalizeName(const std::string &name) const;
  std::vector<std::string> Tokenize(const std::string &name) const;
  int LevenshteinDistance(const std::string &s1, const std::string &s2) const;
  int CalculateNameScore(const std::string &name1,
                         const std::string &name2) const;

  // ---------- Суффиксы ----------
  void InitializeDefaultRegionalSuffixes();
  std::vector<std::string> m_regionalSuffixes;

  // ---------- Кэши и маппинги ----------
  std::unordered_map<std::string, EpgChannel> m_channels;
  std::unordered_map<std::string, std::string>
      m_channelMapping; // "tvgId" или "name:..." → EPG channel id
  std::unordered_map<std::string, std::string>
      m_manualMapping; // tvgId → EPG channel id (ручное)
  std::vector<NormalizedChannel> m_normalizedCache;

  std::vector<EpgSource> m_sources;
  bool m_loaded = false;
  time_t m_lastUpdate = 0;

  // ---------- Настройки ----------
  bool m_autoUpdateEnabled = false;
  int m_updateIntervalHours = 24;
  int m_daysToKeep = 3;

  // ---------- Вспомогательное ----------
  ConfigManager *m_configManager;
  PlaylistManager *m_playlistManager;
  std::string m_cachePath;

  mutable std::recursive_mutex m_mutex;
  std::future<void> m_refreshFuture;
  mutable std::mutex m_lastErrorMutex;
  mutable std::string m_lastError;

  // ---------- Приватные методы ----------
  bool ParseAndMerge(const std::string &xmlData, const std::string &sourceUrl);
  void CleanExpiredPrograms();
  void LoadSourcesFromConfig();
  std::string BuildCacheJson() const;
};

#endif