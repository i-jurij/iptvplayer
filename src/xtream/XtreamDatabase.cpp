#include "xtream/XtreamDatabase.h"

#include "LogControl.h"
#include "Utils_FsPath.h"

#include <wx/filename.h>
#include <wx/wxsqlite3.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <regex>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace wxSQLite3;

namespace xtream {

namespace {

constexpr int kCurrentSchemaVersion = 1;
// SQLITE_OPEN_NOFOLLOW (SQLite 3.31+) используем числовое значение напрямую.
constexpr int kWxOpenNofollow = 0x01000000;

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

bool IsValidUuid(const std::string &uuid) {
  static const std::regex kRe(
      "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
  return std::regex_match(uuid, kRe);
}

#if !defined(_WIN32)
bool PrepareDbFile(const wxString &dbPath) {
  // O_NOFOLLOW защищает только последний компонент пути. Родительские
  // каталоги (configDir, xtream/) — ответственность пользователя: они в
  // его домашней директории с известными правами. Замена их симлинком
  // требует локального доступа с теми же правами, что и для чтения .db.
  const int fd =
      ::open(dbPath.utf8_str().data(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
  if (fd < 0) {
    LOG_ERROR("XtreamDatabase: cannot open/create db file '%s': %s",
              dbPath.utf8_str().data(), std::strerror(errno));
    return false;
  }

  // O_CREAT не меняет права уже существующего файла — принудительно 0600.
  // Файл может быть создан старой версией с 0644; правим до db.Open,
  // чтобы избежать окна с ослабленными правами.
  if (::fchmod(fd, 0600) != 0) {
    LOG_WARN("XtreamDatabase: fchmod 0600 failed on '%s': %s "
             "(will retry after Open)",
             dbPath.utf8_str().data(), std::strerror(errno));
  }

  ::close(fd);
  return true;
}
#endif

} // namespace

struct XtreamDatabase::Impl {
  Database db;
  wxString path;
  std::string uuid;
  bool open = false;
  bool inTransaction = false;
  mutable std::recursive_mutex mutex;
};

XtreamDatabase::XtreamDatabase() : m_impl(std::make_unique<Impl>()) {}
XtreamDatabase::~XtreamDatabase() { Close(); }

bool XtreamDatabase::Open(const wxString &configDir, const std::string &uuid) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);

  if (m_impl->open)
    Close();

  if (!IsValidUuid(uuid)) {
    LOG_ERROR("XtreamDatabase::Open: invalid uuid '%s'", uuid.c_str());
    return false;
  }
  m_impl->uuid = uuid;

  wxString xtreamDir = configDir + wxFILE_SEP_PATH + "xtream";

  {
    std::error_code ec;
    auto fsDir = ToFsPath(xtreamDir.utf8_str().data());
    if (std::filesystem::is_symlink(fsDir, ec)) {
      LOG_ERROR("XtreamDatabase::Open: refusing symlink dir '%s'",
                xtreamDir.utf8_str().data());
      return false;
    }
  }

  {
    namespace fs = std::filesystem;
    const fs::path fsDir = ToFsPath(xtreamDir.utf8_str().data());

    std::error_code mkEc;
    fs::create_directories(fsDir, mkEc);
    if (mkEc) {
      LOG_ERROR("XtreamDatabase::Open: cannot create dir '%s': %s",
                xtreamDir.utf8_str().data(), mkEc.message().c_str());
      return false;
    }

    std::error_code dirEc;
    if (!fs::is_directory(fsDir, dirEc) || dirEc) {
      LOG_ERROR("XtreamDatabase::Open: dir '%s' is not accessible: %s",
                xtreamDir.utf8_str().data(), dirEc.message().c_str());
      return false;
    }

    wxFileName dir(xtreamDir);
    if (!dir.SetPermissions(0700)) {
      LOG_WARN("XtreamDatabase: SetPermissions(0700) failed on '%s' "
               "(continuing; create_directories already created)",
               xtreamDir.utf8_str().data());
    }
  }

  wxString dbPath =
      xtreamDir + wxFILE_SEP_PATH + wxString::FromUTF8(uuid) + ".db";

  // Избыточно по отношению к O_NOFOLLOW в PrepareDbFile, но даёт
  // понятное сообщение в логе вместо ELOOP.
  {
    std::error_code ec;
    auto fsPath = ToFsPath(dbPath.utf8_str().data());
    if (std::filesystem::is_symlink(fsPath, ec)) {
      LOG_ERROR("XtreamDatabase::Open: refusing symlink '%s'",
                dbPath.utf8_str().data());
      return false;
    }
  }

#if !defined(_WIN32)
  if (!PrepareDbFile(dbPath))
    return false;
#endif

  try {
    m_impl->db.Open(dbPath, wxEmptyString,
                    OPEN_READWRITE | OPEN_CREATE | kWxOpenNofollow);
    m_impl->path = dbPath;

    m_impl->db.ExecuteUpdate("PRAGMA busy_timeout = 5000");
    m_impl->db.ExecuteUpdate("PRAGMA journal_mode = WAL");
    m_impl->db.ExecuteUpdate("PRAGMA synchronous = NORMAL");
    m_impl->db.ExecuteUpdate("PRAGMA foreign_keys = ON");
    m_impl->db.ExecuteUpdate("PRAGMA wal_autocheckpoint = 1000");
    m_impl->db.ExecuteUpdate("PRAGMA cache_size = -32000");
    m_impl->db.ExecuteUpdate("PRAGMA mmap_size = 0");
    m_impl->db.ExecuteUpdate("PRAGMA temp_store = MEMORY");

    m_impl->db.ExecuteUpdate("BEGIN IMMEDIATE");

    int userVersion = 0;
    {
      Statement stmt = m_impl->db.PrepareStatement("PRAGMA user_version");
      StatementGuard guard(stmt);
      ResultSet rs = stmt.ExecuteQuery();
      if (rs.NextRow())
        userVersion = rs.GetInt(0);
    }

    if (userVersion > kCurrentSchemaVersion) {
      m_impl->db.ExecuteUpdate("ROLLBACK");
      LOG_ERROR("XtreamDatabase: schema version %d > supported %d", userVersion,
                kCurrentSchemaVersion);
      Close();
      return false;
    }

    if (userVersion < kCurrentSchemaVersion) {
      if (!CreateSchemaV1(/*inTransaction=*/true)) {
        try {
          m_impl->db.ExecuteUpdate("ROLLBACK");
        } catch (...) {
        }
        LOG_ERROR("XtreamDatabase: failed to create schema v1");
        Close();
        return false;
      }
      m_impl->db.ExecuteUpdate(
          wxString::Format("PRAGMA user_version = %d", kCurrentSchemaVersion));
    }

    m_impl->db.ExecuteUpdate("COMMIT");

    {
      wxFileName fn(dbPath);
      if (!fn.SetPermissions(0600)) {
        // Не fatal: на POSIX файл уже создан с 0600 в PrepareDbFile
        // (O_CREAT). SetPermissions — страховка для уже существующего
        // файла с ослабленными правами. Если chmod не поддерживается
        // ФС — продолжаем, warning достаточен.
        LOG_WARN("XtreamDatabase: SetPermissions(0600) failed on '%s' "
                 "(continuing; O_CREAT set 0600 at creation)",
                 dbPath.utf8_str().data());
      }
    }

    m_impl->open = true;
    LOG_INFO("XtreamDatabase: opened '%s' (schema v%d)",
             dbPath.utf8_str().data(), kCurrentSchemaVersion);
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::Open: %s", e.GetMessage().utf8_str().data());
    try {
      m_impl->db.ExecuteUpdate("ROLLBACK");
    } catch (...) {
    }
    Close();
    return false;
  } catch (const std::exception &e) {
    LOG_ERROR("XtreamDatabase::Open: %s", e.what());
    try {
      m_impl->db.ExecuteUpdate("ROLLBACK");
    } catch (...) {
    }
    Close();
    return false;
  } catch (...) {
    LOG_ERROR("XtreamDatabase::Open: unknown exception");
    try {
      m_impl->db.ExecuteUpdate("ROLLBACK");
    } catch (...) {
    }
    Close();
    return false;
  }
}

void XtreamDatabase::Close() {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (m_impl->db.IsOpen()) {
    try {
      m_impl->db.Close();
    } catch (...) {
    }
  }
  m_impl->open = false;
  m_impl->inTransaction = false;
  m_impl->path.Clear();
}

bool XtreamDatabase::IsOpen() const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  return m_impl->open;
}

// Вызывается только изнутри BEGIN IMMEDIATE (см. Open). Параметр
// inTransaction защищает от случайного вызова в автокоммите — иначе
// частичная схема останется в базе.
//
// Порядок вставки для будущих data-методов:
//   categories → vod_streams/series → vod_category_map/series_*
// Родительские строки ДО дочерних. Перезапись каталога — через
// INSERT ... ON CONFLICT DO UPDATE (не DELETE + INSERT), иначе каскад
// удалит vod_info, series_seasons, series_episodes.
//
// name_lower в vod_streams и series заполняется в C++ через
// wxString::Lower() при вставке. SQL-функция lower() в
// SQLite работает только для ASCII, кириллица останется в верхнем
// регистре. При поиске query тоже приводится через wxString::Lower().
bool XtreamDatabase::CreateSchemaV1(bool inTransaction) {
  if (!inTransaction) {
    LOG_ERROR("XtreamDatabase::CreateSchemaV1: called outside transaction");
    return false;
  }

  try {
    m_impl->db.ExecuteUpdate("CREATE TABLE IF NOT EXISTS meta ("
                             "key TEXT PRIMARY KEY, value TEXT)");

    m_impl->db.ExecuteUpdate(
        "CREATE TABLE IF NOT EXISTS categories ("
        "id INTEGER, type TEXT, name TEXT, parent_id INTEGER, "
        "sort_order INTEGER, PRIMARY KEY(id, type))");
    m_impl->db.ExecuteUpdate(
        "CREATE INDEX IF NOT EXISTS idx_cat_type ON categories(type)");

    m_impl->db.ExecuteUpdate(
        "CREATE TABLE IF NOT EXISTS live_streams ("
        "stream_id INTEGER PRIMARY KEY, num INTEGER, name TEXT, "
        "stream_icon TEXT, epg_channel_id TEXT, added INTEGER, "
        "category_id INTEGER, tv_archive INTEGER, "
        "tv_archive_duration INTEGER, custom_sid TEXT)");
    m_impl->db.ExecuteUpdate(
        "CREATE INDEX IF NOT EXISTS idx_live_cat ON live_streams(category_id)");

    m_impl->db.ExecuteUpdate(
        "CREATE TABLE IF NOT EXISTS vod_streams ("
        "stream_id INTEGER PRIMARY KEY, name TEXT, name_lower TEXT, "
        "stream_icon TEXT, rating REAL, rating_5based REAL, added INTEGER, "
        "container_extension TEXT, custom_sid TEXT)");
    m_impl->db.ExecuteUpdate("CREATE INDEX IF NOT EXISTS idx_vod_name_lower ON "
                             "vod_streams(name_lower)");
    m_impl->db.ExecuteUpdate(
        "CREATE INDEX IF NOT EXISTS idx_vod_added ON vod_streams(added)");

    m_impl->db.ExecuteUpdate("CREATE TABLE IF NOT EXISTS vod_category_map ("
                             "stream_id INTEGER REFERENCES "
                             "vod_streams(stream_id) ON DELETE CASCADE, "
                             "category_id INTEGER, "
                             "PRIMARY KEY (stream_id, category_id))");
    m_impl->db.ExecuteUpdate("CREATE INDEX IF NOT EXISTS idx_vcm_cat ON "
                             "vod_category_map(category_id)");

    m_impl->db.ExecuteUpdate(
        "CREATE TABLE IF NOT EXISTS vod_info ("
        "stream_id INTEGER PRIMARY KEY "
        "REFERENCES vod_streams(stream_id) ON DELETE CASCADE, "
        "info_json TEXT, fetched_at INTEGER)");

    m_impl->db.ExecuteUpdate(
        "CREATE TABLE IF NOT EXISTS series ("
        "series_id INTEGER PRIMARY KEY, name TEXT, name_lower TEXT, "
        "cover TEXT, plot TEXT, cast_list TEXT, director TEXT, genre TEXT, "
        "releaseDate TEXT, rating REAL, rating_5based REAL, "
        "backdrop_path TEXT, youtube_trailer TEXT, episode_run_time TEXT, "
        "last_modified INTEGER)");
    m_impl->db.ExecuteUpdate("CREATE INDEX IF NOT EXISTS idx_series_name_lower "
                             "ON series(name_lower)");
    m_impl->db.ExecuteUpdate("CREATE INDEX IF NOT EXISTS idx_series_modified "
                             "ON series(last_modified)");

    m_impl->db.ExecuteUpdate(
        "CREATE TABLE IF NOT EXISTS series_category_map ("
        "series_id INTEGER REFERENCES series(series_id) ON DELETE CASCADE, "
        "category_id INTEGER, "
        "PRIMARY KEY (series_id, category_id))");
    m_impl->db.ExecuteUpdate("CREATE INDEX IF NOT EXISTS idx_scm_cat ON "
                             "series_category_map(category_id)");

    m_impl->db.ExecuteUpdate(
        "CREATE TABLE IF NOT EXISTS series_seasons ("
        "series_id INTEGER REFERENCES series(series_id) ON DELETE CASCADE, "
        "season_number INTEGER, name TEXT, cover TEXT, plot TEXT, "
        "episode_count INTEGER, air_date TEXT, "
        "PRIMARY KEY(series_id, season_number))");

    m_impl->db.ExecuteUpdate(
        "CREATE TABLE IF NOT EXISTS series_episodes ("
        "id TEXT PRIMARY KEY, "
        "series_id INTEGER REFERENCES series(series_id) ON DELETE CASCADE, "
        "season_number INTEGER, episode_num INTEGER, title TEXT, "
        "container_extension TEXT, info_json TEXT, custom_sid TEXT, "
        "added INTEGER)");
    m_impl->db.ExecuteUpdate("CREATE INDEX IF NOT EXISTS idx_ep ON "
                             "series_episodes(series_id, season_number)");

    m_impl->db.ExecuteUpdate("CREATE TABLE IF NOT EXISTS playback_positions ("
                             "media_key TEXT PRIMARY KEY, "
                             "position_sec REAL, duration_sec REAL, "
                             "updated_at INTEGER, finished INTEGER DEFAULT 0)");
    m_impl->db.ExecuteUpdate("CREATE INDEX IF NOT EXISTS idx_pp_updated ON "
                             "playback_positions(updated_at)");

    m_impl->db.ExecuteUpdate(
        "CREATE TABLE IF NOT EXISTS user_lists ("
        "media_key TEXT, list_type TEXT, added_at INTEGER, "
        "PRIMARY KEY (media_key, list_type))");
    m_impl->db.ExecuteUpdate(
        "CREATE INDEX IF NOT EXISTS idx_ul_type ON user_lists(list_type)");

    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::CreateSchemaV1: %s",
              e.GetMessage().utf8_str().data());
    return false;
  } catch (const std::exception &e) {
    LOG_ERROR("XtreamDatabase::CreateSchemaV1: %s", e.what());
    return false;
  } catch (...) {
    LOG_ERROR("XtreamDatabase::CreateSchemaV1: unknown exception");
    return false;
  }
}

