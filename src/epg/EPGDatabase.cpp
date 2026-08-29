#include "EPGDatabase.h"
#include "HashUtils.h"
#include "LogControl.h"
#include <wx/wxsqlite3.h>

#include <ctime>

using namespace wxSQLite3;

static std::string wx_to_std(const wxString &s) {
  return std::string(s.utf8_str());
}

// ------------------------------------------------------------------
// RAII‑обёртка для автоматической финализации Statement
// ------------------------------------------------------------------
class StatementGuard {
  Statement &stmt_;

public:
  explicit StatementGuard(Statement &stmt) : stmt_(stmt) {}
  ~StatementGuard() {
    try {
      stmt_.Finalize();
    } catch (...) {
    }
  }
  StatementGuard(const StatementGuard &) = delete;
  StatementGuard &operator=(const StatementGuard &) = delete;
};

// ------------------------------------------------------------------
// ScopedTransaction (без изменений)
// ------------------------------------------------------------------
class ScopedTransaction {
  EPGDatabase &m_db;
  bool m_committed = false;

public:
  explicit ScopedTransaction(EPGDatabase &db) : m_db(db) {
    m_db.BeginTransaction();
  }
  void Commit() {
    if (!m_committed) {
      m_db.CommitTransaction();
      m_committed = true;
    }
  }
  ~ScopedTransaction() {
    if (!m_committed)
      try {
        m_db.RollbackTransaction();
      } catch (...) {
      }
  }
  ScopedTransaction(const ScopedTransaction &) = delete;
  ScopedTransaction &operator=(const ScopedTransaction &) = delete;
};

// ------------------------------------------------------------------
// Конструктор / Деструктор
// ------------------------------------------------------------------
EPGDatabase::EPGDatabase() = default;
EPGDatabase::~EPGDatabase() { Close(); }

// ------------------------------------------------------------------
// Открытие / Закрытие
// ------------------------------------------------------------------
bool EPGDatabase::Open(const std::string &dbPath) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_isOpen)
    Close();

  try {
    m_db.Open(dbPath);
    m_isOpen = true;

    m_db.ExecuteUpdate("PRAGMA journal_mode = WAL");
    m_db.ExecuteUpdate("PRAGMA synchronous = NORMAL");
    m_db.ExecuteUpdate("PRAGMA foreign_keys = ON");
    m_db.ExecuteUpdate("PRAGMA busy_timeout = 5000");

    if (!TableExists("channels")) {
      if (!CreateTables()) {
        LOG_ERROR("EPGDatabase: failed to create tables");
        Close();
        return false;
      }
    }

    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::Open exception: %s",
              e.GetMessage().ToUTF8().data());
    Close();
    return false;
  } catch (std::exception &e) {
    LOG_ERROR("EPGDatabase::Open std::exception: %s", e.what());
    Close();
    return false;
  } catch (...) {
    LOG_ERROR("EPGDatabase::Open unknown exception");
    Close();
    return false;
  }
}

void EPGDatabase::Close() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_isOpen) {
    try {
      m_db.Close();
    } catch (...) {
    }
    m_isOpen = false;
  }
}

bool EPGDatabase::IsOpen() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  return m_isOpen;
}

