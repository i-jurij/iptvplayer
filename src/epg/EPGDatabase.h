/**
 * EPGDatabase – SQLite wrapper for EPG data storage.
 *
 * All public methods are thread‑safe (protected by a recursive mutex).
 * Time representation: start_time/stop_time values are stored as INTEGER
 * (seconds, UTC). Transactions: InsertPrograms, SavePlaylistMapping use
 * internal transactions; external transactions are also supported.
 */

#ifndef EPG_DATABASE_H
#define EPG_DATABASE_H

#include "EPGData.h"
#include <wx/wxsqlite3.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace wxSQLite3;

/**
 * Класс для работы с SQLite БД EPG.
 * ВНИМАНИЕ: потокобезопасен (все публичные методы защищены рекурсивным
 * мьютексом).
 */
class EPGDatabase {
public:
  EPGDatabase();
  ~EPGDatabase();

  bool Open(const std::string &dbPath);
  void Close();
  bool IsOpen() const;

  bool CreateTables();

  void BeginTransaction();
  void CommitTransaction();
  void RollbackTransaction();

  bool InsertOrUpdateChannel(const EpgChannel &channel);
  bool InsertOrUpdateChannels(const std::vector<EpgChannel> &channels);

  bool InsertProgram(const std::string &channelId, const EpgProgram &program);
  bool InsertPrograms(const std::string &channelId,
                      const std::vector<EpgProgram> &programs);
  std::vector<EpgProgram> GetProgramsForChannel(const std::string &channelId,
                                                time_t startTime,
                                                time_t endTime);
  EpgProgram GetCurrentProgram(const std::string &channelId, time_t now);
  bool DeleteProgramsOlderThan(time_t threshold);
  bool ClearChannelPrograms(const std::string &channelId);

  // Ручные маппинги
  bool UpdateManualMapping(const std::string &playlistId,
                           const std::string &key,
                           const std::string &channelId);
  bool DeleteManualMapping(const std::string &playlistId,
                           const std::string &key);

  // Новые методы для общего удаления и флага ignored
  bool DeleteMappingEntry(const std::string &playlistId,
                          const std::string &key);
  bool SetIgnored(const std::string &playlistId, const std::string &key,
                  bool ignored);
  bool IsIgnored(const std::string &playlistId, const std::string &key);

  struct PlaylistMetadata {
    size_t channelCount = 0;
    std::string channelHash;
    std::string epgHashAtMatch;
    time_t lastMatchTime = 0;
  };

  bool SavePlaylistMetadata(const std::string &playlistId,
                            const PlaylistMetadata &metadata);
  bool LoadPlaylistMetadata(const std::string &playlistId,
                            PlaylistMetadata &metadata);
  bool DeletePlaylistMetadata(const std::string &playlistId);

  bool SavePlaylistMapping(
      const std::string &playlistId,
      const std::unordered_map<std::string, std::string> &mapping,
      const std::unordered_map<std::string, std::string> &manualMapping,
      size_t channelCount, const std::string &channelHash,
      const std::string &epgHashAtMatch);

  bool LoadPlaylistMapping(
      const std::string &playlistId,
      std::unordered_map<std::string, std::string> &mapping,
      std::unordered_map<std::string, std::string> &manualMapping,
      size_t &channelCount, std::string &channelHash,
      std::string &epgHashAtMatch);

  bool GetMappingEntry(const std::string &playlistId, const std::string &key,
                       std::string &channelId, bool &isManual);

  bool DeletePlaylistMapping(const std::string &playlistId);
  std::string GetEpgChannelsHash();

  std::vector<std::pair<std::string, std::string>> GetAllChannels();

  bool SaveGlobalMetadata(const std::string &key, const std::string &value);
  std::string LoadGlobalMetadata(const std::string &key);

private:
  Database m_db;
  bool m_isOpen = false;
  mutable std::recursive_mutex m_mutex;

  bool TableExists(const std::string &tableName);
};

#endif