namespace {

// Замена wxString::FromUTF8 при биндинге.
wxString U8(const std::string &s) { return wxString::FromUTF8(s); }

const char *ListTypeToString(XtreamDatabase::ListType t) {
  switch (t) {
  case XtreamDatabase::ListType::Favorites:
    return "fav";
  case XtreamDatabase::ListType::Watchlist:
    return "watch";
  }
  return "fav";
}

std::string ComputeNameLower(const std::string &name,
                             const std::string &nameLower) {
  if (!nameLower.empty())
    return nameLower;
  if (name.empty())
    return std::string();
  return std::string(wxString::FromUTF8(name).Lower().utf8_str().data());
}
} // namespace


bool XtreamDatabase::SetMeta(const std::string &key, const std::string &value) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open)
    return false;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "INSERT INTO meta (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(key));
    stmt.Bind(2, U8(value));
    stmt.ExecuteUpdate();
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::SetMeta: %s", e.GetMessage().utf8_str().data());
    return false;
  }
}

std::string XtreamDatabase::GetMeta(const std::string &key) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open)
    return std::string();

  try {
    Statement stmt =
        m_impl->db.PrepareStatement("SELECT value FROM meta WHERE key = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(key));
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow())
      return std::string(rs.GetString(0).utf8_str().data());
    return std::string();
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetMeta: %s", e.GetMessage().utf8_str().data());
    return std::string();
  }
}