// ------------------------------------------------------------------
// Создание таблиц и проверка существования
// ------------------------------------------------------------------
bool EPGDatabase::CreateTables() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  try {
    m_db.ExecuteUpdate("CREATE TABLE IF NOT EXISTS channels ("
                       "id TEXT PRIMARY KEY, display_name TEXT)");
    m_db.ExecuteUpdate("CREATE TABLE IF NOT EXISTS programs ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "channel_id TEXT NOT NULL,"
                       "start_time INTEGER NOT NULL,"
                       "stop_time INTEGER NOT NULL,"
                       "title TEXT, description TEXT, category TEXT)");
    m_db.ExecuteUpdate(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_programs_channel_start "
        "ON programs(channel_id, start_time)");
    m_db.ExecuteUpdate("CREATE INDEX IF NOT EXISTS idx_programs_stop_time ON "
                       "programs(stop_time)");
    m_db.ExecuteUpdate(
        "CREATE INDEX IF NOT EXISTS idx_programs_channel_start_stop "
        "ON programs(channel_id, start_time, stop_time)");
    m_db.ExecuteUpdate("CREATE TABLE IF NOT EXISTS playlist_metadata ("
                       "playlist_id TEXT PRIMARY KEY,"
                       "channel_count INTEGER, channel_hash TEXT,"
                       "epg_hash_at_match TEXT, last_match_time INTEGER)");
    m_db.ExecuteUpdate("CREATE TABLE IF NOT EXISTS playlist_mappings ("
                       "playlist_id TEXT, key TEXT, channel_id TEXT,"
                       "is_manual INTEGER DEFAULT 0, confidence TEXT,"
                       "ignored INTEGER DEFAULT 0,"
                       "PRIMARY KEY (playlist_id, key))");
    m_db.ExecuteUpdate("CREATE INDEX IF NOT EXISTS idx_mappings_playlist ON "
                       "playlist_mappings(playlist_id)");
    m_db.ExecuteUpdate("CREATE TABLE IF NOT EXISTS global_metadata (key TEXT "
                       "PRIMARY KEY, value TEXT)");
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::CreateTables exception: %s",
              e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::TableExists(const std::string &tableName) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  try {
    Statement stmt = m_db.PrepareStatement(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
    stmt.Bind(1, wxString::FromUTF8(tableName));
    ResultSet rs = stmt.ExecuteQuery();
    bool exists = rs.NextRow();
    rs.Finalize();
    return exists;
  } catch (...) {
    return false;
  }
}

// ------------------------------------------------------------------
// Транзакции
// ------------------------------------------------------------------
void EPGDatabase::BeginTransaction() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_isOpen)
    try {
      m_db.ExecuteUpdate("BEGIN TRANSACTION");
    } catch (...) {
    }
}

void EPGDatabase::CommitTransaction() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_isOpen)
    try {
      m_db.ExecuteUpdate("COMMIT");
    } catch (...) {
    }
}

void EPGDatabase::RollbackTransaction() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_isOpen)
    try {
      m_db.ExecuteUpdate("ROLLBACK");
    } catch (...) {
    }
}

// ------------------------------------------------------------------
// Вставка каналов и программ (исправлено – локальные Statement)
// ------------------------------------------------------------------
bool EPGDatabase::InsertOrUpdateChannel(const EpgChannel &channel) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt = m_db.PrepareStatement(
        "INSERT OR REPLACE INTO channels (id, display_name) VALUES (?, ?)");
    StatementGuard guard(stmt);

    stmt.Bind(1, wxString::FromUTF8(channel.id));
    stmt.Bind(2, wxString::FromUTF8(channel.displayName));
    stmt.ExecuteUpdate();
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::InsertOrUpdateChannel exception for '%s': %s",
              channel.id.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  } catch (std::exception &e) {
    LOG_ERROR("EPGDatabase::InsertOrUpdateChannel std::exception: %s",
              e.what());
    return false;
  } catch (...) {
    LOG_ERROR("EPGDatabase::InsertOrUpdateChannel unknown exception");
    return false;
  }
}

