#include "PlaylistManager.h"
#include "ConfigManager.h"
#include "ErrorCode.h"
#include "EventIDs.h"
#include "LogControl.h"
#include "LogoCache.h"
#include "M3UExporter.h"
#include "M3UParser.h"
#include "PlaylistJson.h"
#include "MainFrame.h"
#include "Playlist.h"
#include "Utils.h"
#include "Utils_FsPath.h"
#include "Utils_Net.h"
#include "xtream/XtreamApiClient.h"

#include <wx/app.h>
#include <wx/event.h>
#include <wx/stdpaths.h>
#include <wx/string.h>
#include <wx/window.h>

#include <curl/curl.h>

#ifdef _WIN32
#include <io.h>
#endif

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

static std::string wstringToUtf8(const std::wstring &ws) {
  wxString tmp(ws);
  return std::string(tmp.ToUTF8().data());
}

#ifdef _WIN32
// Windows: абсолютный путь → extended-length формат (\\?\...), снимает
// ограничение MAX_PATH (260) для _wfopen / MoveFileExW / _wremove.
// Относительный путь и UNC получают соответствующий префикс.
static std::wstring ToExtendedLengthPath(const std::filesystem::path &p) {
  std::wstring w = p.wstring();
  for (auto &c : w)
    if (c == L'/')
      c = L'\\';
  if (w.compare(0, 4, L"\\\\?\\") == 0)
    return w;
  if (w.size() >= 2 && w[1] == L':')
    return L"\\\\?\\" + w;
  if (w.compare(0, 2, L"\\\\") == 0)
    return L"\\\\?\\UNC\\" + w.substr(2);
  return w;
}
#endif

static size_t WriteCallback(char *ptr, size_t size, size_t nmemb,
                            void *userdata) {
  std::string *out = static_cast<std::string *>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

PlaylistManager::PlaylistManager(const std::string &configDir,
                                 ConfigManager *cfg)
    : m_configManager(cfg), m_configDir(configDir) {
  // Защита от trailing separator на входе
  while (m_configDir.size() > 1 &&
         (m_configDir.back() == '/' || m_configDir.back() == '\\')) {
    m_configDir.pop_back();
  }

  CurlGlobal::instance();
  m_xtreamClient = std::make_shared<xtream::XtreamApiClient>();
}

PlaylistManager::~PlaylistManager() {
  StopCatalogAutoUpdate();

  CancelPendingAddFromUrl();

  std::lock_guard<std::mutex> lk(m_xtreamDbMutex);
  m_xtreamDbs.clear();
}

void PlaylistManager::setError(const std::string &msg) const {
  setLastError(msg);
}

bool PlaylistManager::readFile(const std::string &path,
                               std::string &outContent) {
  std::filesystem::path fsPath = ToFsPath(path);
  std::ifstream in(fsPath, std::ios::binary | std::ios::ate);
  if (!in) {
    std::string utf8Path(fsPath.u8string().begin(), fsPath.u8string().end());
    setLastError("Cannot open file: " + utf8Path);
    return false;
  }

  std::streamsize size = in.tellg();
  if (size < 0 || size > 50 * 1024 * 1024) {
    std::string utf8Path(fsPath.u8string().begin(), fsPath.u8string().end());
    setLastError("File too large or invalid: " + utf8Path);
    return false;
  }

  in.seekg(0, std::ios::beg);

  outContent.resize(static_cast<size_t>(size));
  if (!in.read(&outContent[0], size)) {
    std::string utf8Path(fsPath.u8string().begin(), fsPath.u8string().end());
    setLastError("Error reading file: " + utf8Path);
    return false;
  }

  return true;
}

std::size_t PlaylistManager::size() const {
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  return m_playlists.size();
}

static int ProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow) {
  (void)ultotal; // не используется
  (void)ulnow;

  DownloadProgress *prog = static_cast<DownloadProgress *>(clientp);
  if (!prog)
    return 0;

  // Обновляем общий размер (если известен)
  if (dltotal > 0) {
    prog->totalBytes = static_cast<double>(dltotal);
  }
  prog->downloadedBytes = static_cast<double>(dlnow);

  // Проверка отмены
  if (prog->abort.load()) {
    return -1; // прервать загрузку
  }

  // внешний cancel (например, из AddIPTVPlaylistDialog::FetchToken).
  // Безопасно при nullptr — просто поле не установлено вызывающим.
  if (prog->externalCancel &&
      prog->externalCancel->load(std::memory_order_acquire)) {
    prog->abort = true;
    return -1;
  }

  // Проверка зависания (если прогресс не меняется >30 сек)
  auto now = std::chrono::steady_clock::now();
  if (prog->lastDownloadedBytes == dlnow) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - prog->lastProgressTime);
    if (elapsed.count() > 30) {
      prog->abort = true;
      return -1;
    }
  } else {
    prog->lastDownloadedBytes = dlnow;
    prog->lastProgressTime = now;
  }

  return 0;
}