bool XtreamDatabase::UpsertCategory(const Category &cat) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open)
    return false;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "INSERT INTO categories (id, type, name, parent_id, sort_order) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(id, type) DO UPDATE SET "
        "name = excluded.name, parent_id = excluded.parent_id, "
        "sort_order = excluded.sort_order");
    StatementGuard guard(stmt);
    stmt.Bind(1, cat.id);
    stmt.Bind(2, U8(cat.type));
    stmt.Bind(3, U8(cat.name));
    stmt.Bind(4, cat.parentId);
    stmt.Bind(5, cat.sortOrder);
    stmt.ExecuteUpdate();
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::UpsertCategory: %s",
              e.GetMessage().utf8_str().data());
    return false;
  }
}

std::vector<XtreamDatabase::Category>
XtreamDatabase::GetCategories(const std::string &type) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<Category> out;
  if (!m_impl->open)
    return out;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT id, type, name, parent_id, sort_order FROM categories "
        "WHERE type = ? ORDER BY sort_order, name");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(type));
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      Category c;
      c.id = rs.GetInt(0);
      c.type = std::string(rs.GetString(1).utf8_str().data());
      c.name = std::string(rs.GetString(2).utf8_str().data());
      c.parentId = rs.GetInt(3);
      c.sortOrder = rs.GetInt(4);
      out.push_back(std::move(c));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetCategories: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

bool XtreamDatabase::SavePlaybackPosition(const PlaybackPosition &p) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || p.mediaKey.empty())
    return false;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "INSERT INTO playback_positions "
        "(media_key, position_sec, duration_sec, updated_at, finished) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(media_key) DO UPDATE SET "
        "position_sec = excluded.position_sec, "
        "duration_sec = excluded.duration_sec, "
        "updated_at = excluded.updated_at, "
        "finished = excluded.finished");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(p.mediaKey));
    stmt.Bind(2, p.position);
    stmt.Bind(3, p.duration);
    stmt.Bind(4, wxLongLong(p.updatedAt));
    stmt.Bind(5, p.finished ? 1 : 0);
    stmt.ExecuteUpdate();
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::SavePlaybackPosition: %s",
              e.GetMessage().utf8_str().data());
    return false;
  }
}

XtreamDatabase::PlaybackPosition
XtreamDatabase::GetPlaybackPosition(const std::string &mediaKey) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  PlaybackPosition p;
  p.mediaKey = mediaKey;
  if (!m_impl->open || mediaKey.empty())
    return p;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT position_sec, duration_sec, updated_at, finished "
        "FROM playback_positions WHERE media_key = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(mediaKey));
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow()) {
      p.position = rs.GetDouble(0);
      p.duration = rs.GetDouble(1);
      p.updatedAt = rs.GetInt64(2).GetValue();
      p.finished = rs.GetInt(3) != 0;
    }
    return p;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetPlaybackPosition: %s",
              e.GetMessage().utf8_str().data());
    return p;
  }
}

std::vector<XtreamDatabase::PlaybackPosition>
XtreamDatabase::GetRecentPlaybackPositions(int limit) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<PlaybackPosition> out;
  if (!m_impl->open || limit <= 0)
    return out;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT media_key, position_sec, duration_sec, updated_at, finished "
        "FROM playback_positions ORDER BY updated_at DESC LIMIT ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, limit);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      PlaybackPosition p;
      p.mediaKey = std::string(rs.GetString(0).utf8_str().data());
      p.position = rs.GetDouble(1);
      p.duration = rs.GetDouble(2);
      p.updatedAt = rs.GetInt64(3).GetValue();
      p.finished = rs.GetInt(4) != 0;
      out.push_back(std::move(p));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetRecentPlaybackPositions: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

bool XtreamDatabase::AddToList(const std::string &mediaKey, ListType type) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || mediaKey.empty())
    return false;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "INSERT INTO user_lists (media_key, list_type, added_at) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(media_key, list_type) DO NOTHING");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(mediaKey));
    stmt.Bind(2, U8(ListTypeToString(type)));
    stmt.Bind(3, wxLongLong(std::time(nullptr)));
    stmt.ExecuteUpdate();
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::AddToList: %s",
              e.GetMessage().utf8_str().data());
    return false;
  }
}

bool XtreamDatabase::RemoveFromList(const std::string &mediaKey,
                                    ListType type) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || mediaKey.empty())
    return false;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "DELETE FROM user_lists WHERE media_key = ? AND list_type = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(mediaKey));
    stmt.Bind(2, U8(ListTypeToString(type)));
    return stmt.ExecuteUpdate() > 0;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::RemoveFromList: %s",
              e.GetMessage().utf8_str().data());
    return false;
  }
}

bool XtreamDatabase::IsInList(const std::string &mediaKey,
                              ListType type) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || mediaKey.empty())
    return false;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT 1 FROM user_lists WHERE media_key = ? AND list_type = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(mediaKey));
    stmt.Bind(2, U8(ListTypeToString(type)));
    ResultSet rs = stmt.ExecuteQuery();
    return rs.NextRow();
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::IsInList: %s", e.GetMessage().utf8_str().data());
    return false;
  }
}

std::vector<std::string> XtreamDatabase::GetList(ListType type) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<std::string> out;
  if (!m_impl->open)
    return out;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT media_key FROM user_lists WHERE list_type = ? "
        "ORDER BY added_at DESC");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(ListTypeToString(type)));
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow())
      out.push_back(std::string(rs.GetString(0).utf8_str().data()));
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetList: %s", e.GetMessage().utf8_str().data());
    return out;
  }
}

long long XtreamDatabase::FileSizeBytes() const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || m_impl->path.IsEmpty())
    return -1;

  wxULongLong size = wxFileName::GetSize(m_impl->path);
  if (size == wxInvalidSize)
    return -1;
  return static_cast<long long>(size.GetValue());
}