bool EPGDatabase::InsertOrUpdateChannels(
    const std::vector<EpgChannel> &channels) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen || channels.empty())
    return true;

  try {
    ScopedTransaction tx(*this);
    size_t idx = 0;
    for (const auto &ch : channels) {
      if (!InsertOrUpdateChannel(ch)) {
        LOG_ERROR(
            "InsertOrUpdateChannels: failed at index %zu for channel '%s'", idx,
            ch.id.c_str());
        return false;
      }
      ++idx;
    }
    tx.Commit();
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("InsertOrUpdateChannels exception: %s",
              e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::InsertProgram(const std::string &channelId,
                                const EpgProgram &program) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt = m_db.PrepareStatement(
        "INSERT OR IGNORE INTO programs "
        "(channel_id, start_time, stop_time, title, description, category) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    StatementGuard guard(stmt);

    stmt.Bind(1, wxString::FromUTF8(channelId));
    stmt.Bind(2, wxLongLong(program.startTime));
    stmt.Bind(3, wxLongLong(program.stopTime));
    stmt.Bind(4, wxString::FromUTF8(program.title));
    stmt.Bind(5, wxString::FromUTF8(program.description));
    stmt.Bind(6, wxString::FromUTF8(program.category));
    stmt.ExecuteUpdate();
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::InsertProgram exception for channel '%s': %s",
              channelId.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  } catch (std::exception &e) {
    LOG_ERROR("EPGDatabase::InsertProgram std::exception: %s", e.what());
    return false;
  } catch (...) {
    LOG_ERROR("EPGDatabase::InsertProgram unknown exception");
    return false;
  }
}

bool EPGDatabase::InsertPrograms(const std::string &channelId,
                                 const std::vector<EpgProgram> &programs) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen || programs.empty())
    return true;

  try {
    ScopedTransaction tx(*this);
    size_t idx = 0;
    for (const auto &prog : programs) {
      if (!InsertProgram(channelId, prog)) {
        LOG_ERROR(
            "InsertPrograms: failed at index %zu for channel '%s' (time %lld)",
            idx, channelId.c_str(), static_cast<long long>(prog.startTime));
        return false;
      }
      ++idx;
    }
    tx.Commit();
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("InsertPrograms exception for channel '%s': %s",
              channelId.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  }
}

// ------------------------------------------------------------------
// Получение программ
// ------------------------------------------------------------------
std::vector<EpgProgram>
EPGDatabase::GetProgramsForChannel(const std::string &channelId,
                                   time_t startTime, time_t endTime) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  std::vector<EpgProgram> result;
  if (!m_isOpen)
    return result;

  try {
    Statement stmt = m_db.PrepareStatement(
        "SELECT start_time, stop_time, title, description, category, "
        "channel_id "
        "FROM programs "
        "WHERE channel_id = ? AND start_time < ? AND stop_time > ? "
        "ORDER BY start_time");
    StatementGuard guard(stmt);

    stmt.Bind(1, wxString::FromUTF8(channelId));
    stmt.Bind(2, wxLongLong(endTime));
    stmt.Bind(3, wxLongLong(startTime));
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      EpgProgram prog;
      prog.startTime = static_cast<time_t>(rs.GetInt64(0).GetValue());
      prog.stopTime = static_cast<time_t>(rs.GetInt64(1).GetValue());
      prog.title = wx_to_std(rs.GetString(2));
      prog.description = wx_to_std(rs.GetString(3));
      prog.category = wx_to_std(rs.GetString(4));
      prog.channelId = wx_to_std(rs.GetString(5));
      result.push_back(prog);
    }
    return result;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::GetProgramsForChannel exception for '%s': %s",
              channelId.c_str(), e.GetMessage().ToUTF8().data());
    return result;
  }
}

EpgProgram EPGDatabase::GetCurrentProgram(const std::string &channelId,
                                          time_t now) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  EpgProgram result;
  if (!m_isOpen)
    return result;

  try {
    Statement stmt = m_db.PrepareStatement(
        "SELECT start_time, stop_time, title, description, category, "
        "channel_id "
        "FROM programs WHERE channel_id = ? AND start_time <= ? AND stop_time "
        "> ? "
        "ORDER BY start_time DESC LIMIT 1");
    StatementGuard guard(stmt);

    stmt.Bind(1, wxString::FromUTF8(channelId));
    stmt.Bind(2, wxLongLong(now));
    stmt.Bind(3, wxLongLong(now));
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow()) {
      result.startTime = static_cast<time_t>(rs.GetInt64(0).GetValue());
      result.stopTime = static_cast<time_t>(rs.GetInt64(1).GetValue());
      result.title = wx_to_std(rs.GetString(2));
      result.description = wx_to_std(rs.GetString(3));
      result.category = wx_to_std(rs.GetString(4));
      result.channelId = wx_to_std(rs.GetString(5));
    }
    return result;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::GetCurrentProgram exception for '%s': %s",
              channelId.c_str(), e.GetMessage().ToUTF8().data());
    return result;
  }
}

