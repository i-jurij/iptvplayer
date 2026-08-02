#include "PlaylistManager.h"
#include "ErrorCode.h"
#include "EventIDs.h"
#include "LogoCache.h"
#include "M3UParser.h"
#include "MainFrame.h"
#include "Playlist.h"
#include "Utils.h"

#include <wx/app.h>
#include <wx/event.h>
#include <wx/stdpaths.h>
#include <wx/string.h>
#include <wx/window.h>

#include <curl/curl.h>

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
#include <thread>

static std::string wstringToUtf8(const std::wstring &ws) {
  wxString tmp(ws);
  return std::string(tmp.ToUTF8().data());
}

static size_t WriteCallback(char *ptr, size_t size, size_t nmemb,
                            void *userdata) {
  std::string *out = static_cast<std::string *>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

PlaylistManager::PlaylistManager(const std::string &configDir)
    : m_configDir(configDir) {
  CurlGlobal::instance();
}

PlaylistManager::~PlaylistManager() { WaitForAddFromUrl(); }

void PlaylistManager::setError(const std::string &msg) const {
  setLastError(msg);
}

bool PlaylistManager::readFile(const std::string &path,
                               std::string &outContent) {
  std::filesystem::path fsPath(path);
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

std::string PlaylistManager::copyIntoConfigFolder(const std::string &srcPath) {
  namespace fs = std::filesystem;
  std::string baseDir = m_configDir;

  if (baseDir.empty()) {
    wxString wxDir = wxStandardPaths::Get().GetUserConfigDir();
    baseDir = wxDir.ToStdString();
  }

  fs::path destDir = fs::path(baseDir) / "playlists";
  std::error_code ec;
  fs::create_directories(destDir, ec);
  if (ec)
    return std::string();

  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S");

  fs::path src(srcPath);
  fs::path dest = destDir / (ss.str() + "_" + src.filename().string());

  fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
  if (ec)
    return std::string();

  return std::string(dest.u8string().begin(), dest.u8string().end());
}

ErrorCode PlaylistManager::downloadUrl(const std::string &url,
                                       std::string &content,
                                       const std::string &userAgent) {
  CurlGlobal::instance();

  CurlSession sess;
  if (!sess.get()) {
    setLastError("Failed to initialize CURL");
    return ErrorCode::CurlInitError;
  }

  content.clear();
  content.reserve(256 * 1024);

  curl_easy_setopt(sess.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(sess.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(sess.get(), CURLOPT_WRITEDATA, &content);
  curl_easy_setopt(sess.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(sess.get(), CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(sess.get(), CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(sess.get(), CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(sess.get(), CURLOPT_MAXFILESIZE, 50 * 1024 * 1024L);

  if (!userAgent.empty())
    curl_easy_setopt(sess.get(), CURLOPT_USERAGENT, userAgent.c_str());

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

  auto result = m_parser.parse(content);
  if (!result.success) {
    setLastError(result.error);
    return result.code;
  }

  playlist->setChannels(std::move(result.channels));
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
                                               const std::wstring &titleW) {
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

ErrorCode PlaylistManager::addPlaylistFromUrl(const std::string &url,
                                              std::string &title,
                                              const std::string &userAgent) {
  if (title.empty()) {
    title = derivePlaylistTitleFromUrl(url);
  }

  auto playlist = std::make_unique<Playlist>(title, url, true);
  playlist->setUserAgent(userAgent);

  if (m_addFromUrlFuture.valid()) {
    m_addFromUrlFuture.wait();
  }

  m_addFromUrlFuture = std::async(
      std::launch::async, [this, pl = std::move(playlist)]() mutable {
        ErrorCode ec = loadPlaylistContent(pl.get());
        if (ec != ErrorCode::OK) {
          setLastError("Failed to load playlist: " + getLastError());
          wxCommandEvent evt(wxEVT_COMMAND_BUTTON_CLICKED,
                             ID_ADD_FROM_URL_ERROR);
          evt.SetString(wxString::FromUTF8(getLastError()));
          wxQueueEvent(wxTheApp->GetTopWindow(), evt.Clone());
          return;
        }

        size_t newIndex = 0;
        {
          std::lock_guard<std::mutex> lock(m_playlistsMutex);
          for (const auto &existing : m_playlists) {
            if (existing->getSource() == pl->getSource()) {
              setLastError("Duplicate playlist: " + pl->getTitle());
              wxCommandEvent evt(wxEVT_COMMAND_BUTTON_CLICKED,
                                 ID_ADD_FROM_URL_ERROR);
              evt.SetString(wxString::FromUTF8(getLastError()));
              wxQueueEvent(wxTheApp->GetTopWindow(), evt.Clone());
              return;
            }
          }
          m_playlists.push_back(std::move(pl));
          newIndex = m_playlists.size() - 1;
        }

        ec = savePlaylist(nullptr, newIndex);
        if (ec != ErrorCode::OK) {
          setLastError("Failed to save playlist: " + getLastError());
          wxCommandEvent evt(wxEVT_COMMAND_BUTTON_CLICKED,
                             ID_ADD_FROM_URL_ERROR);
          evt.SetString(wxString::FromUTF8(getLastError()));
          wxQueueEvent(wxTheApp->GetTopWindow(), evt.Clone());
          return;
        }

        wxCommandEvent evt(wxEVT_COMMAND_BUTTON_CLICKED,
                           ID_ADD_FROM_URL_SUCCESS);
        wxQueueEvent(wxTheApp->GetTopWindow(), evt.Clone());
      });

  return ErrorCode::OK;
}

ErrorCode PlaylistManager::updatePlaylist(size_t index) {
  std::string src;
  std::string title;
  std::string userAgent;
  bool isUrl = false;
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
  }

  Playlist tmp(title, src, isUrl);
  tmp.setUserAgent(userAgent);
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
  }

  return savePlaylist(nullptr, index);
}

int PlaylistManager::updateUrlPlaylists() {
  std::vector<size_t> indices;
  {
    std::lock_guard<std::mutex> lock(m_playlistsMutex);
    for (size_t i = 0; i < m_playlists.size(); ++i) {
      if (m_playlists[i] && m_playlists[i]->isUrl())
        indices.push_back(i);
    }
  }

  int updated = 0;
  for (size_t idx : indices) {
    if (updatePlaylist(idx) == ErrorCode::OK)
      ++updated;
  }

  return updated;
}

int PlaylistManager::updateAutoUpdatePlaylists() {
  std::vector<size_t> indices;
  {
    std::lock_guard<std::mutex> lock(m_playlistsMutex);
    for (size_t i = 0; i < m_playlists.size(); ++i) {
      if (m_playlists[i] && m_playlists[i]->getAutoUpdate())
        indices.push_back(i);
    }
  }

  int updated = 0;
  for (size_t idx : indices) {
    if (updatePlaylist(idx) == ErrorCode::OK)
      ++updated;
  }

  return updated;
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
  namespace fs = std::filesystem;

  {
    std::string filename = makePlaylistFileName(title);
    fs::path configFilePath = fs::path(m_configDir) / "playlists" / filename;

    std::error_code ec;
    fs::remove(configFilePath, ec);
    if (ec) {
      std::cerr << "Warning: could not remove config file " << configFilePath
                << " (" << ec.message() << ")" << std::endl;
    }
  }

  if (removeSource && !pl->isUrl()) {
    fs::path sourcePath = pl->getSource();
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
  bool sourceChanged = false;
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

  return savePlaylist(nullptr, index);
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
  fs::path dir = fs::path(m_configDir) / "playlists";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    setLastError("Cannot create playlists directory: " + ec.message());
    return ErrorCode::FileNotFound;
  }

  std::string filename = makePlaylistFileName(pl->getTitle());
  fs::path filePath = dir / filename;

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

  if (!atomicWrite(filePath.string(), json)) {
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
    fs::path dir = fs::path(m_configDir) / "playlists";

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

std::vector<Playlist *>
PlaylistManager::findByCategory(const std::string &category) {
  std::vector<Playlist *> result;
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  for (auto &pl : m_playlists) {
    for (const auto &ch : pl->getChannels()) {
      if (ch.getCategory() == category) {
        result.push_back(pl.get());
        break;
      }
    }
  }
  return result;
}

void PlaylistManager::clearAll() {
  std::lock_guard<std::mutex> lock(m_playlistsMutex);
  m_playlists.clear();
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

  fs::path target(path);
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
  std::string tmpName = (dir / (target.filename().string() + ".tmp." +
                                std::to_string(::GetCurrentProcessId())))
                            .string();

  FILE *fp = std::fopen(tmpName.c_str(), "wb");
  if (!fp) {
    setLastError(
        std::string("atomicWrite: cannot open temp file for writing: ") +
        tmpName);
    return false;
  }

  size_t written = fwrite(data.data(), 1, data.size(), fp);
  if (fflush(fp) != 0) {
    std::fclose(fp);
    std::remove(tmpName.c_str());
    setLastError("atomicWrite: fflush failed");
    return false;
  }

  HANDLE h = (HANDLE)_get_osfhandle(_fileno(fp));
  if (h == INVALID_HANDLE_VALUE) {
    std::fclose(fp);
    std::remove(tmpName.c_str());
    setLastError("atomicWrite: invalid file handle");
    return false;
  }
  if (!FlushFileBuffers(h)) {
    std::fclose(fp);
    std::remove(tmpName.c_str());
    setLastError("atomicWrite: FlushFileBuffers failed");
    return false;
  }

  std::fclose(fp);

  if (written != data.size()) {
    std::remove(tmpName.c_str());
    setLastError("atomicWrite: failed to write all data to temp file");
    return false;
  }

  if (!MoveFileExA(tmpName.c_str(), target.string().c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    std::remove(tmpName.c_str());
    setLastError(std::string("atomicWrite: MoveFileExA failed, error=") +
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