bool XtreamDatabase::InsertVodStreams(int categoryId,
                                      const std::vector<VodStream> &streams,
                                      const std::atomic<bool> &abortFlag) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || categoryId <= 0)
    return false;

  if (streams.empty())
    return true;

  if (streams.size() > 500000) {
    LOG_ERROR("XtreamDatabase::InsertVodStreams: hard limit exceeded "
              "(category=%d, size=%zu)",
              categoryId, streams.size());
    return false;
  }
  if (streams.size() > 100000) {
    LOG_WARN("XtreamDatabase::InsertVodStreams: soft limit exceeded "
             "(category=%d, size=%zu)",
             categoryId, streams.size());
  }

  try {
    {
      Statement del = m_impl->db.PrepareStatement(
          "DELETE FROM vod_category_map WHERE category_id = ?");
      StatementGuard guard(del);
      del.Bind(1, categoryId);
      del.ExecuteUpdate();
    }

    Statement insStream = m_impl->db.PrepareStatement(
        "INSERT INTO vod_streams "
        "(stream_id, name, name_lower, stream_icon, rating, rating_5based, "
        " added, container_extension, custom_sid) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(stream_id) DO UPDATE SET "
        "name = excluded.name, name_lower = excluded.name_lower, "
        "stream_icon = excluded.stream_icon, rating = excluded.rating, "
        "rating_5based = excluded.rating_5based, added = excluded.added, "
        "container_extension = excluded.container_extension, "
        "custom_sid = excluded.custom_sid");
    StatementGuard guardStream(insStream);

    Statement insMap = m_impl->db.PrepareStatement(
        "INSERT INTO vod_category_map (stream_id, category_id) "
        "VALUES (?, ?) "
        "ON CONFLICT(stream_id, category_id) DO NOTHING");
    StatementGuard guardMap(insMap);

    size_t i = 0;
    for (const auto &s : streams) {
      if ((i % 1000) == 0 && abortFlag.load()) {
        LOG_WARN("XtreamDatabase::InsertVodStreams: abort requested "
                 "(category=%d, at=%zu/%zu)",
                 categoryId, i, streams.size());
        return false;
      }

      const std::string lower = ComputeNameLower(s.name, s.nameLower);

      insStream.Bind(1, wxLongLong(s.streamId));
      insStream.Bind(2, U8(s.name));
      insStream.Bind(3, U8(lower));
      insStream.Bind(4, U8(s.posterUrl));
      insStream.Bind(5, s.rating);
      insStream.Bind(6, s.rating5based);
      insStream.Bind(7, wxLongLong(s.added));
      insStream.Bind(8, U8(s.containerExt));
      insStream.Bind(9, U8(s.customSid));
      insStream.ExecuteUpdate();
      insStream.Reset();

      insMap.Bind(1, wxLongLong(s.streamId));
      insMap.Bind(2, categoryId);
      insMap.ExecuteUpdate();
      insMap.Reset();

      ++i;
    }

    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::InsertVodStreams: %s",
              e.GetMessage().utf8_str().data());
    return false;
  } catch (const std::exception &e) {
    LOG_ERROR("XtreamDatabase::InsertVodStreams: %s", e.what());
    return false;
  } catch (...) {
    LOG_ERROR("XtreamDatabase::InsertVodStreams: unknown exception");
    return false;
  }
}

std::vector<XtreamDatabase::VodStream>
XtreamDatabase::GetVodPage(int categoryId, int64_t afterStreamId,
                           int limit) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<VodStream> out;
  if (!m_impl->open || categoryId <= 0)
    return out;

  if (limit <= 0)
    limit = 500;
  if (limit > 5000)
    limit = 5000;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT s.stream_id, s.name, s.name_lower, s.stream_icon, "
        "       s.rating, s.rating_5based, s.added, "
        "       s.container_extension, s.custom_sid "
        "FROM vod_streams s "
        "JOIN vod_category_map m ON s.stream_id = m.stream_id "
        "WHERE m.category_id = ? AND s.stream_id > ? "
        "ORDER BY s.stream_id LIMIT ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, categoryId);
    stmt.Bind(2, wxLongLong(afterStreamId));
    stmt.Bind(3, limit);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      VodStream s;
      s.streamId = rs.GetInt64(0).GetValue();
      s.name = std::string(rs.GetString(1).utf8_str().data());
      s.nameLower = std::string(rs.GetString(2).utf8_str().data());
      s.posterUrl = std::string(rs.GetString(3).utf8_str().data());
      s.rating = rs.GetDouble(4);
      s.rating5based = rs.GetDouble(5);
      s.added = rs.GetInt64(6).GetValue();
      s.containerExt = std::string(rs.GetString(7).utf8_str().data());
      s.customSid = std::string(rs.GetString(8).utf8_str().data());
      out.push_back(std::move(s));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetVodPage: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

int64_t XtreamDatabase::GetVodCount(int categoryId) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || categoryId <= 0)
    return 0;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT COUNT(*) FROM vod_category_map WHERE category_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, categoryId);
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow())
      return rs.GetInt64(0).GetValue();
    return 0;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetVodCount: %s",
              e.GetMessage().utf8_str().data());
    return 0;
  }
}

bool XtreamDatabase::InsertVodInfo(int64_t streamId,
                                   const std::string &infoJson,
                                   int64_t fetchedAt) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || streamId <= 0)
    return false;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "INSERT INTO vod_info (stream_id, info_json, fetched_at) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(stream_id) DO UPDATE SET "
        "info_json = excluded.info_json, "
        "fetched_at = excluded.fetched_at");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxLongLong(streamId));
    stmt.Bind(2, U8(infoJson));
    stmt.Bind(3, wxLongLong(fetchedAt));
    stmt.ExecuteUpdate();
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::InsertVodInfo: %s",
              e.GetMessage().utf8_str().data());
    return false;
  }
}

std::string XtreamDatabase::GetVodInfo(int64_t streamId) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || streamId <= 0)
    return std::string();

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT info_json FROM vod_info WHERE stream_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxLongLong(streamId));
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow())
      return std::string(rs.GetString(0).utf8_str().data());
    return std::string();
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetVodInfo: %s",
              e.GetMessage().utf8_str().data());
    return std::string();
  }
}

bool XtreamDatabase::InsertSeriesBatch(int categoryId,
                                       const std::vector<Series> &items,
                                       const std::atomic<bool> &abortFlag) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || categoryId <= 0)
    return false;

  if (items.empty())
    return true;

  if (items.size() > 500000) {
    LOG_ERROR("XtreamDatabase::InsertSeriesBatch: hard limit exceeded "
              "(category=%d, size=%zu)",
              categoryId, items.size());
    return false;
  }
  if (items.size() > 100000) {
    LOG_WARN("XtreamDatabase::InsertSeriesBatch: soft limit exceeded "
             "(category=%d, size=%zu)",
             categoryId, items.size());
  }

  try {
    {
      Statement del = m_impl->db.PrepareStatement(
          "DELETE FROM series_category_map WHERE category_id = ?");
      StatementGuard guard(del);
      del.Bind(1, categoryId);
      del.ExecuteUpdate();
    }

    Statement insSeries = m_impl->db.PrepareStatement(
        "INSERT INTO series "
        "(series_id, name, name_lower, cover, plot, cast_list, director, "
        " genre, releaseDate, rating, rating_5based, backdrop_path, "
        " youtube_trailer, episode_run_time, last_modified) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(series_id) DO UPDATE SET "
        "name = excluded.name, name_lower = excluded.name_lower, "
        "cover = excluded.cover, plot = excluded.plot, "
        "cast_list = excluded.cast_list, director = excluded.director, "
        "genre = excluded.genre, releaseDate = excluded.releaseDate, "
        "rating = excluded.rating, rating_5based = excluded.rating_5based, "
        "backdrop_path = excluded.backdrop_path, "
        "youtube_trailer = excluded.youtube_trailer, "
        "episode_run_time = excluded.episode_run_time, "
        "last_modified = excluded.last_modified");
    StatementGuard guardSeries(insSeries);

    Statement insMap = m_impl->db.PrepareStatement(
        "INSERT INTO series_category_map (series_id, category_id) "
        "VALUES (?, ?) "
        "ON CONFLICT(series_id, category_id) DO NOTHING");
    StatementGuard guardMap(insMap);

    size_t i = 0;
    for (const auto &s : items) {
      if ((i % 1000) == 0 && abortFlag.load()) {
        LOG_WARN("XtreamDatabase::InsertSeriesBatch: abort requested "
                 "(category=%d, at=%zu/%zu)",
                 categoryId, i, items.size());
        return false;
      }

      const std::string lower = ComputeNameLower(s.name, s.nameLower);

      insSeries.Bind(1, wxLongLong(s.seriesId));
      insSeries.Bind(2, U8(s.name));
      insSeries.Bind(3, U8(lower));
      insSeries.Bind(4, U8(s.coverUrl));
      insSeries.Bind(5, U8(s.plot));
      insSeries.Bind(6, U8(s.castList));
      insSeries.Bind(7, U8(s.director));
      insSeries.Bind(8, U8(s.genre));
      insSeries.Bind(9, U8(s.releaseDate));
      insSeries.Bind(10, s.rating);
      insSeries.Bind(11, s.rating5based);
      insSeries.Bind(12, U8(s.backdropPath));
      insSeries.Bind(13, U8(s.youtubeTrailer));
      insSeries.Bind(14, U8(s.episodeRunTime));
      insSeries.Bind(15, wxLongLong(s.lastModified));
      insSeries.ExecuteUpdate();
      insSeries.Reset();

      insMap.Bind(1, wxLongLong(s.seriesId));
      insMap.Bind(2, categoryId);
      insMap.ExecuteUpdate();
      insMap.Reset();

      ++i;
    }

    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::InsertSeriesBatch: %s",
              e.GetMessage().utf8_str().data());
    return false;
  } catch (const std::exception &e) {
    LOG_ERROR("XtreamDatabase::InsertSeriesBatch: %s", e.what());
    return false;
  } catch (...) {
    LOG_ERROR("XtreamDatabase::InsertSeriesBatch: unknown exception");
    return false;
  }
}

