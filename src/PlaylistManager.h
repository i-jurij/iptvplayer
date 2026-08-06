#pragma once
#include "ErrorCode.h"
#include "M3UParser.h"
#include "Playlist.h"
#include <curl/curl.h>

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct DownloadProgress {
  std::atomic<bool> abort{false};
  std::atomic<double> totalBytes{0.0};
  std::atomic<double> downloadedBytes{0.0};
  std::chrono::steady_clock::time_point lastProgressTime;
  double lastDownloadedBytes = 0.0;
  bool stalled = false;
};

class CurlGlobal {
public:
  static CurlGlobal &instance() {
    static CurlGlobal inst;
    return inst;
  }

private:
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
  CurlGlobal(const CurlGlobal &) = delete;
  CurlGlobal &operator=(const CurlGlobal &) = delete;
};

class CurlSession {
public:
  CurlSession() : m_handle(curl_easy_init()) {}
  ~CurlSession() {
    if (m_handle)
      curl_easy_cleanup(m_handle);
  }
  CURL *get() const { return m_handle; }
  CurlSession(const CurlSession &) = delete;
  CurlSession &operator=(const CurlSession &) = delete;

private:
  CURL *m_handle = nullptr;
};

class PlaylistManager {
public:
  explicit PlaylistManager(const std::string &configDir = "");
  ~PlaylistManager();

  ErrorCode addPlaylistFromFile(const std::wstring &filePathW,
                                const std::wstring &titleW);
  ErrorCode addPlaylistFromUrl(const std::string &url, std::string &title,
                               const std::string &userAgent);
  ErrorCode downloadUrl(const std::string &url, std::string &content,
                        const std::string &userAgent,
                        DownloadProgress *progress = nullptr);

  std::string derivePlaylistTitleFromFile(const std::string &path) const;
  std::string derivePlaylistTitleFromUrl(const std::string &url) const;
  std::string makePlaylistFileName(const std::string &title) const;
  std::string normalizePlaylistTitle(const std::string &rawTitle) const;

  ErrorCode updatePlaylist(size_t index);
  int updateUrlPlaylists();
  int updateAutoUpdatePlaylists();

  ErrorCode removePlaylist(size_t index);
  ErrorCode removePlaylist(size_t index, bool removeSource);
  ErrorCode exportPlaylist(size_t index, const std::string &outputPath);
  ErrorCode editPlaylist(size_t index, const std::string &title,
                         const std::string &source,
                         const std::string &userAgent, bool autoUpdate);

  Playlist *getPlaylist(std::size_t idx);
  std::vector<std::unique_ptr<Playlist>> &getPlaylists() { return m_playlists; }
  std::size_t size() const;

  Playlist *findByTitle(const std::string &title);
  std::vector<Playlist *> findByCategory(const std::string &category);
  void clearAll();

  ErrorCode savePlaylist(const Playlist *pl, size_t idx = (size_t)-1) const;
  ErrorCode savePlaylists() const;
  ErrorCode loadPlaylists();

  bool isDuplicate(const std::string &title, const std::string &source) const;

  void WaitForAddFromUrl();

  std::string getLastError() const;
  void setLastError(const std::string &msg) const;

private:
  mutable std::mutex m_lastErrorMutex;
  mutable std::string m_lastError;

  std::future<void> m_addFromUrlFuture;

  std::string m_configDir;
  std::vector<std::unique_ptr<Playlist>> m_playlists;
  mutable std::mutex m_playlistsMutex;
  M3UParser m_parser;

  void setError(const std::string &msg) const;

  ErrorCode loadPlaylistContent(Playlist *playlist);
  bool readFile(const std::string &path, std::string &outContent);

  std::string copyIntoConfigFolder(const std::string &srcPath);
  bool atomicWrite(const std::string &path, const std::string &data) const;
};