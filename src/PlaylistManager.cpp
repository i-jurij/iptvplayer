#include "PlaylistManager.h"
#include "ErrorCode.h"
#include "EventIDs.h"
#include "LogControl.h"
#include "LogoCache.h"
#include "M3UParser.h"
#include "MainFrame.h"
#include "Playlist.h"
#include "Utils.h"
#include "Utils_FsPath.h"
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

PlaylistManager::PlaylistManager(const std::string &configDir)
    : m_configDir(configDir) {
  // Защита от trailing separator на входе
  while (m_configDir.size() > 1 &&
         (m_configDir.back() == '/' || m_configDir.back() == '\\')) {
    m_configDir.pop_back();
  }

  CurlGlobal::instance();
  m_xtreamClient = std::make_unique<xtream::XtreamApiClient>();
}

PlaylistManager::~PlaylistManager() {
  WaitForAddFromUrl();
  // Release shared_ptr'ы; ~XtreamDatabase закроет соединения и (если
  // помечено) удалит файлы.
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

ErrorCode PlaylistManager::loadPlaylistContent(Playlist *playlist) {
  if (!playlist) {
    setLastError("Invalid playlist pointer");
    return ErrorCode::InvalidIndex;
  }

  std::string content;
  bool ok = playlist->isUrl()
                ? (downloadUrl(playlist->getSource(), content,
                               playlist->getUserAgent()) == ErrorCode::OK)
                : readFile(playlist->getSource(), content);

  if (!ok)
    return ErrorCode::FileNotFound;

  // Xtream API (player_api.php) и другие JSON-эндпоинты возвращают объект
  // или массив вместо M3U. Отсекаем до парсера — иначе M3UParser выдаст
  // невнятное "no channels found".
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

  if (playlist->isXtream()) {
    xtream::XtreamDatabase *db = GetXtreamDb(*playlist);
    if (!db) {
      LOG_WARN("PlaylistManager::loadPlaylistContent: cannot open xtream.db "
               "for '%s', skipping VOD/Series categories",
               playlist->getTitle().c_str());
      return ErrorCode::OK;
    }

    if (m_xtreamClient) {
      const auto vodCats = m_xtreamClient->GetCategories(
          *playlist, xtream::XtreamApiClient::CategoryType::Vod);
      for (const auto &c : vodCats) {
        xtream::XtreamDatabase::Category cat;
        cat.id = c.id;
        cat.type = "vod";
        cat.name = c.name;
        db->UpsertCategory(cat);
      }
      playlist->setHasVod(!vodCats.empty());

      const auto seriesCats = m_xtreamClient->GetCategories(
          *playlist, xtream::XtreamApiClient::CategoryType::Series);
      for (const auto &c : seriesCats) {
        xtream::XtreamDatabase::Category cat;
        cat.id = c.id;
        cat.type = "series";
        cat.name = c.name;
        db->UpsertCategory(cat);
      }
      playlist->setHasSeries(!seriesCats.empty());

      LOG_INFO("PlaylistManager: '%s' has %zu VOD categories, %zu Series "
               "categories",
               playlist->getTitle().c_str(), vodCats.size(),
               seriesCats.size());
    }
  }

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
void PostAddFromUrlEvent(int eventId, const std::string &errorMsg = "") {
  if (!wxTheApp)
    return;
  wxTheApp->CallAfter([eventId, errorMsg]() {
    fprintf(stderr, "[MARK] D0: CallAfter eventId=%d err='%s'\n", eventId,
            errorMsg.c_str());
    fflush(stderr);

    wxWindow *top = wxTheApp->GetTopWindow();
    if (!top)
      return;
    wxCommandEvent evt(wxEVT_COMMAND_BUTTON_CLICKED, eventId);
    if (!errorMsg.empty())
      evt.SetString(wxString::FromUTF8(errorMsg));
    top->GetEventHandler()->ProcessEvent(evt);
  });
}
} // namespace

ErrorCode PlaylistManager::addPlaylistFromUrl(const std::string &url,
                                              std::string &title,
                                              const std::string &userAgent,
                                              bool autoUpdate, bool isXtream,
                                              XtreamMeta xtreamMeta) {
  fprintf(stderr, "[MARK] B0: enter addPlaylistFromUrl url='%s'\n",
          url.c_str());
  fflush(stderr);

  if (title.empty()) {
    title = derivePlaylistTitleFromUrl(url);
  }

  auto playlist = std::make_unique<Playlist>(title, url, true);
  playlist->setUserAgent(userAgent);
  playlist->setAutoUpdate(autoUpdate);

  if (isXtream) {
    playlist->setXtream(true);
    if (xtreamMeta.hasUserInfo())
      playlist->setXtreamMeta(std::move(xtreamMeta));
  }

  if (m_addFromUrlFuture.valid()) {
    m_addFromUrlFuture.wait();
  }

  fprintf(stderr, "[MARK] B1: before std::async\n");
  fflush(stderr);
  try {
    m_addFromUrlFuture = std::async(
        std::launch::async, [this, pl = std::move(playlist)]() mutable {
          fprintf(stderr, "[MARK] C0: async thread started\n");
          fflush(stderr);

          ErrorCode ec = loadPlaylistContent(pl.get());
          if (ec != ErrorCode::OK) {
            setLastError("Failed to load playlist: " + getLastError());
            PostAddFromUrlEvent(ID_ADD_FROM_URL_ERROR, getLastError());
            return;
          }

          size_t newIndex = 0;
          {
            std::lock_guard<std::mutex> lock(m_playlistsMutex);
            for (const auto &existing : m_playlists) {
              if (existing->getSource() == pl->getSource()) {
                setLastError("Duplicate playlist: " + pl->getTitle());
                PostAddFromUrlEvent(ID_ADD_FROM_URL_ERROR, getLastError());
                return;
              }
            }
            m_playlists.push_back(std::move(pl));
            newIndex = m_playlists.size() - 1;
          }

          ec = savePlaylist(nullptr, newIndex);
          if (ec != ErrorCode::OK) {
            setLastError("Failed to save playlist: " + getLastError());
            PostAddFromUrlEvent(ID_ADD_FROM_URL_ERROR, getLastError());
            return;
          }

          PostAddFromUrlEvent(ID_ADD_FROM_URL_SUCCESS);
        });
  } catch (const std::system_error &e) {
    setLastError(std::string("Cannot start download thread: ") + e.what());
    return ErrorCode::NetworkError;
  } catch (const std::exception &e) {
    setLastError(std::string("Cannot start download: ") + e.what());
    return ErrorCode::Unknown;
  }
  fprintf(stderr, "[MARK] B2: after std::async\n");
  fflush(stderr);

  return ErrorCode::OK;
}

ErrorCode PlaylistManager::updatePlaylist(size_t index) {
  std::string src;
  std::string title;
  std::string userAgent;
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
    isUrl = pl->isUrl();
    isXtream = pl->isXtream();
    if (isXtream)
      xtreamMeta = pl->getXtreamMeta();
  }

  Playlist tmp(title, src, isUrl);
  tmp.setUserAgent(userAgent);
  if (isXtream) {
    tmp.setXtream(true);
    tmp.setXtreamMeta(xtreamMeta);
  }

  ErrorCode ec = loadPlaylistContent(&tmp);
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
  std::string m3u = m_parser.exportToM3U(pl->getChannels(), pl->getTitle());

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
  bool sourceChanged = false;
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
    sourceChanged = (pl->getSource() != source);
    pl->setTitle(title);
    pl->setSource(source);
    pl->setUserAgent(userAgent);
    pl->setAutoUpdate(autoUpdate);
  }

  if (sourceChanged) {
    Playlist tmp(title, source, true);
    tmp.setUserAgent(userAgent);
    ErrorCode ec = loadPlaylistContent(&tmp);
    if (ec != ErrorCode::OK)
      return ec;
    {
      std::lock_guard<std::mutex> lock(m_playlistsMutex);
      if (index >= m_playlists.size()) {
        setLastError("Invalid playlist index (after load)");
        return ErrorCode::InvalidIndex;
      }
      Playlist *pl = m_playlists[index].get();
      if (pl) {
        pl->setChannels(tmp.getChannels());
        pl->setLastUpdate(tmp.getLastUpdate());
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
    json = pl->toJson();
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
        if (!pl->fromJson(content)) {
          std::cerr << "Warning: invalid JSON in playlist: " << entry.path()
                    << std::endl;
          continue;
        }

        // Если uniqueId отсутствует – генерируем и сразу сохраняем
        if (pl->getUniqueId().empty()) {
          pl->setUniqueId(GenerateUUID());
          savePlaylist(pl.get()); // фиксируем ID на диске
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

void PlaylistManager::WaitForAddFromUrl() {
  if (m_addFromUrlFuture.valid()) {
    m_addFromUrlFuture.wait();
  }
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
    if (sp.use_count() == 1) {
      // Only we hold it — Close() synchronously (waits for writers) and
      // deletes files.
      sp->Close();
      LOG_DEBUG("PlaylistManager: closed+removed xtream.db for '%s'",
                uuid.c_str());
    } else {
      // Workers still hold — file deletion deferred to last Close().
      LOG_DEBUG("PlaylistManager: deferred delete xtream.db '%s' "
                "(use_count=%ld)",
                uuid.c_str(), static_cast<long>(sp.use_count()));
    }
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
    if (kv.second.use_count() == 1) {
      kv.second->Close();
    }
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