std::vector<XtreamDatabase::Series>
XtreamDatabase::GetSeriesPage(int categoryId, int64_t afterSeriesId,
                              int limit) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<Series> out;
  if (!m_impl->open || categoryId <= 0)
    return out;

  if (limit <= 0)
    limit = 500;
  if (limit > 5000)
    limit = 5000;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT s.series_id, s.name, s.name_lower, s.cover, s.plot, "
        "       s.cast_list, s.director, s.genre, s.releaseDate, "
        "       s.rating, s.rating_5based, s.backdrop_path, "
        "       s.youtube_trailer, s.episode_run_time, s.last_modified "
        "FROM series s "
        "JOIN series_category_map m ON s.series_id = m.series_id "
        "WHERE m.category_id = ? AND s.series_id > ? "
        "ORDER BY s.series_id LIMIT ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, categoryId);
    stmt.Bind(2, wxLongLong(afterSeriesId));
    stmt.Bind(3, limit);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      Series s;
      s.seriesId = rs.GetInt64(0).GetValue();
      s.name = std::string(rs.GetString(1).utf8_str().data());
      s.nameLower = std::string(rs.GetString(2).utf8_str().data());
      s.coverUrl = std::string(rs.GetString(3).utf8_str().data());
      s.plot = std::string(rs.GetString(4).utf8_str().data());
      s.castList = std::string(rs.GetString(5).utf8_str().data());
      s.director = std::string(rs.GetString(6).utf8_str().data());
      s.genre = std::string(rs.GetString(7).utf8_str().data());
      s.releaseDate = std::string(rs.GetString(8).utf8_str().data());
      s.rating = rs.GetDouble(9);
      s.rating5based = rs.GetDouble(10);
      s.backdropPath = std::string(rs.GetString(11).utf8_str().data());
      s.youtubeTrailer = std::string(rs.GetString(12).utf8_str().data());
      s.episodeRunTime = std::string(rs.GetString(13).utf8_str().data());
      s.lastModified = rs.GetInt64(14).GetValue();
      out.push_back(std::move(s));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetSeriesPage: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

int64_t XtreamDatabase::GetSeriesCount(int categoryId) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || categoryId <= 0)
    return 0;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT COUNT(*) FROM series_category_map WHERE category_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, categoryId);
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow())
      return rs.GetInt64(0).GetValue();
    return 0;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetSeriesCount: %s",
              e.GetMessage().utf8_str().data());
    return 0;
  }
}

bool XtreamDatabase::InsertSeriesInfo(int64_t seriesId,
                                      const std::vector<Season> &seasons,
                                      const std::vector<Episode> &episodes,
                                      const std::atomic<bool> &abortFlag) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || seriesId <= 0)
    return false;

  try {
    {
      Statement del = m_impl->db.PrepareStatement(
          "DELETE FROM series_episodes WHERE series_id = ?");
      StatementGuard guard(del);
      del.Bind(1, wxLongLong(seriesId));
      del.ExecuteUpdate();
    }
    {
      Statement del = m_impl->db.PrepareStatement(
          "DELETE FROM series_seasons WHERE series_id = ?");
      StatementGuard guard(del);
      del.Bind(1, wxLongLong(seriesId));
      del.ExecuteUpdate();
    }

    if (!seasons.empty()) {
      Statement insSeason = m_impl->db.PrepareStatement(
          "INSERT INTO series_seasons "
          "(series_id, season_number, name, cover, plot, episode_count, "
          " air_date) "
          "VALUES (?, ?, ?, ?, ?, ?, ?)");
      StatementGuard guardSeason(insSeason);

      size_t i = 0;
      for (const auto &s : seasons) {
        if ((i % 100) == 0 && abortFlag.load()) {
          LOG_WARN("XtreamDatabase::InsertSeriesInfo: abort (seasons) "
                   "(series=%lld, at=%zu/%zu)",
                   static_cast<long long>(seriesId), i, seasons.size());
          return false;
        }
        insSeason.Bind(1, wxLongLong(seriesId));
        insSeason.Bind(2, s.seasonNumber);
        insSeason.Bind(3, U8(s.name));
        insSeason.Bind(4, U8(s.coverUrl));
        insSeason.Bind(5, U8(s.plot));
        insSeason.Bind(6, s.episodeCount);
        insSeason.Bind(7, U8(s.airDate));
        insSeason.ExecuteUpdate();
        insSeason.Reset();
        ++i;
      }
    }

    if (!episodes.empty()) {
      Statement insEp = m_impl->db.PrepareStatement(
          "INSERT INTO series_episodes "
          "(id, series_id, season_number, episode_num, title, "
          " container_extension, info_json, custom_sid, added) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
          "ON CONFLICT(id) DO UPDATE SET "
          "series_id = excluded.series_id, "
          "season_number = excluded.season_number, "
          "episode_num = excluded.episode_num, "
          "title = excluded.title, "
          "container_extension = excluded.container_extension, "
          "info_json = excluded.info_json, "
          "custom_sid = excluded.custom_sid, "
          "added = excluded.added");
      StatementGuard guardEp(insEp);

      size_t i = 0;
      for (const auto &e : episodes) {
        if ((i % 1000) == 0 && abortFlag.load()) {
          LOG_WARN("XtreamDatabase::InsertSeriesInfo: abort (episodes) "
                   "(series=%lld, at=%zu/%zu)",
                   static_cast<long long>(seriesId), i, episodes.size());
          return false;
        }
        insEp.Bind(1, U8(e.id));
        insEp.Bind(2, wxLongLong(seriesId));
        insEp.Bind(3, e.seasonNumber);
        insEp.Bind(4, e.episodeNum);
        insEp.Bind(5, U8(e.title));
        insEp.Bind(6, U8(e.containerExt));
        insEp.Bind(7, U8(e.infoJson));
        insEp.Bind(8, U8(e.customSid));
        insEp.Bind(9, wxLongLong(e.added));
        insEp.ExecuteUpdate();
        insEp.Reset();
        ++i;
      }
    }

    return true;
  } catch (const wxSQLite3::Exception &ex) {
    LOG_ERROR("XtreamDatabase::InsertSeriesInfo: %s",
              ex.GetMessage().utf8_str().data());
    return false;
  } catch (const std::exception &ex) {
    LOG_ERROR("XtreamDatabase::InsertSeriesInfo: %s", ex.what());
    return false;
  } catch (...) {
    LOG_ERROR("XtreamDatabase::InsertSeriesInfo: unknown exception");
    return false;
  }
}

