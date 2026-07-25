#ifndef EPGMANAGER_H
#define EPGMANAGER_H

#include "EPGData.h"

#include <wx/event.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ConfigManager;
class PlaylistManager;
class Channel;

// Событие, генерируемое после обновления EPG
wxDECLARE_EVENT(EVT_EPG_UPDATED, wxCommandEvent);

class EPGManager {
public:
  EPGManager(ConfigManager *configManager, PlaylistManager *playlistManager);
  ~EPGManager();

  // Установка пути кэша
  void SetCachePath(const std::string &path);

  // Загрузка из кэша
  bool LoadFromCache();

  // Сохранение в кэш (JSON)
  bool SaveToCache() const;

  // Загрузка из URL (синхронно, обычно вызывается в потоке)
  bool LoadFromUrl(const std::string &url, const std::string &userAgent = "");

  // Асинхронное обновление всех источников
  void Refresh();

  // Сопоставление каналов по tvg-id
  void MatchChannels(const std::vector<Channel> &playlistChannels);

  // Получение программы для канала на текущий момент
  EpgProgram GetCurrentProgram(const std::string &tvgId) const;

  // Получение программ для канала на указанную дату (локальное время)
  std::vector<EpgProgram> GetProgramsForChannel(const std::string &tvgId,
                                                time_t date) const;

  // Получение списка всех channelId, для которых есть EPG-данные
  std::vector<std::string> GetChannelIdsWithEpg() const;

  // Управление источниками
  void SetSources(const std::vector<EpgSource> &sources);
  std::vector<EpgSource> GetSources() const;

  // Состояние
  bool IsLoaded() const { return m_loaded; }
  time_t GetLastUpdate() const { return m_lastUpdate; }

  // Настройки
  void SetAutoUpdateEnabled(bool enabled) { m_autoUpdateEnabled = enabled; }
  bool IsAutoUpdateEnabled() const { return m_autoUpdateEnabled; }
  void SetUpdateIntervalHours(int hours) { m_updateIntervalHours = hours; }
  int GetUpdateIntervalHours() const { return m_updateIntervalHours; }
  void SetDaysToKeep(int days) { m_daysToKeep = days; }
  int GetDaysToKeep() const { return m_daysToKeep; }

  // Ручное сопоставление
  void SetMapping(const std::string &tvgId, const std::string &channelId);
  std::string GetEpgChannelIdForTvgId(const std::string &tvgId) const;

  // Сохранение источников в конфиг (публичный для вызова извне)
  void SaveSourcesToConfig() const;

  bool DeleteCache();

  std::string getLastError() const { return m_lastError; }

  void RemoveChannelMapping(const std::string &tvgId);

private:
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

  mutable std::mutex m_mutex;

  bool ParseAndMerge(const std::string &xmlData, const std::string &sourceUrl);
  void CleanExpiredPrograms();
  void LoadSourcesFromConfig();
};

#endif