ErrorCode PlaylistManager::downloadUrl(const std::string &url,
                                       std::string &content,
                                       const std::string &userAgent,
                                       DownloadProgress *progress) {
  CurlGlobal::instance();

  CurlSession sess;
  if (!sess.get()) {
    setLastError("Failed to initialize CURL");
    return ErrorCode::CurlInitError;
  }

  ApplyCurlCaBundle(sess.get());

  content.clear();
  content.reserve(256 * 1024);

  curl_easy_setopt(sess.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(sess.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(sess.get(), CURLOPT_WRITEDATA, &content);
  curl_easy_setopt(sess.get(), CURLOPT_FOLLOWLOCATION, 1L);
  // Убираем жёсткий таймаут:
  // curl_easy_setopt(sess.get(), CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(sess.get(), CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(sess.get(), CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(sess.get(), CURLOPT_MAXFILESIZE,
                   100 * 1024 * 1024L); // 100 MB лимит

  if (!userAgent.empty())
    curl_easy_setopt(sess.get(), CURLOPT_USERAGENT, userAgent.c_str());

  // Прогресс + опциональные таймауты (задаются вызывающим через progress).
  if (progress) {
    curl_easy_setopt(sess.get(), CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(sess.get(), CURLOPT_XFERINFODATA, progress);
    curl_easy_setopt(sess.get(), CURLOPT_NOPROGRESS, 0L);
    progress->lastProgressTime = std::chrono::steady_clock::now();
    progress->lastDownloadedBytes = 0.0;

    // применяются только если вызывающий установил ненулевые значения.
    if (progress->connectTimeoutSec > 0)
      curl_easy_setopt(sess.get(), CURLOPT_CONNECTTIMEOUT,
                       progress->connectTimeoutSec);
    if (progress->totalTimeoutSec > 0)
      curl_easy_setopt(sess.get(), CURLOPT_TIMEOUT,
                       progress->totalTimeoutSec);
  }

  CURLcode rc = curl_easy_perform(sess.get());
  if (rc != CURLE_OK) {
    setLastError("CURL error: " + std::string(curl_easy_strerror(rc)));
    return ErrorCode::NetworkError;
  }

  long http = 0;
  curl_easy_getinfo(sess.get(), CURLINFO_RESPONSE_CODE, &http);
  if (http != 200) {
    setLastError("HTTP error: " + std::to_string(http));
    return ErrorCode::HttpError;
  }

  // Если загрузка была отменена
  if (progress && progress->abort.load()) {
    setLastError("Download aborted by user or stalled");
    return ErrorCode::NetworkError;
  }

  return ErrorCode::OK;
}

namespace {
// RAII-флаг занятости типа. Auto-update проверяет перед запуском.
struct AtomicFlagGuard {
  std::atomic<bool> &flag;
  explicit AtomicFlagGuard(std::atomic<bool> &f) : flag(f) {
    flag.store(true, std::memory_order_release);
  }
  ~AtomicFlagGuard() { flag.store(false, std::memory_order_release); }
  AtomicFlagGuard(const AtomicFlagGuard &) = delete;
  AtomicFlagGuard &operator=(const AtomicFlagGuard &) = delete;
};
} // namespace

PlaylistManager::LiveRefreshResult
PlaylistManager::RefreshLiveCatalog(const Playlist &playlist) {
  LiveRefreshResult result;
  result.code = ErrorCode::Unknown;

  if (!playlist.isXtream()) {
    setLastError("RefreshLiveCatalog: not Xtream playlist");
    return result;
  }

  AtomicFlagGuard flagGuard(m_liveRefreshingAtomic);
  if (!m_xtreamClient) {
    setLastError("RefreshLiveCatalog: XtreamApiClient not initialized");
    return result;
  }

  xtream::XtreamDatabase *db = GetXtreamDb(playlist);
  if (!db) {
    setLastError("RefreshLiveCatalog: cannot open xtream.db");
    return result;
  }

  std::atomic<bool> cancel{false};
  if (!m_xtreamClient->RefreshAllLive(const_cast<Playlist &>(playlist), *db,
                                      nullptr, cancel)) {
    setLastError("RefreshLiveCatalog: RefreshAllLive failed");
    result.code = ErrorCode::NetworkError;
    return result;
  }

  auto streams = db->GetAllLiveStreams();
  if (streams.empty()) {
    LOG_INFO("PlaylistManager::RefreshLiveCatalog: '%s' has no live streams "
             "(VOD-only provider)",
             playlist.getTitle().c_str());
    result.code = ErrorCode::OK;
    return result;
  }

  bool legacy = false;
  {
    const std::string probeUrl = m_xtreamClient->BuildLiveUrl(
        playlist, streams.front().streamId, streams.front().customSid, false);
    UrlAvailabilityResult probe =
        CheckUrlAvailability(probeUrl, "", 5, 1024 * 1024);
    if (!probe.available) {
      LOG_INFO("RefreshLiveCatalog: '/live/' format unavailable for '%s'; "
               "using legacy format",
               playlist.getTitle().c_str());
      legacy = true;
    }
  }

  const auto liveCats = m_xtreamClient->GetCategories(
      playlist, xtream::XtreamApiClient::CategoryType::Live);
  std::unordered_map<int, std::string> liveCatName;
  liveCatName.reserve(liveCats.size());
  for (const auto &c : liveCats) {
    liveCatName[c.id] = c.name;
    xtream::XtreamDatabase::Category cat;
    cat.id = c.id;
    cat.type = "live";
    cat.name = c.name;
    db->UpsertCategory(cat);
  }

  result.channels.reserve(streams.size());
  const std::string plTitle = playlist.getTitle();
  const std::string plId = playlist.getUniqueId();

  for (const auto &s : streams) {
    const std::string url = m_xtreamClient->BuildLiveUrl(
        playlist, s.streamId, s.customSid, legacy);
    if (url.empty())
      continue;

    Channel ch;
    ch.setName(s.name);
    ch.setUrl(url);
    ch.setTvgId(s.epgChannelId);
    ch.setLogo(s.posterUrl);
    ch.setPlaylistName(plTitle);
    ch.setPlaylistId(plId);
    ch.setStreamId(s.streamId);
    ch.setMediaType(MediaType::Live);

    auto catIt = liveCatName.find(s.categoryId);
    if (catIt != liveCatName.end())
      ch.setGroupTitle(catIt->second);

    result.channels.push_back(std::move(ch));
  }

  result.code = ErrorCode::OK;
  LOG_INFO("PlaylistManager::RefreshLiveCatalog: '%s' → %zu channels "
           "(legacy=%d)",
           plTitle.c_str(), result.channels.size(), legacy ? 1 : 0);
  return result;
}

int PlaylistManager::GetCatchupStyle(const Playlist &playlist) {
  const std::string uuid = playlist.getUniqueId();
  if (uuid.empty())
    return 1;

  xtream::XtreamDatabase *db = GetXtreamDb(playlist);
  if (!db || !m_xtreamClient)
    return 1;

  // Читаем сохранённый стиль из meta.
  const std::string cached = db->GetMeta("catchup_style");
  if (cached == "1" || cached == "2")
    return std::stoi(cached);

  // Проба: первый live-канал с tv_archive.
  auto streams = db->GetAllLiveStreams();
  const xtream::XtreamDatabase::LiveStream *probe = nullptr;
  for (const auto &s : streams) {
    if (s.tvArchive > 0 && s.tvArchiveDuration > 0) {
      probe = &s;
      break;
    }
  }
  if (!probe)
    return 1;

  const time_t now = std::time(nullptr);
  const int64_t startUtc = static_cast<int64_t>(now) - 3600;
  const int durationMin = 60;

  auto tryUrl = [&](bool queryStyle) -> bool {
    const std::string url = m_xtreamClient->BuildCatchupUrl(
        playlist, probe->streamId, startUtc, durationMin, queryStyle);
    if (url.empty())
      return false;
    UrlAvailabilityResult r = CheckUrlAvailability(url, "", 3, 1024 * 1024);
    return r.available;
  };

  int style = 1;
  if (!tryUrl(false) && tryUrl(true))
    style = 2;

  db->SetMeta("catchup_style", std::to_string(style));
  LOG_INFO("PlaylistManager::GetCatchupStyle: '%s' → %s", uuid.c_str(),
           style == 2 ? "query" : "path");
  return style;
}

bool PlaylistManager::RefreshVodCatalog(const Playlist &playlist) {
  if (!playlist.isXtream() || !m_xtreamClient)
    return false;
  xtream::XtreamDatabase *db = GetXtreamDb(playlist);
  if (!db)
    return false;

  AtomicFlagGuard flagGuard(m_vodRefreshingAtomic);

  std::atomic<bool> cancel{false};
  if (!m_xtreamClient->RefreshAllVod(const_cast<Playlist &>(playlist), *db,
                                     nullptr, cancel)) {
    LOG_WARN("PlaylistManager::RefreshVodCatalog: RefreshAllVod failed for "
             "'%s'",
             playlist.getTitle().c_str());
    return false;
  }

  const auto vodCats = m_xtreamClient->GetCategories(
      playlist, xtream::XtreamApiClient::CategoryType::Vod);
  for (const auto &c : vodCats) {
    xtream::XtreamDatabase::Category cat;
    cat.id = c.id;
    cat.type = "vod";
    cat.name = c.name;
    db->UpsertCategory(cat);
  }
  LOG_INFO("PlaylistManager::RefreshVodCatalog: '%s' done (hasVod=%d)",
           playlist.getTitle().c_str(), vodCats.empty() ? 0 : 1);
  return true;
}

bool PlaylistManager::RefreshSeriesCatalog(const Playlist &playlist) {
  if (!playlist.isXtream() || !m_xtreamClient)
    return false;
  xtream::XtreamDatabase *db = GetXtreamDb(playlist);
  if (!db)
    return false;

  AtomicFlagGuard flagGuard(m_seriesRefreshingAtomic);

  std::atomic<bool> cancel{false};
  if (!m_xtreamClient->RefreshAllSeries(const_cast<Playlist &>(playlist), *db,
                                        nullptr, cancel)) {
    LOG_WARN("PlaylistManager::RefreshSeriesCatalog: RefreshAllSeries failed "
             "for '%s'",
             playlist.getTitle().c_str());
    return false;
  }

  const auto seriesCats = m_xtreamClient->GetCategories(
      playlist, xtream::XtreamApiClient::CategoryType::Series);
  for (const auto &c : seriesCats) {
    xtream::XtreamDatabase::Category cat;
    cat.id = c.id;
    cat.type = "series";
    cat.name = c.name;
    db->UpsertCategory(cat);
  }
  LOG_INFO("PlaylistManager::RefreshSeriesCatalog: '%s' done (hasSeries=%d)",
           playlist.getTitle().c_str(), seriesCats.empty() ? 0 : 1);
  return true;
}

ErrorCode PlaylistManager::RefreshXtreamCatalog(Playlist *playlist) {
  if (!playlist || !playlist->isXtream()) {
    setLastError("RefreshXtreamCatalog: not Xtream playlist");
    return ErrorCode::InvalidArgument;
  }

  auto liveRes = RefreshLiveCatalog(*playlist);
  if (liveRes.code != ErrorCode::OK)
    return liveRes.code;
  playlist->setChannels(std::move(liveRes.channels));

  const bool hasVod = RefreshVodCatalog(*playlist);
  const bool hasSeries = RefreshSeriesCatalog(*playlist);
  playlist->setHasVod(hasVod);
  playlist->setHasSeries(hasSeries);

  LOG_INFO("PlaylistManager::RefreshXtreamCatalog: '%s' done "
           "(live=%zu, hasVod=%d, hasSeries=%d)",
           playlist->getTitle().c_str(), playlist->getChannelCount(),
           hasVod ? 1 : 0, hasSeries ? 1 : 0);
  return ErrorCode::OK;
}

ErrorCode PlaylistManager::loadPlaylistContent(Playlist *playlist) {
  if (!playlist) {
    setLastError("Invalid playlist pointer");
    return ErrorCode::InvalidIndex;
  }

  // Path 1: Xtream — DB cache read only (no network here).
  // Fills m_channels from the existing live_streams table (if the DB was
  // already populated by previous refreshes). Lazy RefreshAllLive is
  // triggered from MainFrame on first entry to Channels. For Xtream this
  // method is a network no-op.
  if (playlist->isXtream()) {
    xtream::XtreamDatabase *db = GetXtreamDb(*playlist);
    if (!db) {
      LOG_WARN("loadPlaylistContent: cannot open xtream.db for '%s'",
               playlist->getTitle().c_str());
      return ErrorCode::OK;
    }
    auto streams = db->GetAllLiveStreams();
    if (!streams.empty() && m_xtreamClient) {
      std::vector<Channel> channels;
      channels.reserve(streams.size());
      const std::string plTitle = playlist->getTitle();
      const std::string plId = playlist->getUniqueId();

      const auto liveCats = db->GetCategories("live");
      std::unordered_map<int, std::string> liveCatName;
      liveCatName.reserve(liveCats.size());
      for (const auto &c : liveCats)
        liveCatName[c.id] = c.name;

      for (const auto &s : streams) {
        const std::string url =
            m_xtreamClient->BuildLiveUrl(*playlist, s.streamId, s.customSid);
        if (url.empty())
          continue;
        Channel ch;
        ch.setName(s.name);
        ch.setUrl(url);
        ch.setTvgId(s.epgChannelId);
        ch.setLogo(s.posterUrl);
        ch.setPlaylistName(plTitle);
        ch.setPlaylistId(plId);
        ch.setStreamId(s.streamId);
        ch.setMediaType(MediaType::Live);
        auto catIt = liveCatName.find(s.categoryId);
        if (catIt != liveCatName.end())
          ch.setGroupTitle(catIt->second);
        channels.push_back(std::move(ch));
      }
      playlist->setChannels(std::move(channels));
    }

    const auto vodCats = db->GetCategories("vod");
    const auto seriesCats = db->GetCategories("series");
    playlist->setHasVod(!vodCats.empty());
    playlist->setHasSeries(!seriesCats.empty());

    LOG_INFO("PlaylistManager: '%s' loaded from cache (%zu live, hasVod=%d, "
             "hasSeries=%d)",
             playlist->getTitle().c_str(), playlist->getChannelCount(),
             playlist->hasVod() ? 1 : 0, playlist->hasSeries() ? 1 : 0);
    return ErrorCode::OK;
  }

  // Path 2: M3U — download/read + parser only. No Xtream logic here.
  std::string content;
  bool ok = false;
  if (playlist->isUrl()) {
    // Bounded network call: without timeouts curl defaults to 300 s on
    // connect and unlimited on transfer, which can hang Update and the
    // process exit. 30 s connect covers slow-but-alive networks; 600 s
    // total covers large playlists on slow links, while leaving the stall
    // detector (30 s with no bytes) as the effective upper bound on a dead
    // server.
    DownloadProgress prog;
    prog.connectTimeoutSec = 30;
    prog.totalTimeoutSec = 600;
    ok = (downloadUrl(playlist->getSource(), content, playlist->getUserAgent(),
                      &prog) == ErrorCode::OK);
  } else {
    ok = readFile(playlist->getSource(), content);
  }

  if (!ok)
    return ErrorCode::FileNotFound;

  // Xtream API (player_api.php) and other JSON endpoints return an object
  // or array instead of M3U. Reject before the parser — otherwise M3UParser
  // will produce an unhelpful "no channels found".
  {
    size_t i = 0;
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF)
      i = 3;
    while (i < content.size()) {
      const char c = content[i];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        ++i;
      else
        break;
    }
    if (i < content.size() && (content[i] == '{' || content[i] == '[')) {
      setLastError("Server returned JSON (Xtream API). "
                   "Use get.php for M3U playlists.");
      return ErrorCode::Unknown;
    }
  }

  auto result = m_parser.parse(content);
  if (!result.success) {
    setLastError(result.error);
    return result.code;
  }
  if (result.channels.empty()) {
    setLastError("Parser returned empty channel list");
    return ErrorCode::Unknown;
  }

  playlist->setChannels(std::move(result.channels));

  // M3U invariants: no VOD/Series catalogs. Fresh tmp.playlist
  // (updatePlaylist) and addPlaylistFromFile already have
  // hasVod=false / hasSeries=false from the constructor; we set them
  // explicitly as defense against accidental pollution from legacy JSON.
  playlist->setHasVod(false);
  playlist->setHasSeries(false);

  return ErrorCode::OK;
}

bool PlaylistManager::isDuplicate(const std::string & /*title*/,
                                  const std::string &source) const {
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  for (const auto &pl : m_playlists) {
    if (pl->getSource() == source) {
      return true;
    }
  }
  return false;
}

ErrorCode PlaylistManager::addPlaylistFromFile(const std::wstring &filePathW,
                                               const std::wstring &titleW,
                                               bool autoUpdate) {
  std::string srcPath = wstringToUtf8(filePathW);
  std::string title = wstringToUtf8(titleW);

  std::string playlistTitle = title;
  if (playlistTitle.empty()) {
    size_t slash = srcPath.find_last_of("/\\");
    playlistTitle =
        (slash != std::string::npos) ? srcPath.substr(slash + 1) : srcPath;
    size_t dot = playlistTitle.find_last_of('.');
    if (dot != std::string::npos)
      playlistTitle = playlistTitle.substr(0, dot);
  }

  playlistTitle = normalizePlaylistTitle(playlistTitle);

  auto playlist = std::make_unique<Playlist>(playlistTitle, srcPath, false);
  playlist->setAutoUpdate(autoUpdate);
  ErrorCode ec = loadPlaylistContent(playlist.get());
  if (ec != ErrorCode::OK)
    return ec;

  size_t newIndex = 0;
  {
    std::lock_guard<std::mutex> lock(m_playlistsMutex);
    for (const auto &existing : m_playlists) {
      if (existing->getSource() == srcPath) {
        setLastError("Duplicate playlist: " + playlistTitle);
        return ErrorCode::DUPLICATE;
      }
    }
    m_playlists.push_back(std::move(playlist));
    newIndex = m_playlists.size() - 1;
  }

  return savePlaylist(nullptr, newIndex);
}

std::string
PlaylistManager::derivePlaylistTitleFromUrl(const std::string &url) const {
  std::string path = url;

  auto pos = path.find("://");
  if (pos != std::string::npos) {
    path = path.substr(pos + 3);
  }

  pos = path.find('@');
  if (pos != std::string::npos) {
    path = path.substr(pos + 1);
  }

  pos = path.find('?');
  if (pos != std::string::npos) {
    path = path.substr(0, pos);
  }
  pos = path.find('#');
  if (pos != std::string::npos) {
    path = path.substr(0, pos);
  }

  pos = path.find_last_of('/');
  std::string filename =
      (pos != std::string::npos) ? path.substr(pos + 1) : path;

  if (filename.empty()) {
    pos = path.find('/');
    filename = (pos != std::string::npos) ? path.substr(0, pos) : path;
  }

  pos = filename.find_last_of('.');
  if (pos != std::string::npos && pos > 0) {
    filename = filename.substr(0, pos);
  }

  if (filename.empty()) {
    filename = path;
  }

  return normalizePlaylistTitle(filename);
}

namespace {
// Same body as PlaylistManager::downloadUrl, but errors go through out-param
// instead of setLastError — safe to call from detached workers.
ErrorCode DownloadUrlNoThis(const std::string &url, std::string &content,
                            const std::string &userAgent,
                            DownloadProgress *progress,
                            std::string &errorOut) {
  CurlGlobal::instance();

  CurlSession sess;
  if (!sess.get()) {
    errorOut = "Failed to initialize CURL";
    return ErrorCode::CurlInitError;
  }

  ApplyCurlCaBundle(sess.get());

  content.clear();
  content.reserve(256 * 1024);

  curl_easy_setopt(sess.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(sess.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(sess.get(), CURLOPT_WRITEDATA, &content);
  curl_easy_setopt(sess.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(sess.get(), CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(sess.get(), CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(sess.get(), CURLOPT_MAXFILESIZE, 100 * 1024 * 1024L);

  if (!userAgent.empty())
    curl_easy_setopt(sess.get(), CURLOPT_USERAGENT, userAgent.c_str());

  if (progress) {
    curl_easy_setopt(sess.get(), CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(sess.get(), CURLOPT_XFERINFODATA, progress);
    curl_easy_setopt(sess.get(), CURLOPT_NOPROGRESS, 0L);
    progress->lastProgressTime = std::chrono::steady_clock::now();
    progress->lastDownloadedBytes = 0.0;

    if (progress->connectTimeoutSec > 0)
      curl_easy_setopt(sess.get(), CURLOPT_CONNECTTIMEOUT,
                       progress->connectTimeoutSec);
    if (progress->totalTimeoutSec > 0)
      curl_easy_setopt(sess.get(), CURLOPT_TIMEOUT,
                       progress->totalTimeoutSec);
  }

  CURLcode rc = curl_easy_perform(sess.get());
  if (rc != CURLE_OK) {
    errorOut = "CURL error: " + std::string(curl_easy_strerror(rc));
    return ErrorCode::NetworkError;
  }

  long http = 0;
  curl_easy_getinfo(sess.get(), CURLINFO_RESPONSE_CODE, &http);
  if (http != 200) {
    errorOut = "HTTP error: " + std::to_string(http);
    return ErrorCode::HttpError;
  }

  if (progress && progress->abort.load()) {
    errorOut = "Download aborted by user or stalled";
    return ErrorCode::NetworkError;
  }

  return ErrorCode::OK;
}
} // namespace

struct PlaylistManager::AddFromUrlJob {
  std::atomic<bool> cancelled{false};
  std::atomic<bool> done{false};

  std::string url;
  std::string title;
  std::string userAgent;
  bool autoUpdate = true;
  bool isXtream = false;
  XtreamMeta xtreamMeta;
  std::string configDir;
  std::string uuid;  // общий UUID для БД и плейлиста (Xtream)

  // Xtream-only: pre-opened shared resources (worker never touches `this`).
  std::shared_ptr<xtream::XtreamApiClient> xtreamClient;
  std::shared_ptr<xtream::XtreamDatabase> xtreamDb;

  // Result.
  bool success = false;
  bool hasVod = false;
  bool hasSeries = false;
  std::string errorMsg;
  std::vector<Channel> channels;
};

void PlaylistManager::CancelPendingAddFromUrl() {
  std::lock_guard<std::mutex> lk(m_addFromUrlJobMutex);
  if (m_activeAddFromUrlJob) {
    m_activeAddFromUrlJob->cancelled.store(true, std::memory_order_release);
    m_activeAddFromUrlJob.reset();
  }
}

void PlaylistManager::PostAddFromUrlEvent(
    int eventId, std::shared_ptr<AddFromUrlJob> job,
    const std::string &errorMsg) {
  if (!job || job->cancelled.load(std::memory_order_acquire))
    return;

  if (!wxTheApp)
    return;
  wxTheApp->CallAfter([eventId, job, errorMsg]() {
    if (!job || job->cancelled.load(std::memory_order_acquire))
      return;
    if (!wxTheApp)
      return;
    wxWindow *top = wxTheApp->GetTopWindow();
    if (!top)
      return;
    wxCommandEvent evt(wxEVT_COMMAND_BUTTON_CLICKED, eventId);
    if (!errorMsg.empty())
      evt.SetString(wxString::FromUTF8(errorMsg));
    top->GetEventHandler()->ProcessEvent(evt);
  });
}

void PlaylistManager::RunAddFromUrlWorker(
    std::shared_ptr<AddFromUrlJob> job) {
  // Xtream: channels come from player_api.php via lazy refresh
  // (RefreshLiveCatalog), not from get.php M3U. Skip download / parse
  // entirely; only fetch categories (VOD/Series) for UI tab visibility.
  if (job->isXtream) {
    if (job->cancelled.load(std::memory_order_acquire)) {
      job->done.store(true, std::memory_order_release);
      return;
    }
    if (job->xtreamClient && job->xtreamDb) {
      Playlist tmp(job->title, job->url, true);
      tmp.setXtream(true);
      tmp.setXtreamMeta(job->xtreamMeta);

      const auto vodCats = job->xtreamClient->GetCategories(
          tmp, xtream::XtreamApiClient::CategoryType::Vod);
      for (const auto &c : vodCats) {
        xtream::XtreamDatabase::Category cat;
        cat.id = c.id;
        cat.type = "vod";
        cat.name = c.name;
        job->xtreamDb->UpsertCategory(cat);
      }
      job->hasVod = !vodCats.empty();

      const auto seriesCats = job->xtreamClient->GetCategories(
          tmp, xtream::XtreamApiClient::CategoryType::Series);
      for (const auto &c : seriesCats) {
        xtream::XtreamDatabase::Category cat;
        cat.id = c.id;
        cat.type = "series";
        cat.name = c.name;
        job->xtreamDb->UpsertCategory(cat);
      }
      job->hasSeries = !seriesCats.empty();
    }
    if (job->cancelled.load(std::memory_order_acquire)) {
      job->done.store(true, std::memory_order_release);
      return;
    }
    job->channels.clear();
    job->success = true;
    job->done.store(true, std::memory_order_release);
    PostAddFromUrlEvent(ID_ADD_FROM_URL_SUCCESS, job, "");
    return;
  }

  // M3U path: download, reject JSON, parse M3U.
  // 1) Download (no `this`).
  std::string content;
  DownloadProgress prog;
  prog.externalCancel    = &job->cancelled;
  prog.connectTimeoutSec = 30;
  prog.totalTimeoutSec   = 600;

  std::string err;
  ErrorCode ec = DownloadUrlNoThis(job->url, content, job->userAgent, &prog,
                                   err);

  if (job->cancelled.load(std::memory_order_acquire)) {
    job->done.store(true, std::memory_order_release);
    return;
  }
  if (ec != ErrorCode::OK) {
    job->success  = false;
    job->errorMsg = "Failed to load playlist: " + err;
    job->done.store(true, std::memory_order_release);
    PostAddFromUrlEvent(ID_ADD_FROM_URL_ERROR, job, job->errorMsg);
    return;
  }

  // 2) Reject JSON (Xtream API endpoint misuse) — same detection as before.
  {
    size_t i = 0;
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF)
      i = 3;
    while (i < content.size() &&
           (content[i] == ' ' || content[i] == '\t' ||
            content[i] == '\r' || content[i] == '\n'))
      ++i;
    if (i < content.size() &&
        (content[i] == '{' || content[i] == '[')) {
      job->success  = false;
      job->errorMsg = "Server returned JSON (Xtream API). "
                      "Use get.php for M3U playlists.";
      job->done.store(true, std::memory_order_release);
      PostAddFromUrlEvent(ID_ADD_FROM_URL_ERROR, job, job->errorMsg);
      return;
    }
  }

  // 3) Parse (M3UParser is stateless — local instance is safe).
  M3UParser parser;
  auto result = parser.parse(content);
  if (!result.success || result.channels.empty()) {
    job->success  = false;
    job->errorMsg = result.success
                        ? std::string("Parser returned empty channel list")
                        : result.error;
    job->done.store(true, std::memory_order_release);
    PostAddFromUrlEvent(ID_ADD_FROM_URL_ERROR, job, job->errorMsg);
    return;
  }
  if (job->cancelled.load(std::memory_order_acquire)) {
    job->done.store(true, std::memory_order_release);
    return;
  }

  // 4) Xtream categories — writes into shared DB, sets flags on job.
  if (job->isXtream && job->xtreamClient && job->xtreamDb) {
    Playlist tmp(job->title, job->url, true);
    tmp.setXtream(true);
    tmp.setXtreamMeta(job->xtreamMeta);

    const auto vodCats = job->xtreamClient->GetCategories(
        tmp, xtream::XtreamApiClient::CategoryType::Vod);
    for (const auto &c : vodCats) {
      xtream::XtreamDatabase::Category cat;
      cat.id = c.id; cat.type = "vod"; cat.name = c.name;
      job->xtreamDb->UpsertCategory(cat);
    }
    job->hasVod = !vodCats.empty();

    const auto seriesCats = job->xtreamClient->GetCategories(
        tmp, xtream::XtreamApiClient::CategoryType::Series);
    for (const auto &c : seriesCats) {
      xtream::XtreamDatabase::Category cat;
      cat.id = c.id; cat.type = "series"; cat.name = c.name;
      job->xtreamDb->UpsertCategory(cat);
    }
    job->hasSeries = !seriesCats.empty();
  }
  if (job->cancelled.load(std::memory_order_acquire)) {
    job->done.store(true, std::memory_order_release);
    return;
  }

  // 5) Hand result to UI thread; commit is done there.
  job->channels = std::move(result.channels);
  job->success  = true;
  job->done.store(true, std::memory_order_release);
  PostAddFromUrlEvent(ID_ADD_FROM_URL_SUCCESS, job, "");
}

ErrorCode PlaylistManager::CommitActiveAddFromUrlJob() {
  std::shared_ptr<AddFromUrlJob> job;
  {
    std::lock_guard<std::mutex> lk(m_addFromUrlJobMutex);
    job = m_activeAddFromUrlJob;
    if (!job) {
      setLastError("No job ready to commit");
      return ErrorCode::Unknown;
    }
    if (!job->done.load(std::memory_order_acquire)) {
      setLastError("Job not complete");
      return ErrorCode::Unknown;
    }
    m_activeAddFromUrlJob.reset();
  }
  if (!job->success) {
    setLastError(job->errorMsg);
    return ErrorCode::Unknown;
  }

  // для Xtream source = meta.baseUrl (без creds).
  // Используется и в duplicate-check, и в Playlist: иначе второй add
  // того же Xtream (source уже baseUrl в БД, job->url = get.php?) не
  // поймается как дубликат.
  std::string effectiveSource = job->url;
  if (job->isXtream && !job->xtreamMeta.baseUrl.empty())
    effectiveSource = job->xtreamMeta.baseUrl;

  // Duplicate check under lock.
  {
    std::lock_guard<std::mutex> lock(m_playlistsMutex);
    for (const auto &existing : m_playlists) {
      if (existing->getSource() == effectiveSource) {
        setLastError("Duplicate playlist: " + job->title);
        return ErrorCode::DUPLICATE;
      }
    }
  }

  auto pl = std::make_unique<Playlist>(job->title, effectiveSource, true);
  pl->setUserAgent(job->userAgent);
  pl->setAutoUpdate(job->autoUpdate);
  if (job->isXtream && !job->uuid.empty()) {
    pl->setUniqueId(job->uuid);  // тот же UUID, что у БД
  }
  if (job->isXtream) {
    pl->setXtream(true);
    pl->setXtreamMeta(job->xtreamMeta);
    pl->setHasVod(job->hasVod);
    pl->setHasSeries(job->hasSeries);
  }
  pl->setChannels(std::move(job->channels));

  size_t newIndex = 0;
  {
    std::lock_guard<std::mutex> lock(m_playlistsMutex);
    m_playlists.push_back(std::move(pl));
    newIndex = m_playlists.size() - 1;
  }

  return savePlaylist(nullptr, newIndex);
}

ErrorCode PlaylistManager::addPlaylistFromUrl(const std::string &url,
                                              std::string &title,
                                              const std::string &userAgent,
                                              bool autoUpdate, bool isXtream,
                                              XtreamMeta xtreamMeta) {
  if (title.empty())
    title = derivePlaylistTitleFromUrl(url);

  auto job = std::make_shared<AddFromUrlJob>();
  job->url        = url;
  job->title      = title;
  job->userAgent  = userAgent;
  job->autoUpdate = autoUpdate;
  job->isXtream   = isXtream;
  job->configDir  = m_configDir;
  if (isXtream)
    job->xtreamMeta = std::move(xtreamMeta);

  if (isXtream) {
    // Генерируем UUID один раз, чтобы БД при добавлении и при открытии
    // была одна и та же (ключ в m_xtreamDbs — uuid плейлиста).
    std::string uuid = GenerateUUID();
    Playlist tmp(title, url, true);
    tmp.setXtream(true);
    tmp.setXtreamMeta(job->xtreamMeta);
    tmp.setUniqueId(uuid);
    job->xtreamClient = m_xtreamClient;
    job->xtreamDb     = GetXtreamDbShared(tmp);
    job->uuid         = uuid;
  }

  {
    std::lock_guard<std::mutex> lk(m_addFromUrlJobMutex);
    if (m_activeAddFromUrlJob)
      m_activeAddFromUrlJob->cancelled.store(true, std::memory_order_release);
    m_activeAddFromUrlJob = job;
  }

  try {
    std::thread([job]() { RunAddFromUrlWorker(job); }).detach();
  } catch (const std::system_error &e) {
    {
      std::lock_guard<std::mutex> lk(m_addFromUrlJobMutex);
      if (m_activeAddFromUrlJob == job)
        m_activeAddFromUrlJob.reset();
    }
    setLastError(std::string("Cannot start download thread: ") + e.what());
    return ErrorCode::NetworkError;
  }

  return ErrorCode::OK;
}

ErrorCode PlaylistManager::updatePlaylist(size_t index) {
  std::string src;
  std::string title;
  std::string userAgent;
  std::string uuid;
  bool isUrl = false;
  bool isXtream = false;
  XtreamMeta xtreamMeta;
  {
    std::lock_guard<std::mutex> lock(m_playlistsMutex);
    if (index >= m_playlists.size()) {
      setLastError("Invalid playlist index");
      return ErrorCode::InvalidIndex;
    }
    Playlist *pl = m_playlists[index].get();
    if (!pl) {
      setLastError("Null playlist pointer");
      return ErrorCode::Unknown;
    }
    src = pl->getSource();
    title = pl->getTitle();
    userAgent = pl->getUserAgent();
    uuid = pl->getUniqueId();
    isUrl = pl->isUrl();
    isXtream = pl->isXtream();
    if (isXtream)
      xtreamMeta = pl->getXtreamMeta();
  }

  Playlist tmp(title, src, isUrl);
  // Prerequisite для 9.3: Xtream tmp должен использовать тот же uuid, что
  // и pl (RefreshXtreamCatalog пишет в DB по uuid).
  tmp.setUniqueId(uuid);
  tmp.setUserAgent(userAgent);
  if (isXtream) {
    tmp.setXtream(true);
    tmp.setXtreamMeta(xtreamMeta);
  }

  ErrorCode ec = isXtream ? RefreshXtreamCatalog(&tmp)
                          : loadPlaylistContent(&tmp);
  if (ec != ErrorCode::OK) {
    return ec;
  }

  {
    std::lock_guard<std::mutex> lock(m_playlistsMutex);
    if (index >= m_playlists.size()) {
      setLastError("Invalid playlist index (after load)");
      return ErrorCode::InvalidIndex;
    }
    Playlist *pl = m_playlists[index].get();
    if (!pl) {
      setLastError("Null playlist pointer (after load)");
      return ErrorCode::Unknown;
    }
    pl->setChannels(tmp.getChannels());
    pl->setLastUpdate(tmp.getLastUpdate());
    if (isXtream) {
      pl->setHasVod(tmp.hasVod());
      pl->setHasSeries(tmp.hasSeries());
    }
  }

  return savePlaylist(nullptr, index);
}

std::vector<size_t>
PlaylistManager::getAutoUpdateIndices(bool includeAll) const {
  std::vector<size_t> indices;
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  for (size_t i = 0; i < m_playlists.size(); ++i) {
    const Playlist *p = m_playlists[i].get();
    if (!p)
      continue;
    // Xtream обновляется lazy (при входе на вкладки) и вручную
    // («Update playlist»). Startup auto-update — только для M3U.
    if (p->isXtream())
      continue;
    if (includeAll || p->getAutoUpdate())
      indices.push_back(i);
  }
  return indices;
}

ErrorCode PlaylistManager::removePlaylist(size_t index) {
  return removePlaylist(index, false);
}

ErrorCode PlaylistManager::removePlaylist(size_t index, bool removeSource) {
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  if (index >= m_playlists.size()) {
    setLastError("Invalid playlist index");
    return ErrorCode::InvalidIndex;
  }

  Playlist *pl = m_playlists[index].get();
  if (!pl) {
    setLastError("Null playlist pointer");
    return ErrorCode::Unknown;
  }

  std::string title = pl->getTitle();
  const std::string uuid = pl->getUniqueId();

  // закрыть и удалить xtream.db до удаления файлов плейлиста.
  CloseAndRemoveXtreamDb(uuid);

  namespace fs = std::filesystem;

  {
    std::string filename = makePlaylistFileName(title);
    fs::path configFilePath =
        ToFsPath(m_configDir) / "playlists" / ToFsPath(filename);

    std::error_code ec;
    fs::remove(configFilePath, ec);
    if (ec) {
      std::cerr << "Warning: could not remove config file " << configFilePath
                << " (" << ec.message() << ")" << std::endl;
    }
  }

  if (removeSource && !pl->isUrl()) {
    fs::path sourcePath = ToFsPath(pl->getSource());
    std::error_code ec;
    fs::remove(sourcePath, ec);
    if (ec) {
      std::cerr << "Warning: could not remove source file " << sourcePath
                << " (" << ec.message() << ")" << std::endl;
    }
  }

  m_playlists.erase(m_playlists.begin() + index);

  return ErrorCode::OK;
}

ErrorCode PlaylistManager::exportPlaylist(size_t index,
                                          const std::string &outputPath) {
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  if (index >= m_playlists.size()) {
    setLastError("Invalid playlist index");
    return ErrorCode::InvalidIndex;
  }

  Playlist *pl = m_playlists[index].get();
  std::string m3u = ExportToM3U(pl->getChannels(), pl->getTitle());

  std::ofstream out(outputPath);
  if (!out.is_open()) {
    setLastError("Cannot create file: " + outputPath);
    return ErrorCode::FileNotFound;
  }

  out << m3u;
  out.close();

  return ErrorCode::OK;
}

ErrorCode PlaylistManager::editPlaylist(size_t index, const std::string &title,
                                        const std::string &source,
                                        const std::string &userAgent,
                                        bool autoUpdate) {
  setLastError("");
  std::string oldTitle;
  {
    std::lock_guard<std::mutex> lock(m_playlistsMutex);
    if (index >= m_playlists.size()) {
      setLastError("Invalid playlist index");
      return ErrorCode::InvalidIndex;
    }
    Playlist *pl = m_playlists[index].get();
    if (!pl) {
      setLastError("Null playlist pointer");
      return ErrorCode::Unknown;
    }
    oldTitle = pl->getTitle();
    pl->setTitle(title);
    pl->setSource(source);
    pl->setUserAgent(userAgent);
    pl->setAutoUpdate(autoUpdate);
  }

  // Xtream — source = meta.baseUrl. Если baseUrl изменился,
  // сбросить meta last_update_* чтобы lazy refresh сработал при следующем
  // входе на вкладки (Channels / Movies / Series). Сетевой перезагрузки НЕ
  // делаем.
  {
    Playlist *pl = nullptr;
    {
      std::lock_guard<std::mutex> lock(m_playlistsMutex);
      if (index < m_playlists.size())
        pl = m_playlists[index].get();
    }
    if (pl && pl->isXtream()) {
      XtreamMeta meta = pl->getXtreamMeta();
      if (meta.baseUrl != source) {
        meta.baseUrl = source;
        pl->setXtreamMeta(std::move(meta));
        xtream::XtreamDatabase *db = GetXtreamDb(*pl);
        if (db) {
          db->SetMeta("last_update_live", "");
          db->SetMeta("last_update_vod", "");
          db->SetMeta("last_update_series", "");
          LOG_INFO("editPlaylist: Xtream '%s' baseUrl changed; lazy refresh "
                   "scheduled on next tab entry",
                   pl->getTitle().c_str());
        }
      }
    }
  }

  ErrorCode ec = savePlaylist(nullptr, index);
  if (ec != ErrorCode::OK)
    return ec;

  // Если title изменился — удалить старый файл <oldTitle>.json,
  // чтобы loadPlaylists при следующем запуске не нашёл дубликат.
  if (!oldTitle.empty() && oldTitle != title) {
    namespace fs = std::filesystem;
    fs::path dir = ToFsPath(m_configDir) / "playlists";
    fs::path oldPath = dir / ToFsPath(makePlaylistFileName(oldTitle));

    if (fs::exists(oldPath)) {
      std::error_code rmEc;
      bool removed = fs::remove(oldPath, rmEc);

      if (rmEc || !removed) {
        setLastError("Playlist renamed successfully, but old file could not be "
                     "removed:\n" +
                     oldPath.string() +
                     "\nPlease delete it manually to avoid a duplicate on next "
                     "start.");
      }
    }
  }

  return ErrorCode::OK;
}

Playlist *PlaylistManager::getPlaylist(std::size_t idx) {
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  if (idx >= m_playlists.size())
    return nullptr;
  return m_playlists[idx].get();
}

ErrorCode PlaylistManager::savePlaylist(const Playlist *pl, size_t idx) const {
  if (idx != (size_t)-1) {
    std::lock_guard<std::mutex> lock(m_playlistsMutex);
    if (idx >= m_playlists.size()) {
      setLastError("Invalid playlist index");
      return ErrorCode::InvalidIndex;
    }
    pl = m_playlists[idx].get();
    if (!pl) {
      setLastError("Null playlist pointer");
      return ErrorCode::Unknown;
    }
  } else {
    if (!pl) {
      setLastError("Null playlist pointer");
      return ErrorCode::Unknown;
    }
  }

  namespace fs = std::filesystem;
  fs::path dir = ToFsPath(m_configDir) / "playlists";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    setLastError("Cannot create playlists directory: " + ec.message());
    return ErrorCode::FileNotFound;
  }

  std::string filename = makePlaylistFileName(pl->getTitle());
  fs::path filePath = dir / ToFsPath(filename);

  std::string json;
  try {
    json = ToJson(*pl);
  } catch (const std::exception &ex) {
    setLastError("Serialization failed: " + std::string(ex.what()));
    return ErrorCode::Unknown;
  } catch (...) {
    setLastError("Serialization failed: unknown error");
    return ErrorCode::Unknown;
  }

  const auto u8 = filePath.u8string();
  std::string filePathU8(u8.begin(), u8.end());
  if (!atomicWrite(filePathU8, json)) {
    return ErrorCode::Unknown;
  }

  return ErrorCode::OK;
}

ErrorCode PlaylistManager::savePlaylists() const {
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  for (const auto &pl : m_playlists) {
    ErrorCode ec = savePlaylist(pl.get());
    if (ec != ErrorCode::OK)
      return ec;
  }
  return ErrorCode::OK;
}

ErrorCode PlaylistManager::loadPlaylists() {
  namespace fs = std::filesystem;
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  try {
    fs::path dir = ToFsPath(m_configDir) / "playlists";

    if (!fs::exists(dir)) {
      m_playlists.clear();
      return ErrorCode::FileNotFound;
    }

    m_playlists.clear();

    for (const auto &entry : fs::directory_iterator(dir)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json")
        continue;

      try {
        std::ifstream in(entry.path(), std::ios::binary | std::ios::ate);
        if (!in.is_open()) {
          std::cerr << "Warning: cannot open playlist file: " << entry.path()
                    << std::endl;
          continue;
        }

        std::streamsize size = in.tellg();
        if (size < 0 || size > 50 * 1024 * 1024) {
          std::cerr << "Warning: playlist file too large or invalid: "
                    << entry.path() << std::endl;
          continue;
        }

        in.seekg(0, std::ios::beg);
        std::string content;
        content.resize(static_cast<size_t>(size));

        if (!in.read(&content[0], size)) {
          std::cerr << "Warning: cannot read playlist file: " << entry.path()
                    << std::endl;
          continue;
        }

        auto pl = std::make_unique<Playlist>();
        if (!FromJson(content, *pl)) {
          std::cerr << "Warning: invalid JSON in playlist: " << entry.path()
                    << std::endl;
          continue;
        }

        // Если uniqueId отсутствует – генерируем и сразу сохраняем
        if (pl->getUniqueId().empty()) {
          pl->setUniqueId(GenerateUUID());
          savePlaylist(pl.get()); // фиксируем ID на диске
        }

        // нормализация source для Xtream. source = meta.baseUrl (без creds).
        if (pl->isXtream() && !pl->getXtreamMeta().baseUrl.empty()) {
          pl->setSource(pl->getXtreamMeta().baseUrl);
        }

        m_playlists.push_back(std::move(pl));
      } catch (const std::exception &e) {
        std::cerr << "Exception while loading playlist file " << entry.path()
                  << ": " << e.what() << std::endl;
        continue;
      } catch (...) {
        std::cerr << "Unknown exception while loading playlist file "
                  << entry.path() << std::endl;
        continue;
      }
    }

    return ErrorCode::OK;
  } catch (const std::exception &e) {
    std::cerr << "loadPlaylists fatal exception: " << e.what() << std::endl;
    return ErrorCode::Unknown;
  } catch (...) {
    std::cerr << "loadPlaylists fatal unknown exception\n";
    return ErrorCode::Unknown;
  }
}

Playlist *PlaylistManager::findByTitle(const std::string &title) {
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  for (auto &pl : m_playlists) {
    if (pl->getTitle() == title)
      return pl.get();
  }
  return nullptr;
}

std::string
PlaylistManager::makePlaylistFileName(const std::string &title) const {
  std::string base =
      NormalizeFileNameForDisk(title.empty() ? "playlist" : title);
  return base + ".json";
}

std::string
PlaylistManager::normalizePlaylistTitle(const std::string &rawTitle) const {
  std::string safe = rawTitle;
  if (safe.empty()) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    safe = std::string("playlist_") + buf;
  }
  return NormalizeFileNameForDisk(safe);
}

bool PlaylistManager::atomicWrite(const std::string &path,
                                  const std::string &data) const {
  namespace fs = std::filesystem;
  std::error_code ec;

  fs::path target = ToFsPath(path);
  fs::path dir = target.parent_path();
  if (dir.empty())
    dir = ".";

  fs::create_directories(dir, ec);
  if (ec) {
    setLastError(std::string("atomicWrite: create_directories failed: ") +
                 ec.message());
    return false;
  }

#ifdef _WIN32
  fs::path tmpPath = dir / (target.filename().wstring() + L".tmp." +
                            std::to_wstring(::GetCurrentProcessId()));
  std::wstring tmpExt = ToExtendedLengthPath(tmpPath);
  std::wstring targetExt = ToExtendedLengthPath(target);

  FILE *fp = _wfopen(tmpExt.c_str(), L"wb");
  if (!fp) {
    setLastError(
        std::string("atomicWrite: cannot open temp file for writing: ") +
        tmpPath.string());
    return false;
  }

  size_t written = fwrite(data.data(), 1, data.size(), fp);
  if (fflush(fp) != 0) {
    std::fclose(fp);
    _wremove(tmpExt.c_str());
    setLastError("atomicWrite: fflush failed");
    return false;
  }

  HANDLE h = (HANDLE)_get_osfhandle(_fileno(fp));
  if (h == INVALID_HANDLE_VALUE) {
    std::fclose(fp);
    _wremove(tmpExt.c_str());
    setLastError("atomicWrite: invalid file handle");
    return false;
  }
  if (!FlushFileBuffers(h)) {
    std::fclose(fp);
    _wremove(tmpExt.c_str());
    setLastError("atomicWrite: FlushFileBuffers failed");
    return false;
  }

  std::fclose(fp);

  if (written != data.size()) {
    _wremove(tmpExt.c_str());
    setLastError("atomicWrite: failed to write all data to temp file");
    return false;
  }

  if (!MoveFileExW(tmpExt.c_str(), targetExt.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    _wremove(tmpExt.c_str());
    setLastError(std::string("atomicWrite: MoveFileExW failed, error=") +
                 std::to_string(GetLastError()));
    return false;
  }

  return true;
#else
  std::string tmpl =
      (dir / (target.filename().string() + ".tmpXXXXXX")).string();
  std::vector<char> tmplBuf(tmpl.begin(), tmpl.end());
  tmplBuf.push_back('\0');

  int fd = mkstemp(tmplBuf.data());
  if (fd == -1) {
    setLastError(std::string("atomicWrite: mkstemp failed: ") +
                 ::strerror(errno));
    return false;
  }

  const char *buf = data.data();
  size_t remaining = data.size();
  while (remaining > 0) {
    ssize_t w = ::write(fd, buf, remaining);
    if (w <= 0) {
      int err = errno;
      ::close(fd);
      ::unlink(tmplBuf.data());
      setLastError(std::string("atomicWrite: write failed: ") +
                   ::strerror(err));
      return false;
    }
    remaining -= static_cast<size_t>(w);
    buf += w;
  }

  if (fsync(fd) != 0) {
    int err = errno;
    ::close(fd);
    ::unlink(tmplBuf.data());
    setLastError(std::string("atomicWrite: fsync failed: ") + ::strerror(err));
    return false;
  }

  ::close(fd);

  if (std::rename(tmplBuf.data(), target.string().c_str()) != 0) {
    int err = errno;
    ::unlink(tmplBuf.data());
    setLastError(std::string("atomicWrite: rename failed: ") + ::strerror(err));
    return false;
  }

  return true;
#endif
}

std::string PlaylistManager::getLastError() const {
  std::lock_guard<std::mutex> lock(m_lastErrorMutex);
  return m_lastError;
}

void PlaylistManager::setLastError(const std::string &msg) const {
  std::lock_guard<std::mutex> lock(m_lastErrorMutex);
  m_lastError = msg;
}

Playlist *PlaylistManager::findByUniqueId(const std::string &uniqueId) {
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  for (auto &pl : m_playlists) {
    if (pl->getUniqueId() == uniqueId)
      return pl.get();
  }
  return nullptr;
}

std::shared_ptr<xtream::XtreamDatabase>
PlaylistManager::GetXtreamDbShared(const Playlist &playlist) {
  if (!playlist.isXtream())
    return nullptr;

  const std::string &uuid = playlist.getUniqueId();
  if (uuid.empty())
    return nullptr;

  std::lock_guard<std::mutex> lock(m_xtreamDbMutex);

  auto it = m_xtreamDbs.find(uuid);
  if (it != m_xtreamDbs.end())
    return it->second;

  auto db = std::make_shared<xtream::XtreamDatabase>();
  if (!db->Open(wxString::FromUTF8(m_configDir.c_str()), uuid)) {
    LOG_WARN("PlaylistManager::GetXtreamDbShared: cannot open db for '%s'",
             uuid.c_str());
    return nullptr;
  }
  m_xtreamDbs[uuid] = db;
  return db;
}

xtream::XtreamDatabase *PlaylistManager::GetXtreamDb(const Playlist &playlist) {
  auto sp = GetXtreamDbShared(playlist);
  return sp ? sp.get() : nullptr;
}

void PlaylistManager::CloseAndRemoveXtreamDb(const std::string &uuid) {
  if (uuid.empty())
    return;

  std::shared_ptr<xtream::XtreamDatabase> sp;
  {
    std::lock_guard<std::mutex> lock(m_xtreamDbMutex);
    auto it = m_xtreamDbs.find(uuid);
    if (it != m_xtreamDbs.end()) {
      sp = it->second;
      m_xtreamDbs.erase(it);
    }
  }

  if (sp) {
    sp->MarkForDeferredDelete();
    // Close() is idempotent and safe to call even if workers still hold
    // shared_ptr copies: it sets open=false, waits for in-progress writers,
    // then closes connections.  Files are deleted when Close() runs (not
    // deferred to destructor) because the deferred-delete flag is set above.
    sp->Close();
    LOG_DEBUG("PlaylistManager: closed+removed xtream.db for '%s'",
              uuid.c_str());
    return;
  }

  // No live object — clean up leftover files from previous run.
  const wxString dbPath = wxString::FromUTF8(m_configDir) + wxFILE_SEP_PATH +
                          "xtream" + wxFILE_SEP_PATH +
                          wxString::FromUTF8(uuid) + ".db";
  xtream::XtreamDatabase::DeleteDbFiles(dbPath);
}

ErrorCode PlaylistManager::ClearAllXtreamCaches() {
  // Release all live objects. If workers still hold, files are deferred.
  std::unordered_map<std::string, std::shared_ptr<xtream::XtreamDatabase>>
      snapshot;
  {
    std::lock_guard<std::mutex> lock(m_xtreamDbMutex);
    snapshot.swap(m_xtreamDbs);
  }
  for (auto &kv : snapshot) {
    kv.second->MarkForDeferredDelete();
    kv.second->Close();
  }
  snapshot.clear();

  namespace fs = std::filesystem;
  const fs::path base = ToFsPath(m_configDir) / "xtream";

  std::error_code ec;
  if (!fs::exists(base, ec) || !fs::is_directory(base, ec))
    return ErrorCode::FileNotFound;

  int removed = 0;
  int failed = 0;

  for (auto it = fs::directory_iterator(base, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    const fs::path &p = it->path();

    std::error_code fileEc;
    if (!it->is_regular_file(fileEc) || fileEc)
      continue;

    const std::string ext = p.extension().string();
    if (ext != ".db" && ext != ".db-wal" && ext != ".db-shm")
      continue;

    std::error_code symEc;
    if (fs::is_symlink(p, symEc)) {
      LOG_WARN("PlaylistManager: refusing to remove symlink '%s'",
               p.string().c_str());
      continue;
    }

    std::error_code rmEc;
    if (fs::remove(p, rmEc) && !rmEc) {
      ++removed;
    } else {
      ++failed;
      LOG_WARN("PlaylistManager: cannot remove '%s': %s", p.string().c_str(),
               rmEc.message().c_str());
    }
  }

  LOG_INFO("PlaylistManager: ClearAllXtreamCaches removed %d, failed %d",
           removed, failed);

  xtream::XtreamApiClient::ResetSizeWarn();

  if (removed == 0)
    return ErrorCode::FileNotFound;
  return ErrorCode::OK;
}

long long PlaylistManager::GetTotalXtreamCacheBytes() const {
  namespace fs = std::filesystem;
  const fs::path base = ToFsPath(m_configDir) / "xtream";

  std::error_code ec;
  if (!fs::exists(base, ec) || !fs::is_directory(base, ec))
    return 0;

  long long total = 0;

  for (auto it = fs::directory_iterator(base, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    const fs::path &p = it->path();

    std::error_code fileEc;
    if (!it->is_regular_file(fileEc) || fileEc)
      continue;

    const auto size = fs::file_size(p, fileEc);
    if (!fileEc)
      total += static_cast<long long>(size);
  }

  return total;
}

// ---------------------------------------------------------------------------
// catalog auto-update
// ---------------------------------------------------------------------------

void PlaylistManager::SetCatalogUpdatedCallback(CatalogUpdatedFn fn) {
  std::lock_guard<std::mutex> lock(m_catalogCallbackMutex);
  m_catalogUpdatedCallback = std::move(fn);
}

int PlaylistManager::ComputeDueMask(const Playlist &pl) {
  if (!pl.isXtream())
    return 0;
  xtream::XtreamDatabase *db = GetXtreamDb(pl);
  if (!db)
    return 0;

  int intervalHours = 24;
  if (m_configManager)
    intervalHours =
        m_configManager->getInt("xtream_catalog_interval_hours", 24);
  if (intervalHours < 1)
    intervalHours = 1;
  if (intervalHours > 168)
    intervalHours = 168;
  const int64_t intervalSec = static_cast<int64_t>(intervalHours) * 3600;
  const int64_t now = static_cast<int64_t>(std::time(nullptr));

  auto isDue = [&](const char *key) -> bool {
    const std::string val = db->GetMeta(key);
    if (val.empty())
      return true;
    try {
      const int64_t last = std::stoll(val);
      return (now - last) >= intervalSec;
    } catch (...) {
      return true;
    }
  };

  int mask = 0;
  if (isDue("last_update_live"))
    mask |= 1;
  if (isDue("last_update_vod"))
    mask |= 2;
  if (isDue("last_update_series"))
    mask |= 4;
  return mask;
}

void PlaylistManager::RunCatalogRefresh(const std::string &uuid, int mask) {
  if (mask == 0)
    return;
  Playlist *pl = findByUniqueId(uuid);
  if (!pl || !pl->isXtream())
    return;

  const std::string mode = m_configManager
                               ? m_configManager->getSetting(
                                     "xtream_catalog_update_mode", "parallel")
                               : "parallel";

  if (mode == "sequential") {
    if ((mask & 1) && !m_liveRefreshingAtomic.load())
      RefreshLiveCatalog(*pl);
    if ((mask & 2) && !m_vodRefreshingAtomic.load())
      RefreshVodCatalog(*pl);
    if ((mask & 4) && !m_seriesRefreshingAtomic.load())
      RefreshSeriesCatalog(*pl);
  } else {
    std::vector<std::thread> threads;
    if ((mask & 1) && !m_liveRefreshingAtomic.load()) {
      threads.emplace_back([this, pl]() { RefreshLiveCatalog(*pl); });
    }
    if ((mask & 2) && !m_vodRefreshingAtomic.load()) {
      threads.emplace_back([this, pl]() { RefreshVodCatalog(*pl); });
    }
    if ((mask & 4) && !m_seriesRefreshingAtomic.load()) {
      threads.emplace_back([this, pl]() { RefreshSeriesCatalog(*pl); });
    }
    for (auto &t : threads)
      t.join();
  }

  CatalogUpdatedFn cb;
  {
    std::lock_guard<std::mutex> lock(m_catalogCallbackMutex);
    cb = m_catalogUpdatedCallback;
  }
  if (cb && wxTheApp) {
    const std::string uuidCopy = uuid;
    wxTheApp->CallAfter([cb, uuidCopy, mask]() { cb(uuidCopy, mask); });
  }
}

void PlaylistManager::CheckCatalogDueForAll() {
  if (!m_configManager)
    return;
  if (m_configManager->getSetting("xtream_catalog_autoupdate", "false") !=
      "true")
    return;

  // Snapshot: список Xtream-плейлистов (uuid, title).
  std::vector<std::pair<std::string, std::string>> snapshot;
  {
    std::lock_guard<std::mutex> lock(m_playlistsMutex);
    for (const auto &up : m_playlists) {
      if (up && up->isXtream())
        snapshot.emplace_back(up->getUniqueId(), up->getTitle());
    }
  }

  for (const auto &kv : snapshot) {
    const std::string &uuid = kv.first;
    Playlist *pl = findByUniqueId(uuid);
    if (!pl || !pl->isXtream())
      continue;
    const int mask = ComputeDueMask(*pl);
    if (mask == 0)
      continue;
    LOG_INFO("Catalog auto-update: '%s' due (mask=%d)", kv.second.c_str(),
             mask);
    RunCatalogRefresh(uuid, mask);
  }
}

void PlaylistManager::StartCatalogAutoUpdate() {
  if (m_catalogAutoUpdateThread.joinable())
    return;
  m_catalogStop.store(false, std::memory_order_release);
  m_catalogAutoUpdateThread = std::thread([this]() {
    std::unique_lock<std::mutex> lock(m_catalogMutex);
    // 9.8: первый чек через 60 сек после старта — разнесение с M3U/EPG.
    if (m_catalogCV.wait_for(lock, std::chrono::seconds(60), [this]() {
          return m_catalogStop.load(std::memory_order_acquire);
        })) {
      return;
    }
    while (!m_catalogStop.load(std::memory_order_acquire)) {
      lock.unlock();
      CheckCatalogDueForAll();
      lock.lock();
      // Периодический тик: 1 час.
      if (m_catalogCV.wait_for(lock, std::chrono::hours(1), [this]() {
            return m_catalogStop.load(std::memory_order_acquire);
          })) {
        return;
      }
    }
  });
}

void PlaylistManager::StopCatalogAutoUpdate() {
  if (!m_catalogAutoUpdateThread.joinable())
    return;
  m_catalogStop.store(true, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(m_catalogMutex);
    m_catalogCV.notify_all();
  }
  m_catalogAutoUpdateThread.join();
}

void PlaylistManager::RestartCatalogAutoUpdate() {
  StopCatalogAutoUpdate();
  if (m_configManager && m_configManager->getSetting(
                             "xtream_catalog_autoupdate", "false") == "true") {
    StartCatalogAutoUpdate();
  }
}