std::vector<XtreamDatabase::Season>
XtreamDatabase::GetSeasons(int64_t seriesId) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<Season> out;
  if (!m_impl->open || seriesId <= 0)
    return out;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT series_id, season_number, name, cover, plot, "
        "       episode_count, air_date "
        "FROM series_seasons WHERE series_id = ? "
        "ORDER BY season_number");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxLongLong(seriesId));
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      Season s;
      s.seriesId = rs.GetInt64(0).GetValue();
      s.seasonNumber = rs.GetInt(1);
      s.name = std::string(rs.GetString(2).utf8_str().data());
      s.coverUrl = std::string(rs.GetString(3).utf8_str().data());
      s.plot = std::string(rs.GetString(4).utf8_str().data());
      s.episodeCount = rs.GetInt(5);
      s.airDate = std::string(rs.GetString(6).utf8_str().data());
      out.push_back(std::move(s));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetSeasons: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

std::vector<XtreamDatabase::Episode>
XtreamDatabase::GetEpisodes(int64_t seriesId, int seasonNumber) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<Episode> out;
  if (!m_impl->open || seriesId <= 0)
    return out;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT id, series_id, season_number, episode_num, title, "
        "       container_extension, info_json, custom_sid, added "
        "FROM series_episodes "
        "WHERE series_id = ? AND season_number = ? "
        "ORDER BY episode_num");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxLongLong(seriesId));
    stmt.Bind(2, seasonNumber);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      Episode e;
      e.id = std::string(rs.GetString(0).utf8_str().data());
      e.seriesId = rs.GetInt64(1).GetValue();
      e.seasonNumber = rs.GetInt(2);
      e.episodeNum = rs.GetInt(3);
      e.title = std::string(rs.GetString(4).utf8_str().data());
      e.containerExt = std::string(rs.GetString(5).utf8_str().data());
      e.infoJson = std::string(rs.GetString(6).utf8_str().data());
      e.customSid = std::string(rs.GetString(7).utf8_str().data());
      e.added = rs.GetInt64(8).GetValue();
      out.push_back(std::move(e));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetEpisodes: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

std::vector<XtreamDatabase::VodStream>
XtreamDatabase::SearchVod(const std::string &queryLower, int limit) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<VodStream> out;
  if (!m_impl->open || queryLower.empty())
    return out;

  if (limit <= 0)
    limit = 200;
  if (limit > 1000)
    limit = 1000;

  try {
    const std::string pattern = "%" + queryLower + "%";
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT stream_id, name, name_lower, stream_icon, rating, "
        "       rating_5based, added, container_extension, custom_sid "
        "FROM vod_streams WHERE name_lower LIKE ? "
        "ORDER BY added DESC LIMIT ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(pattern));
    stmt.Bind(2, limit);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      VodStream s;
      s.streamId = rs.GetInt64(0).GetValue();
      s.name = std::string(rs.GetString(1).utf8_str().data());
      s.nameLower = std::string(rs.GetString(2).utf8_str().data());
      s.posterUrl = std::string(rs.GetString(3).utf8_str().data());
      s.rating = rs.GetDouble(4);
      s.rating5based = rs.GetDouble(5);
      s.added = rs.GetInt64(6).GetValue();
      s.containerExt = std::string(rs.GetString(7).utf8_str().data());
      s.customSid = std::string(rs.GetString(8).utf8_str().data());
      out.push_back(std::move(s));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::SearchVod: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

std::vector<XtreamDatabase::Series>
XtreamDatabase::SearchSeries(const std::string &queryLower, int limit) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<Series> out;
  if (!m_impl->open || queryLower.empty())
    return out;

  if (limit <= 0)
    limit = 200;
  if (limit > 1000)
    limit = 1000;

  try {
    const std::string pattern = "%" + queryLower + "%";
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT series_id, name, name_lower, cover, plot, cast_list, "
        "       director, genre, releaseDate, rating, rating_5based, "
        "       backdrop_path, youtube_trailer, episode_run_time, "
        "       last_modified "
        "FROM series WHERE name_lower LIKE ? "
        "ORDER BY last_modified DESC LIMIT ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(pattern));
    stmt.Bind(2, limit);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      Series s;
      s.seriesId = rs.GetInt64(0).GetValue();
      s.name = std::string(rs.GetString(1).utf8_str().data());
      s.nameLower = std::string(rs.GetString(2).utf8_str().data());
      s.coverUrl = std::string(rs.GetString(3).utf8_str().data());
      s.plot = std::string(rs.GetString(4).utf8_str().data());
      s.castList = std::string(rs.GetString(5).utf8_str().data());
      s.director = std::string(rs.GetString(6).utf8_str().data());
      s.genre = std::string(rs.GetString(7).utf8_str().data());
      s.releaseDate = std::string(rs.GetString(8).utf8_str().data());
      s.rating = rs.GetDouble(9);
      s.rating5based = rs.GetDouble(10);
      s.backdropPath = std::string(rs.GetString(11).utf8_str().data());
      s.youtubeTrailer = std::string(rs.GetString(12).utf8_str().data());
      s.episodeRunTime = std::string(rs.GetString(13).utf8_str().data());
      s.lastModified = rs.GetInt64(14).GetValue();
      out.push_back(std::move(s));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::SearchSeries: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

std::vector<XtreamDatabase::VodStream>
XtreamDatabase::GetRecentlyAddedVod(int limit) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<VodStream> out;
  if (!m_impl->open)
    return out;

  if (limit <= 0)
    limit = 100;
  if (limit > 500)
    limit = 500;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT stream_id, name, name_lower, stream_icon, rating, "
        "       rating_5based, added, container_extension, custom_sid "
        "FROM vod_streams ORDER BY added DESC LIMIT ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, limit);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      VodStream s;
      s.streamId = rs.GetInt64(0).GetValue();
      s.name = std::string(rs.GetString(1).utf8_str().data());
      s.nameLower = std::string(rs.GetString(2).utf8_str().data());
      s.posterUrl = std::string(rs.GetString(3).utf8_str().data());
      s.rating = rs.GetDouble(4);
      s.rating5based = rs.GetDouble(5);
      s.added = rs.GetInt64(6).GetValue();
      s.containerExt = std::string(rs.GetString(7).utf8_str().data());
      s.customSid = std::string(rs.GetString(8).utf8_str().data());
      out.push_back(std::move(s));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetRecentlyAddedVod: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

std::vector<XtreamDatabase::Series>
XtreamDatabase::GetRecentlyAddedSeries(int limit) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<Series> out;
  if (!m_impl->open)
    return out;

  if (limit <= 0)
    limit = 100;
  if (limit > 500)
    limit = 500;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT series_id, name, name_lower, cover, plot, cast_list, "
        "       director, genre, releaseDate, rating, rating_5based, "
        "       backdrop_path, youtube_trailer, episode_run_time, "
        "       last_modified "
        "FROM series ORDER BY last_modified DESC LIMIT ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, limit);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      Series s;
      s.seriesId = rs.GetInt64(0).GetValue();
      s.name = std::string(rs.GetString(1).utf8_str().data());
      s.nameLower = std::string(rs.GetString(2).utf8_str().data());
      s.coverUrl = std::string(rs.GetString(3).utf8_str().data());
      s.plot = std::string(rs.GetString(4).utf8_str().data());
      s.castList = std::string(rs.GetString(5).utf8_str().data());
      s.director = std::string(rs.GetString(6).utf8_str().data());
      s.genre = std::string(rs.GetString(7).utf8_str().data());
      s.releaseDate = std::string(rs.GetString(8).utf8_str().data());
      s.rating = rs.GetDouble(9);
      s.rating5based = rs.GetDouble(10);
      s.backdropPath = std::string(rs.GetString(11).utf8_str().data());
      s.youtubeTrailer = std::string(rs.GetString(12).utf8_str().data());
      s.episodeRunTime = std::string(rs.GetString(13).utf8_str().data());
      s.lastModified = rs.GetInt64(14).GetValue();
      out.push_back(std::move(s));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetRecentlyAddedSeries: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

XtreamDatabase::VodStream XtreamDatabase::GetVod(int64_t streamId) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  VodStream s;
  if (!m_impl->open || streamId <= 0)
    return s;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT stream_id, name, name_lower, stream_icon, rating, "
        "       rating_5based, added, container_extension, custom_sid "
        "FROM vod_streams WHERE stream_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxLongLong(streamId));
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow()) {
      s.streamId = rs.GetInt64(0).GetValue();
      s.name = std::string(rs.GetString(1).utf8_str().data());
      s.nameLower = std::string(rs.GetString(2).utf8_str().data());
      s.posterUrl = std::string(rs.GetString(3).utf8_str().data());
      s.rating = rs.GetDouble(4);
      s.rating5based = rs.GetDouble(5);
      s.added = rs.GetInt64(6).GetValue();
      s.containerExt = std::string(rs.GetString(7).utf8_str().data());
      s.customSid = std::string(rs.GetString(8).utf8_str().data());
    }
    return s;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetVod: %s", e.GetMessage().utf8_str().data());
    return s;
  }
}