bool EPGDatabase::DeleteProgramsOlderThan(time_t threshold) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt =
        m_db.PrepareStatement("DELETE FROM programs WHERE stop_time < ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxLongLong(threshold));
    stmt.ExecuteUpdate();
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::DeleteProgramsOlderThan exception: %s",
              e.GetMessage().ToUTF8().data());
    return false;
  } catch (std::exception &e) {
    LOG_ERROR("EPGDatabase::DeleteProgramsOlderThan std::exception: %s",
              e.what());
    return false;
  } catch (...) {
    LOG_ERROR("EPGDatabase::DeleteProgramsOlderThan unknown exception");
    return false;
  }
}

bool EPGDatabase::ClearChannelPrograms(const std::string &channelId) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt =
        m_db.PrepareStatement("DELETE FROM programs WHERE channel_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(channelId));
    stmt.ExecuteUpdate();
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::ClearChannelPrograms exception for '%s': %s",
              channelId.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  } catch (std::exception &e) {
    LOG_ERROR("EPGDatabase::ClearChannelPrograms std::exception: %s", e.what());
    return false;
  } catch (...) {
    LOG_ERROR("EPGDatabase::ClearChannelPrograms unknown exception");
    return false;
  }
}

// ------------------------------------------------------------------
// GetAllChannels
// ------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> EPGDatabase::GetAllChannels() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  std::vector<std::pair<std::string, std::string>> out;
  if (!m_isOpen)
    return out;

  try {
    Statement stmt =
        m_db.PrepareStatement("SELECT id, display_name FROM channels");
    StatementGuard guard(stmt);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      std::string id = wx_to_std(rs.GetString(0));
      std::string name = wx_to_std(rs.GetString(1));
      out.emplace_back(std::move(id), std::move(name));
    }
    return out;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::GetAllChannels exception: %s",
              e.GetMessage().ToUTF8().data());
    return out;
  }
}

// ------------------------------------------------------------------
// Ручные маппинги
// ------------------------------------------------------------------
bool EPGDatabase::UpdateManualMapping(const std::string &playlistId,
                                      const std::string &key,
                                      const std::string &channelId) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt = m_db.PrepareStatement(
        "INSERT OR REPLACE INTO playlist_mappings "
        "(playlist_id, key, channel_id, is_manual, confidence) "
        "VALUES (?, ?, ?, ?, ?)");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    stmt.Bind(2, wxString::FromUTF8(key));
    stmt.Bind(3, wxString::FromUTF8(channelId));
    stmt.Bind(4, 1);
    stmt.Bind(5, "");
    stmt.ExecuteUpdate();
    LOG_DEBUG("EPGDatabase: Updated manual mapping for playlist '%s' key '%s' "
              "-> '%s'",
              playlistId.c_str(), key.c_str(), channelId.c_str());
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR(
        "EPGDatabase::UpdateManualMapping exception for '%s' key '%s': %s",
        playlistId.c_str(), key.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::DeleteManualMapping(const std::string &playlistId,
                                      const std::string &key) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt =
        m_db.PrepareStatement("DELETE FROM playlist_mappings WHERE playlist_id "
                              "= ? AND key = ? AND is_manual = 1");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    stmt.Bind(2, wxString::FromUTF8(key));
    int rows = stmt.ExecuteUpdate();
    if (rows > 0) {
      LOG_DEBUG(
          "EPGDatabase: Deleted manual mapping for playlist '%s' key '%s'",
          playlistId.c_str(), key.c_str());
    }
    return rows > 0;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR(
        "EPGDatabase::DeleteManualMapping exception for '%s' key '%s': %s",
        playlistId.c_str(), key.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  }
}

