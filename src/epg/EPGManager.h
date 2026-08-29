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

#include <wx/event.h>
#include <wx/timer.h>

#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ConfigManager;
class PlaylistManager;
class Channel;

// Состояния прогресса
enum class EpgProgressStage {
  None,
  Downloading,
  Error,
  Extracting,
  Parsing,
  Matching,
  Done,
  Cancelled
};

// Структура с информацией о прогрессе
struct EpgProgressInfo {
  EpgProgressStage stage = EpgProgressStage::None;
  int percent = 0;       // 0-100, -1 если не известно
  std::string stageText; // "Downloading", "Extracting" и т.д.
  double downloadedBytes = 0.0;
  double totalBytes = 0.0;
  double speedBytesPerSec = 0.0;
  int matched = 0;          // для Matching
  int totalChannels = 0;    // для Matching
  std::string errorMessage; // для Error (если понадобится)
};

class EPGManager : public wxEvtHandler {
public:
  EPGManager(ConfigManager *configManager, PlaylistManager *playlistManager);
  ~EPGManager();
  
  wxString GetConfigDirectory() const;

  bool LoadFromFile(const std::string &filePath);
  void SetDbPath(const std::string &path);
  bool OpenDatabase();

  bool LoadFromUrl(const std::string &url, const std::string &userAgent = "");

  struct MatchResult {
    std::string channelId;
    std::string method; // "alias", "tvg-id", "exact_name", "substring",
                        // "tokens", "jaro"
    int score = 0;
    std::string confidence; // "high", "medium", "low", "none"
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
  void SetManualMapping(const std::string &playlistId, const std::string &tvgId,
                        const std::string &epgId,
                        const std::string &channelName = "");
  void RemoveChannelMapping(const std::string &tvgId);
  std::string GetEpgChannelIdForTvgId(const std::string &tvgId) const;

  bool LoadMappingForPlaylist(const std::string &playlistId,
                              const std::vector<Channel> &channels);
  void SaveMappingForPlaylist(const std::string &playlistId,
                              const std::vector<Channel> &channels);
  void InvalidatePlaylistMapping(const std::string &playlistId);

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

  // ---- Getters for match thresholds (для UI) ----
  int GetFuzzyThreshold() const { return m_fuzzyThreshold; }
  int GetSubstringMinLength() const { return m_substringMinLength; }
  int GetSubstringMinRatio() const { return m_substringMinRatio; }
  int GetMinMatchScore() const { return m_minMatchScore; }

  // ---- Setters for match thresholds (сохраняют в ConfigManager) ----
  void SetFuzzyThreshold(int value);
  void SetSubstringMinLength(int value);
  void SetSubstringMinRatio(int value);
  void SetMinMatchScore(int value);

  void ReMatchCurrentPlaylist();

  // ---- Для ручного маппинга (будет использовано позже) ----
  std::vector<std::pair<std::string, std::string>> GetAllEpgChannels() const;

  void RemoveManualMapping(const std::string &playlistId,
                           const std::string &tvgId);
  std::string GetEpgName(const std::string &epgId) const;
  bool GetMappingEntry(const std::string &playlistId, const std::string &key,
                       std::string &channelId, bool &isManual);
  std::unordered_map<std::string, MappingEntry>
  GetAllMappingsForPlaylist(const std::string &playlistId);
  
  // Колбэк прогресса
  using ProgressCallback = std::function<void(const EpgProgressInfo &)>;
  void SetOnProgress(ProgressCallback callback);
  // Методы для управления прогрессом (будут использоваться внутри)
  void UpdateProgress(EpgProgressStage stage, int percent = -1,
                      const std::string &stageText = "", double downloaded = -1,
                      double total = -1, double speed = -1, int matched = -1,
                      int totalChannels = -1);
  void RefreshSourceAsync(
      const std::string &url, const std::string &name,
      std::function<void(bool, const std::string &)> callback = nullptr);

  void AddOnProgress(ProgressCallback callback);

  void RemoveMappingEntry(const std::string &playlistId,
                          const std::string &key);
  void IgnoreAutoMapping(const std::string &playlistId, const std::string &key);
  void UnignoreAutoMapping(const std::string &playlistId,
                           const std::string &key);
  bool IsIgnored(const std::string &playlistId, const std::string &key) const;

  std::string NormalizeName(const std::string &name) const;

  void UpdateAllSources(bool onlyAutoUpdate);

private:
  void EnsureConfigFile(const wxString &filename);