XtreamDatabase::Series XtreamDatabase::GetSeries(int64_t seriesId) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  Series s;
  if (!m_impl->open || seriesId <= 0)
    return s;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT series_id, name, name_lower, cover, plot, cast_list, "
        "       director, genre, releaseDate, rating, rating_5based, "
        "       backdrop_path, youtube_trailer, episode_run_time, "
        "       last_modified "
        "FROM series WHERE series_id = ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, wxLongLong(seriesId));
    ResultSet rs = stmt.ExecuteQuery();
    if (rs.NextRow()) {
      s.seriesId = rs.GetInt64(0).GetValue();
      s.name = std::string(rs.GetString(1).utf8_str().data());
      s.nameLower = std::string(rs.GetString(2).utf8_str().data());
      s.coverUrl = std::string(rs.GetString(3).utf8_str().data());
      s.plot = std::string(rs.GetString(4).utf8_str().data());
      s.castList = std::string(rs.GetString(5).utf8_str().data());
      s.director = std::string(rs.GetString(6).utf8_str().data());
      s.genre = std::string(rs.GetString(7).utf8_str().data());
      s.releaseDate = std::string(rs.GetString(8).utf8_str().data());
      s.rating = rs.GetDouble(9);
      s.rating5based = rs.GetDouble(10);
      s.backdropPath = std::string(rs.GetString(11).utf8_str().data());
      s.youtubeTrailer = std::string(rs.GetString(12).utf8_str().data());
      s.episodeRunTime = std::string(rs.GetString(13).utf8_str().data());
      s.lastModified = rs.GetInt64(14).GetValue();
    }
    return s;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetSeries: %s",
              e.GetMessage().utf8_str().data());
    return s;
  }
}

bool XtreamDatabase::BeginTransaction() {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || m_impl->inTransaction)
    return false;

  try {
    m_impl->db.ExecuteUpdate("BEGIN IMMEDIATE");
    m_impl->inTransaction = true;
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::BeginTransaction: %s",
              e.GetMessage().utf8_str().data());
    return false;
  }
}

bool XtreamDatabase::Commit() {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || !m_impl->inTransaction)
    return false;

  try {
    m_impl->db.ExecuteUpdate("COMMIT");
    m_impl->inTransaction = false;
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::Commit: %s", e.GetMessage().utf8_str().data());
    return false;
  }
}

bool XtreamDatabase::Rollback() {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || !m_impl->inTransaction)
    return false;

  try {
    m_impl->db.ExecuteUpdate("ROLLBACK");
    m_impl->inTransaction = false;
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::Rollback: %s", e.GetMessage().utf8_str().data());
    return false;
  }
}

bool XtreamDatabase::ClearVodCategoryMap(int categoryId) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || categoryId <= 0)
    return false;

  try {
    Statement del = m_impl->db.PrepareStatement(
        "DELETE FROM vod_category_map WHERE category_id = ?");
    StatementGuard guard(del);
    del.Bind(1, categoryId);
    del.ExecuteUpdate();
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::ClearVodCategoryMap: %s",
              e.GetMessage().utf8_str().data());
    return false;
  }
}

bool XtreamDatabase::UpsertVodBatch(int categoryId,
                                    const std::vector<VodStream> &batch,
                                    const std::atomic<bool> &abortFlag) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || categoryId <= 0)
    return false;

  if (batch.empty())
    return true;

  try {
    Statement insStream = m_impl->db.PrepareStatement(
        "INSERT INTO vod_streams "
        "(stream_id, name, name_lower, stream_icon, rating, rating_5based, "
        " added, container_extension, custom_sid) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(stream_id) DO UPDATE SET "
        "name = excluded.name, name_lower = excluded.name_lower, "
        "stream_icon = excluded.stream_icon, rating = excluded.rating, "
        "rating_5based = excluded.rating_5based, added = excluded.added, "
        "container_extension = excluded.container_extension, "
        "custom_sid = excluded.custom_sid");
    StatementGuard guardStream(insStream);

    Statement insMap = m_impl->db.PrepareStatement(
        "INSERT INTO vod_category_map (stream_id, category_id) "
        "VALUES (?, ?) "
        "ON CONFLICT(stream_id, category_id) DO NOTHING");
    StatementGuard guardMap(insMap);

    size_t i = 0;
    for (const auto &s : batch) {
      if ((i % 1000) == 0 && abortFlag.load()) {
        LOG_WARN("XtreamDatabase::UpsertVodBatch: abort (category=%d, "
                 "at=%zu/%zu)",
                 categoryId, i, batch.size());
        return false;
      }

      const std::string lower = ComputeNameLower(s.name, s.nameLower);

      insStream.Bind(1, wxLongLong(s.streamId));
      insStream.Bind(2, U8(s.name));
      insStream.Bind(3, U8(lower));
      insStream.Bind(4, U8(s.posterUrl));
      insStream.Bind(5, s.rating);
      insStream.Bind(6, s.rating5based);
      insStream.Bind(7, wxLongLong(s.added));
      insStream.Bind(8, U8(s.containerExt));
      insStream.Bind(9, U8(s.customSid));
      insStream.ExecuteUpdate();
      insStream.Reset();

      insMap.Bind(1, wxLongLong(s.streamId));
      insMap.Bind(2, categoryId);
      insMap.ExecuteUpdate();
      insMap.Reset();

      ++i;
    }

    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::UpsertVodBatch: %s",
              e.GetMessage().utf8_str().data());
    return false;
  } catch (const std::exception &e) {
    LOG_ERROR("XtreamDatabase::UpsertVodBatch: %s", e.what());
    return false;
  } catch (...) {
    LOG_ERROR("XtreamDatabase::UpsertVodBatch: unknown exception");
    return false;
  }
}