// ------------------------------------------------------------------
// Метаданные плейлиста
// ------------------------------------------------------------------
bool EPGDatabase::SavePlaylistMetadata(const std::string &playlistId,
                                       const PlaylistMetadata &metadata) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt =
        m_db.PrepareStatement("INSERT OR REPLACE INTO playlist_metadata "
                              "(playlist_id, channel_count, channel_hash, "
                              "epg_hash_at_match, last_match_time) "
                              "VALUES (?, ?, ?, ?, ?)");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    stmt.Bind(2, wxLongLong(metadata.channelCount));
    stmt.Bind(3, wxString::FromUTF8(metadata.channelHash));
    stmt.Bind(4, wxString::FromUTF8(metadata.epgHashAtMatch));
    stmt.Bind(5, wxLongLong(metadata.lastMatchTime));
    stmt.ExecuteUpdate();
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::SavePlaylistMetadata exception for '%s': %s",
              playlistId.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::LoadPlaylistMetadata(const std::string &playlistId,
                                       PlaylistMetadata &metadata) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt =
        m_db.PrepareStatement("SELECT channel_count, channel_hash, "
                              "epg_hash_at_match, last_match_time "
                              "FROM playlist_metadata WHERE playlist_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow()) {
      metadata.channelCount = static_cast<size_t>(rs.GetInt64(0).GetValue());
      metadata.channelHash = wx_to_std(rs.GetString(1));
      metadata.epgHashAtMatch = wx_to_std(rs.GetString(2));
      metadata.lastMatchTime = static_cast<time_t>(rs.GetInt64(3).GetValue());
      return true;
    }
    return false;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::LoadPlaylistMetadata exception for '%s': %s",
              playlistId.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::DeletePlaylistMetadata(const std::string &playlistId) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt = m_db.PrepareStatement(
        "DELETE FROM playlist_metadata WHERE playlist_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    stmt.ExecuteUpdate();
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::DeletePlaylistMetadata exception for '%s': %s",
              playlistId.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  }
}