  std::atomic<bool> m_autoUpdateInProgress{false};

  // Количество потоков для параллельного матчинга
  int m_matchThreads = 0;

  // Вспомогательный метод для параллельной обработки части каналов
  std::unordered_map<std::string, std::string>
  ProcessChannelBatch(const std::vector<Channel> &batch) const;

  std::future<void> m_singleRefreshFuture;

  // Прогресс
  std::vector<ProgressCallback> m_progressCallbacks;
  wxTimer *m_progressTimer;
  DownloadProgress m_downloadProgress;
  std::chrono::steady_clock::time_point m_lastProgressTime;
  double m_lastDownloadedBytes = 0.0;

  void OnProgressTimer(wxTimerEvent &event);
  void StartProgressTimer();
  void StopProgressTimer();

  mutable std::unordered_map<std::string, std::string> m_epgNameCache;
  void UpdateEpgNameCache();

  // Пороги матчинга
  int m_fuzzyThreshold = 85;
  int m_substringMinLength = 6;
  int m_substringMinRatio = 30;
  int m_minMatchScore = 50;

  void LoadMatchSettings(); // загружает пороги из ConfigManager

  // Вспомогательная структура для хранения кандидата
  struct Candidate {
    std::string epgId;
    int score = 0;
    std::string method;
  };

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

  std::function<void(int, const std::string &)> m_onUpdateFinished;
  RefreshStartedCallback m_onRefreshStarted;

  mutable std::unique_ptr<EPGDatabase> m_db;
  std::string m_dbPath;
  std::string m_epgChannelsHash;
  std::string m_currentPlaylistId;

  mutable std::shared_mutex m_mappingMutex;
  std::unordered_map<std::string, std::string> m_channelMapping;
  std::unordered_map<std::string, std::string> m_manualMapping;

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
  mutable std::mutex m_lastErrorMutex;
  mutable std::string m_lastError;

  bool ParseAndMerge(const std::string &xmlData, const std::string &sourceUrl);
  void CleanExpiredPrograms();
  void LoadSourcesFromConfig();
  std::string ComputePlaylistHash(const std::vector<Channel> &channels) const;

  struct NormalizedChannel {
    std::string id; // EPG channel id
    std::string displayName;   // оригинальное display‑name (для индекса алиасов)
    std::string baseName; // очищенное имя без суффиксов и стоп-слов
    std::string region;   // ru, us и т.п. (из региональных суффиксов)
    std::string quality;  // hd, 1080p и т.п.
    std::string version;  // plus, premium и т.п.
    std::vector<std::string> tokens; // слова из baseName (уже без стоп-слов)
  };

  void RebuildNormalizedCache();
  void NormalizeTvgId(std::string &id) const;
  std::vector<std::string> Tokenize(const std::string &name) const;

  void InitializeDefaultRegionalSuffixes();
  std::vector<std::string> m_regionalSuffixes;

  std::unordered_map<std::string, std::string> m_aliasIndex; // очищенное имя EPG → epgId
  void RebuildAliasIndex();

  mutable std::mutex m_normalizedCacheMutex;
  std::vector<NormalizedChannel> m_normalizedCache;

  void LoadMatchingRules();
  void LoadChannelAliases();
  void NormalizeWithAttributes(const std::string &rawName,
                               NormalizedChannel &out) const;
  double ComputeJaroWinkler(const std::string &s1, const std::string &s2) const;

  // Правила (загружаются из файлов)
  std::vector<std::string> m_qualitySuffixes;
  std::vector<std::string> m_versionSuffixes;
  std::vector<std::string> m_stopwords;
  std::unordered_map<std::string, std::string> m_channelAliases;

  // Пороги
  double m_tokenHigh = 0.7;
  double m_tokenLow = 0.5;
  double m_jaroHigh = 0.9;
  double m_jaroMedium = 0.85;
  double m_jaroLow = 0.8;
  int m_substringMinLen = 4;

  // Вспомогательные методы нормализации
  static std::string ToLower(const std::string &str);
  static std::string RemoveRatingSuffixes(const std::string &str);
  std::string RemoveStopwords(const std::string &str) const;
  std::string ExtractSuffix(const std::string &str,
                            const std::vector<std::string> &suffixes,
                            std::string &outSuffix) const;
  std::string CleanPunctuation(const std::string &str) const;

  std::string NormalizeAliasKey(const std::string &name) const;
  MatchResult MatchByAlias(const std::string &playlistName) const;
};

#endif