bool XtreamDatabase::ClearSeriesCategoryMap(int categoryId) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || categoryId <= 0)
    return false;

  try {
    Statement del = m_impl->db.PrepareStatement(
        "DELETE FROM series_category_map WHERE category_id = ?");
    StatementGuard guard(del);
    del.Bind(1, categoryId);
    del.ExecuteUpdate();
    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::ClearSeriesCategoryMap: %s",
              e.GetMessage().utf8_str().data());
    return false;
  }
}

bool XtreamDatabase::UpsertSeriesBatch(int categoryId,
                                       const std::vector<Series> &batch,
                                       const std::atomic<bool> &abortFlag) {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  if (!m_impl->open || categoryId <= 0)
    return false;

  if (batch.empty())
    return true;

  try {
    Statement insSeries = m_impl->db.PrepareStatement(
        "INSERT INTO series "
        "(series_id, name, name_lower, cover, plot, cast_list, director, "
        " genre, releaseDate, rating, rating_5based, backdrop_path, "
        " youtube_trailer, episode_run_time, last_modified) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(series_id) DO UPDATE SET "
        "name = excluded.name, name_lower = excluded.name_lower, "
        "cover = excluded.cover, plot = excluded.plot, "
        "cast_list = excluded.cast_list, director = excluded.director, "
        "genre = excluded.genre, releaseDate = excluded.releaseDate, "
        "rating = excluded.rating, rating_5based = excluded.rating_5based, "
        "backdrop_path = excluded.backdrop_path, "
        "youtube_trailer = excluded.youtube_trailer, "
        "episode_run_time = excluded.episode_run_time, "
        "last_modified = excluded.last_modified");
    StatementGuard guardSeries(insSeries);

    Statement insMap = m_impl->db.PrepareStatement(
        "INSERT INTO series_category_map (series_id, category_id) "
        "VALUES (?, ?) "
        "ON CONFLICT(series_id, category_id) DO NOTHING");
    StatementGuard guardMap(insMap);

    size_t i = 0;
    for (const auto &s : batch) {
      if ((i % 1000) == 0 && abortFlag.load()) {
        LOG_WARN("XtreamDatabase::UpsertSeriesBatch: abort (category=%d, "
                 "at=%zu/%zu)",
                 categoryId, i, batch.size());
        return false;
      }

      const std::string lower = ComputeNameLower(s.name, s.nameLower);

      insSeries.Bind(1, wxLongLong(s.seriesId));
      insSeries.Bind(2, U8(s.name));
      insSeries.Bind(3, U8(lower));
      insSeries.Bind(4, U8(s.coverUrl));
      insSeries.Bind(5, U8(s.plot));
      insSeries.Bind(6, U8(s.castList));
      insSeries.Bind(7, U8(s.director));
      insSeries.Bind(8, U8(s.genre));
      insSeries.Bind(9, U8(s.releaseDate));
      insSeries.Bind(10, s.rating);
      insSeries.Bind(11, s.rating5based);
      insSeries.Bind(12, U8(s.backdropPath));
      insSeries.Bind(13, U8(s.youtubeTrailer));
      insSeries.Bind(14, U8(s.episodeRunTime));
      insSeries.Bind(15, wxLongLong(s.lastModified));
      insSeries.ExecuteUpdate();
      insSeries.Reset();

      insMap.Bind(1, wxLongLong(s.seriesId));
      insMap.Bind(2, categoryId);
      insMap.ExecuteUpdate();
      insMap.Reset();

      ++i;
    }

    return true;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::UpsertSeriesBatch: %s",
              e.GetMessage().utf8_str().data());
    return false;
  } catch (const std::exception &e) {
    LOG_ERROR("XtreamDatabase::UpsertSeriesBatch: %s", e.what());
    return false;
  } catch (...) {
    LOG_ERROR("XtreamDatabase::UpsertSeriesBatch: unknown exception");
    return false;
  }
}

std::vector<XtreamDatabase::VodStream>
XtreamDatabase::GetVodByList(ListType type, int limit) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<VodStream> out;
  if (!m_impl->open)
    return out;

  if (limit <= 0)
    limit = 1000;
  if (limit > 5000)
    limit = 5000;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT s.stream_id, s.name, s.name_lower, s.stream_icon, "
        "       s.rating, s.rating_5based, s.added, "
        "       s.container_extension, s.custom_sid "
        "FROM vod_streams s "
        "INNER JOIN user_lists l "
        "  ON l.media_key = 'vod:' || s.stream_id "
        "WHERE l.list_type = ? "
        "ORDER BY l.added_at DESC LIMIT ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(ListTypeToString(type)));
    stmt.Bind(2, limit);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      VodStream s;
      s.streamId = rs.GetInt64(0).GetValue();
      s.name = std::string(rs.GetString(1).utf8_str().data());
      s.nameLower = std::string(rs.GetString(2).utf8_str().data());
      s.posterUrl = std::string(rs.GetString(3).utf8_str().data());
      s.rating = rs.GetDouble(4);
      s.rating5based = rs.GetDouble(5);
      s.added = rs.GetInt64(6).GetValue();
      s.containerExt = std::string(rs.GetString(7).utf8_str().data());
      s.customSid = std::string(rs.GetString(8).utf8_str().data());
      out.push_back(std::move(s));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetVodByList: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}

std::vector<XtreamDatabase::Series>
XtreamDatabase::GetSeriesByList(ListType type, int limit) const {
  std::lock_guard<std::recursive_mutex> lock(m_impl->mutex);
  std::vector<Series> out;
  if (!m_impl->open)
    return out;

  if (limit <= 0)
    limit = 1000;
  if (limit > 5000)
    limit = 5000;

  try {
    Statement stmt = m_impl->db.PrepareStatement(
        "SELECT s.series_id, s.name, s.name_lower, s.cover, s.plot, "
        "       s.cast_list, s.director, s.genre, s.releaseDate, "
        "       s.rating, s.rating_5based, s.backdrop_path, "
        "       s.youtube_trailer, s.episode_run_time, s.last_modified "
        "FROM series s "
        "INNER JOIN user_lists l "
        "  ON l.media_key = 'series:' || s.series_id "
        "WHERE l.list_type = ? "
        "ORDER BY l.added_at DESC LIMIT ?");
    StatementGuard guard(stmt);
    stmt.Bind(1, U8(ListTypeToString(type)));
    stmt.Bind(2, limit);
    ResultSet rs = stmt.ExecuteQuery();
    while (rs.NextRow()) {
      Series s;
      s.seriesId = rs.GetInt64(0).GetValue();
      s.name = std::string(rs.GetString(1).utf8_str().data());
      s.nameLower = std::string(rs.GetString(2).utf8_str().data());
      s.coverUrl = std::string(rs.GetString(3).utf8_str().data());
      s.plot = std::string(rs.GetString(4).utf8_str().data());
      s.castList = std::string(rs.GetString(5).utf8_str().data());
      s.director = std::string(rs.GetString(6).utf8_str().data());
      s.genre = std::string(rs.GetString(7).utf8_str().data());
      s.releaseDate = std::string(rs.GetString(8).utf8_str().data());
      s.rating = rs.GetDouble(9);
      s.rating5based = rs.GetDouble(10);
      s.backdropPath = std::string(rs.GetString(11).utf8_str().data());
      s.youtubeTrailer = std::string(rs.GetString(12).utf8_str().data());
      s.episodeRunTime = std::string(rs.GetString(13).utf8_str().data());
      s.lastModified = rs.GetInt64(14).GetValue();
      out.push_back(std::move(s));
    }
    return out;
  } catch (const wxSQLite3::Exception &e) {
    LOG_ERROR("XtreamDatabase::GetSeriesByList: %s",
              e.GetMessage().utf8_str().data());
    return out;
  }
}
} // namespace xtream