// ------------------------------------------------------------------
// Сохранение и загрузка полного маппинга
// ------------------------------------------------------------------
bool EPGDatabase::SavePlaylistMapping(
    const std::string &playlistId,
    const std::unordered_map<std::string, std::string> &mapping,
    const std::unordered_map<std::string, std::string> &manualMapping,
    size_t channelCount, const std::string &channelHash,
    const std::string &epgHashAtMatch) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    ScopedTransaction tx(*this);

    // Удаляем старые записи
    Statement delStmt = m_db.PrepareStatement(
        "DELETE FROM playlist_mappings WHERE playlist_id = ?");
    StatementGuard guardDel(delStmt);
    delStmt.Bind(1, wxString::FromUTF8(playlistId));
    delStmt.ExecuteUpdate();

    // Вставляем автоматические
    Statement insStmt =
        m_db.PrepareStatement("INSERT INTO playlist_mappings (playlist_id, "
                              "key, channel_id, is_manual, confidence) "
                              "VALUES (?, ?, ?, ?, ?)");
    StatementGuard guardIns(insStmt);
    for (const auto &[key, chId] : mapping) {
      insStmt.Bind(1, wxString::FromUTF8(playlistId));
      insStmt.Bind(2, wxString::FromUTF8(key));
      insStmt.Bind(3, wxString::FromUTF8(chId));
      insStmt.Bind(4, 0);
      insStmt.Bind(5, "");
      insStmt.ExecuteUpdate();
      insStmt.Reset();
    }
    // Вставляем ручные
    for (const auto &[key, chId] : manualMapping) {
      insStmt.Bind(1, wxString::FromUTF8(playlistId));
      insStmt.Bind(2, wxString::FromUTF8(key));
      insStmt.Bind(3, wxString::FromUTF8(chId));
      insStmt.Bind(4, 1);
      insStmt.Bind(5, "");
      insStmt.ExecuteUpdate();
      insStmt.Reset();
    }

    // Сохраняем метаданные
    PlaylistMetadata meta;
    meta.channelCount = channelCount;
    meta.channelHash = channelHash;
    meta.epgHashAtMatch = epgHashAtMatch;
    meta.lastMatchTime = std::time(nullptr);
    if (!SavePlaylistMetadata(playlistId, meta)) {
      LOG_ERROR("SavePlaylistMapping: failed to save metadata for '%s'",
                playlistId.c_str());
      return false;
    }

    tx.Commit();
    LOG_DEBUG(
        "EPGDatabase: Saved playlist mapping for '%s' (%zu auto, %zu manual)",
        playlistId.c_str(), mapping.size(), manualMapping.size());
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("SavePlaylistMapping exception for '%s': %s", playlistId.c_str(),
              e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::LoadPlaylistMapping(
    const std::string &playlistId,
    std::unordered_map<std::string, std::string> &mapping,
    std::unordered_map<std::string, std::string> &manualMapping,
    size_t &channelCount, std::string &channelHash,
    std::string &epgHashAtMatch) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    mapping.clear();
    manualMapping.clear();

    Statement stmt =
        m_db.PrepareStatement("SELECT key, channel_id, is_manual FROM "
                              "playlist_mappings WHERE playlist_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      std::string key = wx_to_std(rs.GetString(0));
      std::string chId = wx_to_std(rs.GetString(1));
      bool isManual = rs.GetInt(2) != 0;
      if (isManual)
        manualMapping[key] = chId;
      else
        mapping[key] = chId;
    }

    PlaylistMetadata meta;
    if (LoadPlaylistMetadata(playlistId, meta)) {
      channelCount = meta.channelCount;
      channelHash = meta.channelHash;
      epgHashAtMatch = meta.epgHashAtMatch;
      return true;
    }
    return false;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::LoadPlaylistMapping exception for '%s': %s",
              playlistId.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::GetMappingEntry(const std::string &playlistId,
                                  const std::string &key,
                                  std::string &channelId, bool &isManual) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt = m_db.PrepareStatement(
        "SELECT channel_id, is_manual FROM playlist_mappings WHERE playlist_id "
        "= ? AND key = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    stmt.Bind(2, wxString::FromUTF8(key));
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow()) {
      channelId = wx_to_std(rs.GetString(0));
      isManual = rs.GetInt(1) != 0;
      return true;
    }
    return false;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::GetMappingEntry exception for '%s' key '%s': %s",
              playlistId.c_str(), key.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::GetAllMappings(
    const std::string &playlistId,
    std::unordered_map<std::string, MappingEntry> &out) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt =
        m_db.PrepareStatement("SELECT key, channel_id, is_manual, ignored FROM "
                              "playlist_mappings WHERE playlist_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      std::string key = wx_to_std(rs.GetString(0));
      MappingEntry entry;
      entry.epgId = wx_to_std(rs.GetString(1));
      entry.isManual = rs.GetInt(2) != 0;
      entry.ignored = rs.GetInt(3) != 0;
      out[key] = std::move(entry);
    }
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::GetAllMappings exception: %s",
              e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::DeletePlaylistMapping(const std::string &playlistId) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    ScopedTransaction tx(*this);
    Statement stmt = m_db.PrepareStatement(
        "DELETE FROM playlist_mappings WHERE playlist_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    stmt.ExecuteUpdate();
    DeletePlaylistMetadata(playlistId);
    tx.Commit();
    LOG_DEBUG("EPGDatabase: Deleted playlist mapping for '%s'",
              playlistId.c_str());
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::DeletePlaylistMapping exception for '%s': %s",
              playlistId.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  }
}

// ------------------------------------------------------------------
// Хэш каналов EPG
// ------------------------------------------------------------------
std::string EPGDatabase::GetEpgChannelsHash() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return "";

  try {
    Statement stmt = m_db.PrepareStatement(
        "SELECT DISTINCT channel_id FROM programs ORDER BY channel_id");
    StatementGuard guard(stmt);
    ResultSet rs = stmt.ExecuteQuery();
    std::string concatenated;
    while (rs.NextRow()) {
      concatenated += wx_to_std(rs.GetString(0));
      concatenated += "|";
    }
    if (concatenated.empty())
      return "";
    concatenated.pop_back();
    return stable_hash(concatenated);
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::GetEpgChannelsHash exception: %s",
              e.GetMessage().ToUTF8().data());
    return "";
  }
}

// ------------------------------------------------------------------
// Глобальная метаинформация
// ------------------------------------------------------------------
bool EPGDatabase::SaveGlobalMetadata(const std::string &key,
                                     const std::string &value) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;

  try {
    Statement stmt = m_db.PrepareStatement(
        "INSERT OR REPLACE INTO global_metadata (key, value) VALUES (?, ?)");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(key));
    stmt.Bind(2, wxString::FromUTF8(value));
    stmt.ExecuteUpdate();
    return true;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::SaveGlobalMetadata exception for key '%s': %s",
              key.c_str(), e.GetMessage().ToUTF8().data());
    return false;
  }
}

std::string EPGDatabase::LoadGlobalMetadata(const std::string &key) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return "";

  try {
    Statement stmt = m_db.PrepareStatement(
        "SELECT value FROM global_metadata WHERE key = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(key));
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow()) {
      std::string value = wx_to_std(rs.GetString(0));
      return value;
    }
    return "";
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::LoadGlobalMetadata exception for key '%s': %s",
              key.c_str(), e.GetMessage().ToUTF8().data());
    return "";
  }
}

bool EPGDatabase::DeleteMappingEntry(const std::string &playlistId,
                                     const std::string &key) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;
  try {
    Statement stmt = m_db.PrepareStatement(
        "DELETE FROM playlist_mappings WHERE playlist_id = ? AND key = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    stmt.Bind(2, wxString::FromUTF8(key));
    int rows = stmt.ExecuteUpdate();
    return rows > 0;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::DeleteMappingEntry exception: %s",
              e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::SetIgnored(const std::string &playlistId,
                             const std::string &key, bool ignored) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;
  try {
    Statement stmt =
        m_db.PrepareStatement("UPDATE playlist_mappings SET ignored = ? WHERE "
                              "playlist_id = ? AND key = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, ignored ? 1 : 0);
    stmt.Bind(2, wxString::FromUTF8(playlistId));
    stmt.Bind(3, wxString::FromUTF8(key));
    int rows = stmt.ExecuteUpdate();
    return rows > 0;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::SetIgnored exception: %s",
              e.GetMessage().ToUTF8().data());
    return false;
  }
}

bool EPGDatabase::IsIgnored(const std::string &playlistId,
                            const std::string &key) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;
  try {
    Statement stmt =
        m_db.PrepareStatement("SELECT ignored FROM playlist_mappings WHERE "
                              "playlist_id = ? AND key = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxString::FromUTF8(playlistId));
    stmt.Bind(2, wxString::FromUTF8(key));
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow()) {
      return rs.GetInt(0) != 0;
    }
    return false;
  } catch (wxSQLite3::Exception &e) {
    LOG_ERROR("EPGDatabase::IsIgnored exception: %s",
              e.GetMessage().ToUTF8().data());
    return false;
  }
}
